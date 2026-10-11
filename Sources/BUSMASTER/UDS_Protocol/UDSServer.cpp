#include "stdafx.h"
#include "UDSServer.h"
#include "resource.h"
#include "BaseDIL_CAN.h"
#include "IBusMasterKernel.h"
#include "ProtocolsDefinitions.h"
#include "VectorSeedKey.h"
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <afxdlgs.h>

#define MAX_ROWS        64
#define MAX_MATCH       32
#define MAX_DATA        512
#define MAX_PDU         4095
#define MAX_XFER        (64 * 1024)
#define MAX_FILE        (16 * 1024 * 1024)
static int g_loadCap = MAX_XFER;
#define WM_UDS_SERVER_STATUS (WM_APP + 0x61)
#define WM_UDS_SERVER_PARSE  (WM_APP + 0x62)

extern BOOL g_udsCanFd;
extern int g_udsFdDlc;

static HWND g_hStatus = NULL;

struct ParseNote
{
    int index;
    int result;
    LONG left;
};

static void PostParse(int index, int result, LONG left)
{
    if (g_hStatus == NULL || !::IsWindow(g_hStatus))
    {
        return;
    }
    ParseNote* note = (ParseNote*)malloc(sizeof(ParseNote));
    if (note == NULL)
    {
        return;
    }
    note->index = index;
    note->result = result;
    note->left = left;
    if (!::PostMessage(g_hStatus, WM_UDS_SERVER_PARSE, 0, (LPARAM)note))
    {
        free(note);
    }
}

extern int BSize;
extern int SSTMin;
extern int SizeFC;

static void PostStatus(const char* text)
{
    if (text == NULL || text[0] == 0)
    {
        return;
    }
    if (g_hStatus == NULL || !::IsWindow(g_hStatus))
    {
        return;
    }
    char* copy = _strdup(text);
    if (copy == NULL)
    {
        return;
    }
    if (!::PostMessage(g_hStatus, WM_UDS_SERVER_STATUS, 0, (LPARAM)copy))
    {
        free(copy);
    }
}

struct SvcRow
{
    BOOL on;
    BYTE sid;
    BYTE match[MAX_MATCH];
    int matchLen;
    BYTE data[MAX_DATA];
    int dataLen;
    int nrc;
    char file[MAX_PATH];
    BOOL writable;
};

struct TxJob
{
    LONG gen;
    int len;
    int startOff;
    int seq;
    int bs;
    int stMs;
    unsigned int id;
    unsigned char ext;
    unsigned char ch;
    BYTE* data;
};

static int ParseHex(const char* text, BYTE* out, int cap)
{
    int n = 0;
    int nibble = -1;
    if (text == NULL)
    {
        return 0;
    }
    for (const char* p = text; *p != 0; ++p)
    {
        char c = *p;
        int v;
        if (c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n')
        {
            if (nibble >= 0)
            {
                return -1;
            }
            continue;
        }
        if (c >= '0' && c <= '9')
        {
            v = c - '0';
        }
        else if (c >= 'A' && c <= 'F')
        {
            v = c - 'A' + 10;
        }
        else if (c >= 'a' && c <= 'f')
        {
            v = c - 'a' + 10;
        }
        else
        {
            return -1;
        }
        if (nibble < 0)
        {
            nibble = v;
        }
        else
        {
            if (n >= cap)
            {
                return -1;
            }
            out[n++] = (BYTE)((nibble << 4) | v);
            nibble = -1;
        }
    }
    if (nibble >= 0)
    {
        return -1;
    }
    return n;
}

static int ParseHexToken(const char* text, BYTE* out, int cap)
{
    if (text == NULL || text[0] == 0 || strcmp(text, "-") == 0)
    {
        return 0;
    }
    return ParseHex(text, out, cap);
}

static BOOL SidHasSub(BYTE sid)
{
    switch (sid)
    {
        case 0x10:
        case 0x11:
        case 0x27:
        case 0x28:
        case 0x3E:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x87:
        case 0x31:
            return TRUE;
    }
    return FALSE;
}

static void HexPreview(char* dst, int cap, const BYTE* p, int n)
{
    int pos = 0;
    int show = n;
    if (dst == NULL || cap <= 0)
    {
        return;
    }
    dst[0] = 0;
    if (show > 24)
    {
        show = 24;
    }
    for (int i = 0; i < show && pos + 4 < cap; i++)
    {
        if (i > 0 && pos + 1 < cap)
        {
            dst[pos++] = ' ';
        }
        pos += _snprintf(dst + pos, cap - pos, "%02X", p[i]);
    }
    if (n > show && pos + 5 < cap)
    {
        pos += _snprintf(dst + pos, cap - pos, " ...");
    }
    if (pos < 0 || pos >= cap)
    {
        dst[cap - 1] = 0;
    }
    else
    {
        dst[pos] = 0;
    }
}

struct SvcName
{
    BYTE sid;
    const char* name;
};

/* ISO 14229-1 request services (2013 and 2020). */
static const SvcName kIso14229[] =
{
    { 0x10, "DiagnosticSessionControl" },
    { 0x11, "ECUReset" },
    { 0x14, "ClearDiagnosticInformation" },
    { 0x19, "ReadDTCInformation" },
    { 0x22, "ReadDataByIdentifier" },
    { 0x23, "ReadMemoryByAddress" },
    { 0x24, "ReadScalingDataByIdentifier" },
    { 0x27, "SecurityAccess" },
    { 0x28, "CommunicationControl" },
    { 0x29, "Authentication" },
    { 0x2A, "ReadDataByPeriodicIdentifier" },
    { 0x2C, "DynamicallyDefineDataIdentifier" },
    { 0x2E, "WriteDataByIdentifier" },
    { 0x2F, "InputOutputControlByIdentifier" },
    { 0x31, "RoutineControl" },
    { 0x34, "RequestDownload" },
    { 0x35, "RequestUpload" },
    { 0x36, "TransferData" },
    { 0x37, "RequestTransferExit" },
    { 0x38, "RequestFileTransfer" },
    { 0x3D, "WriteMemoryByAddress" },
    { 0x3E, "TesterPresent" },
    { 0x83, "AccessTimingParameter" },
    { 0x84, "SecuredDataTransmission" },
    { 0x85, "ControlDTCSetting" },
    { 0x86, "ResponseOnEvent" },
    { 0x87, "LinkControl" },
};

static CString SidText(BYTE sid)
{
    CString text;
    for (int i = 0; i < (int)(sizeof(kIso14229) / sizeof(kIso14229[0])); i++)
    {
        if (kIso14229[i].sid == sid)
        {
            text.Format("%02X  %s", sid, kIso14229[i].name);
            return text;
        }
    }
    text.Format("%02X", sid);
    return text;
}

struct MatchOpt
{
    BYTE sid;
    const char* hex;
    const char* name;
};

/* Common sub-function or identifier bytes after the SID. Empty hex means no match bytes. */
static const MatchOpt kMatchOpt[] =
{
    { 0x10, "01", "defaultSession" },
    { 0x10, "02", "programmingSession" },
    { 0x10, "03", "extendedDiagnosticSession" },
    { 0x10, "04", "safetySystemDiagnosticSession" },
    { 0x11, "01", "hardReset" },
    { 0x11, "02", "keyOffOnReset" },
    { 0x11, "03", "softReset" },
    { 0x11, "04", "enableRapidPowerShutDown" },
    { 0x11, "05", "disableRapidPowerShutDown" },
    { 0x14, "FF FF FF", "allGroups" },
    { 0x14, "FF FF FE", "allGroupsWithPermanent" },
    { 0x19, "01", "reportNumberOfDTCByStatusMask" },
    { 0x19, "02", "reportDTCByStatusMask" },
    { 0x19, "03", "reportDTCSnapshotIdentification" },
    { 0x19, "04", "reportDTCSnapshotRecordByDTCNumber" },
    { 0x19, "06", "reportDTCExtDataRecordByDTCNumber" },
    { 0x19, "0A", "reportSupportedDTC" },
    { 0x19, "0B", "reportFirstTestFailedDTC" },
    { 0x19, "0C", "reportFirstConfirmedDTC" },
    { 0x19, "0D", "reportMostRecentTestFailedDTC" },
    { 0x19, "0E", "reportMostRecentConfirmedDTC" },
    { 0x19, "14", "reportDTCFaultDetectionCounter" },
    { 0x19, "15", "reportDTCWithPermanentStatus" },
    { 0x22, "F1 86", "activeDiagnosticSession" },
    { 0x22, "F1 87", "sparePartNumber" },
    { 0x22, "F1 88", "ECUSoftwareNumber" },
    { 0x22, "F1 89", "ECUSoftwareVersion" },
    { 0x22, "F1 8A", "systemSupplierIdentifier" },
    { 0x22, "F1 8C", "ECUSerialNumber" },
    { 0x22, "F1 90", "VIN" },
    { 0x22, "F1 91", "ECUHardwareNumber" },
    { 0x22, "F1 93", "systemSupplierHardwareVersion" },
    { 0x22, "F1 95", "systemSupplierSoftwareVersion" },
    { 0x22, "F1 98", "repairShopCode" },
    { 0x22, "F1 99", "programmingDate" },
    { 0x23, "14", "addr1 size4" },
    { 0x23, "24", "addr2 size4" },
    { 0x23, "44", "addr4 size4" },
    { 0x24, "F1 86", "activeDiagnosticSession" },
    { 0x24, "F1 90", "VIN" },
    { 0x27, "01", "requestSeed level1" },
    { 0x27, "02", "sendKey level1" },
    { 0x27, "03", "requestSeed level2" },
    { 0x27, "04", "sendKey level2" },
    { 0x27, "05", "requestSeed level3" },
    { 0x27, "06", "sendKey level3" },
    { 0x27, "11", "requestSeed level9" },
    { 0x27, "12", "sendKey level9" },
    { 0x28, "00", "enableRxAndTx" },
    { 0x28, "01", "enableRxAndDisableTx" },
    { 0x28, "02", "disableRxAndEnableTx" },
    { 0x28, "03", "disableRxAndTx" },
    { 0x29, "00", "deAuthenticate" },
    { 0x29, "01", "verifyCertificateUnidirectional" },
    { 0x29, "02", "verifyCertificateBidirectional" },
    { 0x29, "03", "proofOfOwnership" },
    { 0x29, "08", "authenticationConfiguration" },
    { 0x2A, "01", "sendAtSlowRate" },
    { 0x2A, "02", "sendAtMediumRate" },
    { 0x2A, "03", "sendAtFastRate" },
    { 0x2A, "04", "stopSending" },
    { 0x2C, "01", "defineByIdentifier" },
    { 0x2C, "02", "defineByMemoryAddress" },
    { 0x2C, "03", "clearDynamicallyDefinedDataIdentifier" },
    { 0x2E, "F1 90", "VIN" },
    { 0x2E, "F1 98", "repairShopCode" },
    { 0x2E, "F1 99", "programmingDate" },
    { 0x2E, "F1 87", "sparePartNumber" },
    { 0x2E, "F1 9E", "ODXFile" },
    { 0x2F, "F1 90", "VIN" },
    { 0x2F, "F1 91", "ECUHardwareNumber" },
    { 0x2F, "00", "returnControlToECU" },
    { 0x2F, "01", "resetToDefault" },
    { 0x2F, "02", "freezeCurrentState" },
    { 0x2F, "03", "shortTermAdjustment" },
    { 0x31, "01", "startRoutine" },
    { 0x31, "02", "stopRoutine" },
    { 0x31, "03", "requestRoutineResults" },
    { 0x31, "01 FF 00", "startRoutine eraseMemory" },
    { 0x31, "01 FF 01", "startRoutine checkDependencies" },
    { 0x31, "01 02 02", "startRoutine checkMemory" },
    { 0x31, "01 02 03", "startRoutine checkPreconditions" },
    { 0x34, "00 44", "noCompression addr4 size4" },
    { 0x35, "00 44", "noCompression addr4 size4" },
    { 0x36, "01", "blockSequence 1" },
    { 0x37, "", "(no sub-function)" },
    { 0x38, "01", "addFile" },
    { 0x38, "02", "deleteFile" },
    { 0x38, "03", "replaceFile" },
    { 0x38, "04", "readFile" },
    { 0x38, "05", "readDir" },
    { 0x38, "06", "resumeFile" },
    { 0x3D, "14", "addr1 size4" },
    { 0x3D, "24", "addr2 size4" },
    { 0x3D, "44", "addr4 size4" },
    { 0x3E, "00", "zeroSubFunction" },
    { 0x83, "01", "readExtendedTimingParameterSet" },
    { 0x83, "02", "setTimingParametersToDefault" },
    { 0x83, "03", "readCurrentlyActiveTimingParameters" },
    { 0x83, "04", "setTimingParametersToGivenValues" },
    { 0x84, "", "(no sub-function)" },
    { 0x85, "01", "on" },
    { 0x85, "02", "off" },
    { 0x86, "00", "stopResponseOnEvent" },
    { 0x86, "01", "onDTCStatusChange" },
    { 0x86, "03", "onChangeOfDataIdentifier" },
    { 0x86, "04", "reportActivatedEvents" },
    { 0x86, "05", "startResponseOnEvent" },
    { 0x86, "06", "clearResponseOnEvent" },
    { 0x87, "01", "verifyModeTransitionWithFixedParameter" },
    { 0x87, "02", "verifyModeTransitionWithSpecificParameter" },
    { 0x87, "03", "transitionMode" },
};

static CString HexSpaced(const BYTE* p, int n);

static CString CompactHex(const char* text)
{
    CString out;
    if (text == NULL)
    {
        return out;
    }
    for (const char* p = text; *p != 0; ++p)
    {
        char c = *p;
        if (c == ' ' || c == '\t')
        {
            continue;
        }
        if (c >= 'a' && c <= 'f')
        {
            c = (char)(c - 'a' + 'A');
        }
        out += c;
    }
    return out;
}

static CString MatchLabel(BYTE sid, const BYTE* bytes, int len)
{
    CString hex = HexSpaced(bytes, len);
    if (len <= 0)
    {
        return hex;
    }
    CString compact = CompactHex((LPCTSTR)hex);
    for (int i = 0; i < (int)(sizeof(kMatchOpt) / sizeof(kMatchOpt[0])); i++)
    {
        if (kMatchOpt[i].sid != sid || kMatchOpt[i].hex[0] == 0)
        {
            continue;
        }
        if (CompactHex(kMatchOpt[i].hex) == compact)
        {
            CString text;
            text.Format("%s  %s", (LPCTSTR)hex, kMatchOpt[i].name);
            return text;
        }
    }
    return hex;
}

static CString HexSpaced(const BYTE* p, int n)
{
    CString s;
    for (int i = 0; i < n; i++)
    {
        CString one;
        one.Format("%02X", p[i]);
        if (i > 0)
        {
            s += " ";
        }
        s += one;
    }
    return s;
}

static BOOL MatchTokenHex(const char* s, int n, const char** hex, int* hexLen)
{
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
        n -= 2;
    }
    if (n <= 0 || (n & 1) != 0)
    {
        return FALSE;
    }
    for (int i = 0; i < n; i++)
    {
        char c = s[i];
        BOOL ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        if (!ok)
        {
            return FALSE;
        }
    }
    *hex = s;
    *hexLen = n;
    return TRUE;
}

static int ParseMatchText(const char* text, BYTE* out, int cap)
{
    if (text == NULL)
    {
        return 0;
    }
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }
    if (*text == 0)
    {
        return 0;
    }
    if (_stricmp(text, "(no sub-function)") == 0)
    {
        return 0;
    }
    int n = 0;
    BOOL sawHex = FALSE;
    const char* p = text;
    while (*p != 0)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
        {
            p++;
        }
        if (*p == 0)
        {
            break;
        }
        const char* start = p;
        while (*p != 0 && *p != ' ' && *p != '\t' && *p != ',')
        {
            p++;
        }
        const char* hex = NULL;
        int hexLen = 0;
        if (!MatchTokenHex(start, (int)(p - start), &hex, &hexLen))
        {
            if (!sawHex)
            {
                return -1;
            }
            break;
        }
        sawHex = TRUE;
        for (int i = 0; i < hexLen; i += 2)
        {
            if (n >= cap)
            {
                return -1;
            }
            int hi = (hex[i] >= 'a') ? (hex[i] - 'a' + 10) : (hex[i] >= 'A') ? (hex[i] - 'A' + 10) : (hex[i] - '0');
            int lo = (hex[i + 1] >= 'a') ? (hex[i + 1] - 'a' + 10) : (hex[i + 1] >= 'A') ? (hex[i + 1] - 'A' + 10) : (hex[i + 1] - '0');
            out[n++] = (BYTE)((hi << 4) | lo);
        }
    }
    return n;
}

static CString Strip0x(CString s)
{
    s.Trim();
    if (s.GetLength() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s = s.Mid(2);
        s.Trim();
    }
    return s;
}

static BOOL ParseId(const CString& text, UINT& id)
{
    CString t = Strip0x(text);
    if (t.IsEmpty())
    {
        return FALSE;
    }
    char* end = NULL;
    unsigned long v = strtoul((LPCTSTR)t, &end, 16);
    if (end == (char*)(LPCTSTR)t || end == NULL || *end != 0)
    {
        return FALSE;
    }
    if (v > 0x1FFFFFFF)
    {
        return FALSE;
    }
    id = (UINT)v;
    return TRUE;
}

static int BuildResponse(const SvcRow& row, const BYTE* req, int reqLen, BYTE* out, int cap)
{
    if (row.nrc >= 0)
    {
        if (cap < 3)
        {
            return -1;
        }
        out[0] = 0x7F;
        out[1] = row.sid;
        out[2] = (BYTE)row.nrc;
        return 3;
    }
    if (row.dataLen >= 1 && row.data[0] == (BYTE)(row.sid + 0x40))
    {
        if (row.dataLen > cap)
        {
            return -1;
        }
        memcpy(out, row.data, row.dataLen);
        return row.dataLen;
    }
    int keyLen = row.matchLen;
    if (keyLen > reqLen - 1)
    {
        keyLen = reqLen - 1;
    }
    if (keyLen < 0)
    {
        keyLen = 0;
    }
    if (1 + keyLen + row.dataLen > cap)
    {
        return -1;
    }
    out[0] = (BYTE)(row.sid + 0x40);
    for (int i = 0; i < keyLen; i++)
    {
        BYTE b = req[1 + i];
        if (i == 0 && SidHasSub(row.sid))
        {
            b = (BYTE)(b & 0x7F);
        }
        out[1 + i] = b;
    }
    if (row.dataLen > 0)
    {
        memcpy(out + 1 + keyLen, row.data, row.dataLen);
    }
    return 1 + keyLen + row.dataLen;
}

static BOOL RowMatches(const SvcRow& row, const BYTE* req, int reqLen)
{
    if (!row.on || reqLen < 1 || req[0] != row.sid)
    {
        return FALSE;
    }
    if (row.matchLen <= 0)
    {
        return TRUE;
    }
    if (reqLen < 1 + row.matchLen)
    {
        return FALSE;
    }
    for (int i = 0; i < row.matchLen; i++)
    {
        BYTE b = req[1 + i];
        BYTE m = row.match[i];
        if (i == 0 && SidHasSub(row.sid))
        {
            b = (BYTE)(b & 0x7F);
            m = (BYTE)(m & 0x7F);
        }
        if (b != m)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static int NibbleOf(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int ReadByte2(const char* p)
{
    if (p == NULL || p[0] == 0 || p[1] == 0)
    {
        return -1;
    }
    int hi = NibbleOf(p[0]);
    int lo = NibbleOf(p[1]);
    if (hi < 0 || lo < 0)
    {
        return -1;
    }
    return (hi << 4) | lo;
}

static void TrimLine(char* s)
{
    if (s == NULL)
    {
        return;
    }
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
    {
        s[--n] = 0;
    }
    char* p = s;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    if (p != s)
    {
        memmove(s, p, strlen(p) + 1);
    }
}

static const char* FileExt(const char* path)
{
    if (path == NULL)
    {
        return "";
    }
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '\\');
    if (slash == NULL)
    {
        slash = strrchr(path, '/');
    }
    if (dot == NULL || (slash != NULL && dot < slash))
    {
        return "";
    }
    return dot + 1;
}

static BOOL ExtEq(const char* path, const char* ext)
{
    return _stricmp(FileExt(path), ext) == 0;
}

static BOOL IsDataFileExt(const char* path)
{
    return ExtEq(path, "bin") || ExtEq(path, "hex") || ExtEq(path, "mot")
           || ExtEq(path, "s19") || ExtEq(path, "s28") || ExtEq(path, "s37")
           || ExtEq(path, "srec") || ExtEq(path, "txt");
}

struct HexPiece
{
    unsigned int addr;
    BYTE* data;
    int len;
};

static void FreePieces(HexPiece* pcs, int n)
{
    if (pcs == NULL)
    {
        return;
    }
    for (int i = 0; i < n; i++)
    {
        delete[] pcs[i].data;
    }
    free(pcs);
}

static BOOL AddPiece(HexPiece** pcs, int* n, int* cap, unsigned int addr, const BYTE* data, int len)
{
    if (len <= 0)
    {
        return TRUE;
    }
    if (*n >= *cap)
    {
        int next = (*cap == 0) ? 64 : (*cap * 2);
        HexPiece* grown = (HexPiece*)realloc(*pcs, next * sizeof(HexPiece));
        if (grown == NULL)
        {
            return FALSE;
        }
        *pcs = grown;
        *cap = next;
    }
    BYTE* copy = new BYTE[len];
    memcpy(copy, data, len);
    (*pcs)[*n].addr = addr;
    (*pcs)[*n].data = copy;
    (*pcs)[*n].len = len;
    (*n)++;
    return TRUE;
}

static BYTE* BuildImage(HexPiece* pcs, int n, int* outLen)
{
    *outLen = 0;
    if (n <= 0)
    {
        return NULL;
    }
    unsigned int lo = 0xFFFFFFFF;
    unsigned int hi = 0;
    int total = 0;
    for (int i = 0; i < n; i++)
    {
        if (pcs[i].len <= 0)
        {
            continue;
        }
        if (pcs[i].addr < lo)
        {
            lo = pcs[i].addr;
        }
        unsigned int end = pcs[i].addr + (unsigned int)pcs[i].len;
        if (end > hi)
        {
            hi = end;
        }
        total += pcs[i].len;
    }
    if (total <= 0)
    {
        return NULL;
    }
    unsigned int span = (hi > lo) ? (hi - lo) : 0;
    if (span > 0 && span <= (unsigned int)g_loadCap)
    {
        BYTE* out = new BYTE[span];
        memset(out, 0xFF, span);
        for (int i = 0; i < n; i++)
        {
            if (pcs[i].len <= 0 || pcs[i].addr < lo)
            {
                continue;
            }
            unsigned int off = pcs[i].addr - lo;
            int ncopy = pcs[i].len;
            if (off >= span)
            {
                continue;
            }
            if (off + (unsigned int)ncopy > span)
            {
                ncopy = (int)(span - off);
            }
            memcpy(out + off, pcs[i].data, ncopy);
        }
        *outLen = (int)span;
        return out;
    }
    if (total > g_loadCap)
    {
        total = g_loadCap;
    }
    BYTE* out = new BYTE[total];
    int pos = 0;
    for (int i = 0; i < n && pos < total; i++)
    {
        int ncopy = pcs[i].len;
        if (pos + ncopy > total)
        {
            ncopy = total - pos;
        }
        if (ncopy > 0)
        {
            memcpy(out + pos, pcs[i].data, ncopy);
        }
        pos += ncopy;
    }
    *outLen = pos;
    return out;
}

static char* ReadTextFile(const char* path, int* outLen, int cap)
{
    *outLen = 0;
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return NULL;
    }
    if (sz > cap)
    {
        sz = cap;
    }
    char* buf = new char[(size_t)sz + 1];
    int n = (int)fread(buf, 1, (size_t)sz, f);
    buf[n] = 0;
    fclose(f);
    *outLen = n;
    return buf;
}

static BYTE* LoadLooseHex(const char* text, int* outLen)
{
    int count = 0;
    int other = 0;
    for (const char* p = text; *p != 0; ++p)
    {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',' || *p == ':')
        {
            continue;
        }
        if (NibbleOf(*p) >= 0)
        {
            count++;
        }
        else
        {
            other++;
        }
    }
    if (other > 0 || count < 2)
    {
        *outLen = 0;
        return NULL;
    }
    int nbytes = count / 2;
    if (nbytes > g_loadCap)
    {
        nbytes = g_loadCap;
    }
    BYTE* out = new BYTE[nbytes];
    int nibble = -1;
    int n = 0;
    for (const char* p = text; *p != 0 && n < nbytes; ++p)
    {
        int v = NibbleOf(*p);
        if (v < 0)
        {
            continue;
        }
        if (nibble < 0)
        {
            nibble = v;
        }
        else
        {
            out[n++] = (BYTE)((nibble << 4) | v);
            nibble = -1;
        }
    }
    *outLen = n;
    return out;
}

static BYTE* LoadIntelHex(const char* text, int* outLen)
{
    HexPiece* pcs = NULL;
    int n = 0;
    int cap = 0;
    unsigned int upper = 0;
    const char* p = text;
    while (*p != 0)
    {
        while (*p != 0 && *p != ':')
        {
            p++;
        }
        if (*p != ':')
        {
            break;
        }
        p++;
        int count = ReadByte2(p);
        int ah = ReadByte2(p + 2);
        int al = ReadByte2(p + 4);
        int type = ReadByte2(p + 6);
        p += 8;
        if (count < 0 || ah < 0 || al < 0 || type < 0)
        {
            break;
        }
        unsigned int addr = ((unsigned int)ah << 8) | (unsigned int)al;
        if (type == 0x01)
        {
            break;
        }
        if ((type == 0x04 || type == 0x02) && count == 2)
        {
            int u0 = ReadByte2(p);
            int u1 = ReadByte2(p + 2);
            if (u0 >= 0 && u1 >= 0)
            {
                unsigned int word = ((unsigned int)u0 << 8) | (unsigned int)u1;
                upper = (type == 0x04) ? (word << 16) : (word << 4);
            }
            continue;
        }
        if (type == 0x00 && count > 0)
        {
            BYTE tmp[256];
            if (count > 256)
            {
                break;
            }
            BOOL ok = TRUE;
            for (int i = 0; i < count; i++)
            {
                int b = ReadByte2(p);
                p += 2;
                if (b < 0)
                {
                    ok = FALSE;
                    break;
                }
                tmp[i] = (BYTE)b;
            }
            if (!ok || !AddPiece(&pcs, &n, &cap, upper + addr, tmp, count))
            {
                break;
            }
        }
    }
    BYTE* img = BuildImage(pcs, n, outLen);
    FreePieces(pcs, n);
    return img;
}

static BYTE* LoadSrec(const char* text, int* outLen)
{
    HexPiece* pcs = NULL;
    int n = 0;
    int cap = 0;
    const char* p = text;
    while (*p != 0)
    {
        while (*p != 0 && *p != 'S' && *p != 's')
        {
            p++;
        }
        if (*p == 0)
        {
            break;
        }
        char kind = p[1];
        p += 2;
        int addrBytes = 0;
        if (kind == '1') addrBytes = 2;
        else if (kind == '2') addrBytes = 3;
        else if (kind == '3') addrBytes = 4;
        else
        {
            continue;
        }
        int count = ReadByte2(p);
        p += 2;
        if (count < 0)
        {
            break;
        }
        unsigned int addr = 0;
        BOOL ok = TRUE;
        for (int i = 0; i < addrBytes; i++)
        {
            int b = ReadByte2(p);
            p += 2;
            if (b < 0)
            {
                ok = FALSE;
                break;
            }
            addr = (addr << 8) | (unsigned int)b;
        }
        int dataLen = count - addrBytes - 1;
        if (!ok || dataLen < 0 || dataLen > 256)
        {
            break;
        }
        BYTE tmp[256];
        for (int i = 0; i < dataLen; i++)
        {
            int b = ReadByte2(p);
            p += 2;
            if (b < 0)
            {
                ok = FALSE;
                break;
            }
            tmp[i] = (BYTE)b;
        }
        if (!ok || !AddPiece(&pcs, &n, &cap, addr, tmp, dataLen))
        {
            break;
        }
    }
    BYTE* img = BuildImage(pcs, n, outLen);
    FreePieces(pcs, n);
    return img;
}

static long long FileSizeOf(const char* path)
{
    if (path == NULL || path[0] == 0)
    {
        return -1;
    }
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || f == NULL)
    {
        return -1;
    }
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return -1;
    }
    long long n = _ftelli64(f);
    fclose(f);
    return n;
}

static BOOL LoadDataFile(const char* path, BYTE** out, int* outLen, int cap)
{
    *out = NULL;
    *outLen = 0;
    if (path == NULL || path[0] == 0)
    {
        return FALSE;
    }
    if (cap <= 0 || cap > MAX_FILE)
    {
        cap = MAX_FILE;
    }
    int savedCap = g_loadCap;
    g_loadCap = cap;
    if (ExtEq(path, "bin"))
    {
        int n = 0;
        char* raw = ReadTextFile(path, &n, cap);
        g_loadCap = savedCap;
        if (raw == NULL)
        {
            return FALSE;
        }
        *out = (BYTE*)raw;
        *outLen = n;
        return n > 0;
    }
    int n = 0;
    char* text = ReadTextFile(path, &n, cap * 4);
    if (text == NULL)
    {
        g_loadCap = savedCap;
        return FALSE;
    }
    BYTE* img = NULL;
    int len = 0;
    if (ExtEq(path, "hex"))
    {
        img = LoadIntelHex(text, &len);
    }
    else if (ExtEq(path, "mot") || ExtEq(path, "s19") || ExtEq(path, "s28")
             || ExtEq(path, "s37") || ExtEq(path, "srec"))
    {
        img = LoadSrec(text, &len);
    }
    else
    {
        img = LoadLooseHex(text, &len);
        if (img == NULL)
        {
            if (n > g_loadCap)
            {
                n = g_loadCap;
            }
            img = new BYTE[n > 0 ? n : 1];
            memcpy(img, text, n);
            len = n;
        }
    }
    delete[] text;
    g_loadCap = savedCap;
    if (img == NULL || len <= 0)
    {
        delete[] img;
        return FALSE;
    }
    *out = img;
    *outLen = len;
    return TRUE;
}

static BOOL SaveDataFile(const char* path, const BYTE* data, int len)
{
    if (path == NULL || path[0] == 0 || data == NULL || len < 0)
    {
        return FALSE;
    }
    if (len > g_loadCap)
    {
        len = g_loadCap;
    }
    FILE* f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || f == NULL)
    {
        return FALSE;
    }
    BOOL ok = TRUE;
    if (ExtEq(path, "bin"))
    {
        ok = (fwrite(data, 1, len, f) == (size_t)len);
    }
    else if (ExtEq(path, "hex"))
    {
        unsigned int addr = 0;
        int off = 0;
        unsigned int page = 0xFFFFFFFF;
        while (off < len)
        {
            if ((addr >> 16) != page)
            {
                page = addr >> 16;
                BYTE sum = 0x02 + 0x04 + (BYTE)((page >> 8) & 0xFF) + (BYTE)(page & 0xFF);
                fprintf(f, ":02000004%04X%02X\n", page & 0xFFFF, (BYTE)(0 - sum));
            }
            int n = len - off;
            if (n > 16)
            {
                n = 16;
            }
            BYTE sum = (BYTE)n + (BYTE)((addr >> 8) & 0xFF) + (BYTE)(addr & 0xFF);
            fprintf(f, ":%02X%04X00", n, addr & 0xFFFF);
            for (int i = 0; i < n; i++)
            {
                fprintf(f, "%02X", data[off + i]);
                sum = (BYTE)(sum + data[off + i]);
            }
            fprintf(f, "%02X\n", (BYTE)(0 - sum));
            addr += (unsigned int)n;
            off += n;
        }
        fprintf(f, ":00000001FF\n");
    }
    else if (ExtEq(path, "mot") || ExtEq(path, "s19") || ExtEq(path, "s28")
             || ExtEq(path, "s37") || ExtEq(path, "srec"))
    {
        unsigned int addr = 0;
        int off = 0;
        while (off < len)
        {
            int n = len - off;
            if (n > 16)
            {
                n = 16;
            }
            int count = n + 4 + 1;
            BYTE sum = (BYTE)count;
            fprintf(f, "S3%02X%08X", count, addr);
            sum = (BYTE)(sum + (BYTE)((addr >> 24) & 0xFF) + (BYTE)((addr >> 16) & 0xFF)
                         + (BYTE)((addr >> 8) & 0xFF) + (BYTE)(addr & 0xFF));
            for (int i = 0; i < n; i++)
            {
                fprintf(f, "%02X", data[off + i]);
                sum = (BYTE)(sum + data[off + i]);
            }
            fprintf(f, "%02X\n", (BYTE)(0xFF - sum));
            addr += (unsigned int)n;
            off += n;
        }
        fprintf(f, "S70500000000FA\n");
    }
    else
    {
        for (int i = 0; i < len; i++)
        {
            fprintf(f, "%02X%s", data[i], ((i + 1) % 16 == 0 || i + 1 == len) ? "\n" : " ");
        }
    }
    fclose(f);
    return ok;
}

static void FreeJob(TxJob* job)
{
    if (job == NULL)
    {
        return;
    }
    delete[] job->data;
    delete job;
}

class CUdsServerEngine
{
public:
    enum { SEED_FIXED = 0, SEED_RANDOM = 1, SEED_INC = 2 };
    enum { KEY_DLL = 0, KEY_FIXED = 1, KEY_XOR = 2, KEY_NOT = 3, KEY_ADD = 4, KEY_SWAP = 5 };

    CUdsServerEngine()
    {
        m_enable = FALSE;
        m_channel = 1;
        m_reqId = 0x7E0;
        m_respId = 0x7E8;
        m_ext = FALSE;
        m_nRows = 0;
        m_loaded = FALSE;
        m_rxState = RX_IDLE;
        m_rxExpect = 0;
        m_rxGot = 0;
        m_rxSeq = 1;
        m_rxCh = 1;
        m_txState = ST_IDLE;
        m_txLen = 0;
        m_txOff = 0;
        m_txSeq = 1;
        m_txCh = 1;
        m_txGen = 0;
        m_activeGen = 0;
        m_pDil = NULL;
        m_clientId = 0;
        m_unlockWrite = FALSE;
        m_dll[0] = 0;
        m_variant[0] = 0;
        m_level = 0x01;
        m_seedAlgo = SEED_FIXED;
        m_keyAlgo = KEY_FIXED;
        m_seedLen = 0;
        m_keyLen = 0;
        m_pendingLen = 0;
        m_rng = 0;
        m_unlocked = FALSE;
        m_keyFails = 0;
        memset(m_seed, 0, sizeof(m_seed));
        memset(m_key, 0, sizeof(m_key));
        memset(m_pending, 0, sizeof(m_pending));
        memset(m_rows, 0, sizeof(m_rows));
        memset(m_cache, 0, sizeof(m_cache));
        memset(m_rx, 0, sizeof(m_rx));
        memset(m_tx, 0, sizeof(m_tx));
        m_txHold = NULL;
        m_parseHead = NULL;
        m_parseTail = NULL;
        m_parseLeft = 0;
        m_parseStop = 0;
        m_parseSem = NULL;
        m_parseThread = NULL;
    }

    ~CUdsServerEngine()
    {
        m_parseStop = 1;
        if (m_parseSem != NULL)
        {
            ReleaseSemaphore(m_parseSem, 1, NULL);
        }
        if (m_parseThread != NULL)
        {
            WaitForSingleObject(m_parseThread, INFINITE);
            CloseHandle(m_parseThread);
            m_parseThread = NULL;
        }
        if (m_parseSem != NULL)
        {
            CloseHandle(m_parseSem);
            m_parseSem = NULL;
        }
        while (m_parseHead != NULL)
        {
            ParseJob* next = m_parseHead->next;
            delete m_parseHead;
            m_parseHead = next;
        }
        m_parseTail = NULL;
        FreeAllCache();
        delete[] m_txHold;
        m_txHold = NULL;
    }

    void EnsureLoaded()
    {
        m_cs.Lock();
        if (!m_loaded)
        {
            LoadUnlocked();
        }
        m_cs.Unlock();
    }

    void Reload()
    {
        m_cs.Lock();
        m_loaded = FALSE;
        LoadUnlocked();
        m_cs.Unlock();
    }

    void DefaultConfigPath(char* out, int cap)
    {
        m_cs.Lock();
        ConfigPath(out, cap);
        m_cs.Unlock();
    }

    BOOL SaveFile(const char* path)
    {
        m_cs.Lock();
        BOOL ok = SaveToPath(path);
        if (ok)
        {
            char defPath[MAX_PATH];
            ConfigPath(defPath, MAX_PATH);
            if (defPath[0] != 0 && _stricmp(defPath, path) != 0)
            {
                SaveToPath(defPath);
            }
        }
        m_cs.Unlock();
        return ok;
    }

    BOOL LoadFile(const char* path)
    {
        m_cs.Lock();
        BOOL ok = LoadFromPath(path);
        if (ok)
        {
            char defPath[MAX_PATH];
            ConfigPath(defPath, MAX_PATH);
            if (defPath[0] != 0 && _stricmp(defPath, path) != 0)
            {
                SaveToPath(defPath);
            }
        }
        m_cs.Unlock();
        return ok;
    }

    void GetHeader(BOOL& enable, int& channel, UINT& req, UINT& resp, BOOL& ext)
    {
        m_cs.Lock();
        if (!m_loaded)
        {
            LoadUnlocked();
        }
        enable = m_enable;
        channel = m_channel;
        req = m_reqId;
        resp = m_respId;
        ext = m_ext;
        m_cs.Unlock();
    }

    void SetHeader(BOOL enable, int channel, UINT req, UINT resp, BOOL ext)
    {
        m_cs.Lock();
        m_enable = enable;
        m_channel = channel;
        m_reqId = req;
        m_respId = resp;
        m_ext = ext;
        if (m_reqId > 0x7FF || m_respId > 0x7FF)
        {
            m_ext = TRUE;
        }
        m_loaded = TRUE;
        SaveUnlocked();
        m_cs.Unlock();
    }

    void GetSecurity(BOOL& unlock, int& level, int& seedAlgo, int& keyAlgo,
                     CString& dll, CString& variant, CString& seed, CString& key)
    {
        m_cs.Lock();
        if (!m_loaded)
        {
            LoadUnlocked();
        }
        unlock = m_unlockWrite;
        level = m_level;
        seedAlgo = m_seedAlgo;
        keyAlgo = m_keyAlgo;
        dll = m_dll;
        variant = m_variant;
        seed = HexSpaced(m_seed, m_seedLen);
        key = HexSpaced(m_key, m_keyLen);
        m_cs.Unlock();
    }

    void SetSecurity(BOOL unlock, int level, int seedAlgo, int keyAlgo,
                     const char* dll, const char* variant,
                     const BYTE* seed, int seedLen, const BYTE* key, int keyLen)
    {
        m_cs.Lock();
        m_unlockWrite = unlock;
        m_level = (level >= 1 && level <= 0x7F) ? (BYTE)level : 0x01;
        m_seedAlgo = (seedAlgo >= 0 && seedAlgo <= SEED_INC) ? seedAlgo : SEED_FIXED;
        m_keyAlgo = (keyAlgo >= 0 && keyAlgo <= KEY_SWAP) ? keyAlgo : KEY_FIXED;
        m_dll[0] = 0;
        m_variant[0] = 0;
        if (dll != NULL)
        {
            strncpy_s(m_dll, dll, _TRUNCATE);
        }
        if (variant != NULL)
        {
            strncpy_s(m_variant, variant, _TRUNCATE);
        }
        m_seedLen = 0;
        m_keyLen = 0;
        if (seed != NULL && seedLen > 0)
        {
            if (seedLen > 16)
            {
                seedLen = 16;
            }
            memcpy(m_seed, seed, seedLen);
            m_seedLen = seedLen;
        }
        if (key != NULL && keyLen > 0)
        {
            if (keyLen > 16)
            {
                keyLen = 16;
            }
            memcpy(m_key, key, keyLen);
            m_keyLen = keyLen;
        }
        m_pendingLen = 0;
        m_loaded = TRUE;
        SaveUnlocked();
        m_cs.Unlock();
    }

    int CachedLength(int index)
    {
        m_cs.Lock();
        int n = -1;
        if (index >= 0 && index < m_nRows)
        {
            n = (m_rows[index].file[0] == 0) ? 0 : m_cache[index].len;
        }
        m_cs.Unlock();
        return n;
    }

    int CachedFileBytes(int index)
    {
        m_cs.Lock();
        int n = 0;
        if (index >= 0 && index < m_nRows)
        {
            n = m_cache[index].fileBytes;
        }
        m_cs.Unlock();
        return n;
    }

    int RowCount()
    {
        m_cs.Lock();
        int n = m_nRows;
        m_cs.Unlock();
        return n;
    }

    BOOL GetRow(int index, SvcRow& row)
    {
        m_cs.Lock();
        BOOL ok = FALSE;
        if (index >= 0 && index < m_nRows)
        {
            row = m_rows[index];
            ok = TRUE;
        }
        m_cs.Unlock();
        return ok;
    }

    BOOL AddRow(const SvcRow& row)
    {
        m_cs.Lock();
        BOOL ok = FALSE;
        if (m_nRows < MAX_ROWS)
        {
            m_rows[m_nRows++] = row;
            LoadCacheAt(m_nRows - 1);
            SaveUnlocked();
            ok = TRUE;
        }
        m_cs.Unlock();
        return ok;
    }

    BOOL UpdateRow(int index, const SvcRow& row)
    {
        m_cs.Lock();
        BOOL ok = FALSE;
        if (index >= 0 && index < m_nRows)
        {
            m_rows[index] = row;
            LoadCacheAt(index);
            SaveUnlocked();
            ok = TRUE;
        }
        m_cs.Unlock();
        return ok;
    }

    BOOL DeleteRow(int index)
    {
        m_cs.Lock();
        BOOL ok = FALSE;
        if (index >= 0 && index < m_nRows)
        {
            FreeCacheAt(index);
            for (int i = index; i < m_nRows - 1; i++)
            {
                m_rows[i] = m_rows[i + 1];
                m_cache[i] = m_cache[i + 1];
            }
            m_cache[m_nRows - 1].data = NULL;
            m_cache[m_nRows - 1].len = 0;
            m_cache[m_nRows - 1].fileBytes = 0;
            InterlockedIncrement(&m_cache[m_nRows - 1].gen);
            m_nRows--;
            SaveUnlocked();
            ok = TRUE;
        }
        m_cs.Unlock();
        return ok;
    }

    void AddExamples()
    {
        m_cs.Lock();
        if (m_reqId == 0)
        {
            m_reqId = 0x7E0;
        }
        if (m_respId == 0)
        {
            m_respId = 0x7E8;
        }
        if (m_channel < 0)
        {
            m_channel = 1;
        }
        m_enable = TRUE;
        AddExampleUnlocked(0x10, "01", "");
        AddExampleUnlocked(0x10, "03", "003201F4");
        AddExampleUnlocked(0x3E, "00", "");
        AddExampleUnlocked(0x22, "F190", "3132333435363738393031323334353637");
        AddExampleUnlocked(0x22, "F187", "53573031");
        m_loaded = TRUE;
        SaveUnlocked();
        m_cs.Unlock();
    }

    void OnCan(const STCAN_MSG& msg)
    {
        OutAct act;
        memset(&act, 0, sizeof(act));
        m_cs.Lock();
        HandleLocked(msg, act);
        m_cs.Unlock();
        if (act.hasFrame && SendFrame(act.frame) != S_OK)
        {
            char buf[320];
            _snprintf(buf, sizeof(buf), "%s (send failed)", act.log[0] ? act.log : "TX");
            buf[sizeof(buf) - 1] = 0;
            PostStatus(buf);
        }
        else if (act.log[0] != 0)
        {
            PostStatus(act.log);
        }
        if (act.job != NULL)
        {
            StartTxThread(act.job);
        }
    }

    HRESULT SendFrame(const STCAN_MSG& msg)
    {
        CBaseDIL_CAN* pDil = NULL;
        DWORD clientId = 0;
        m_cs.Lock();
        BOOL ok = EnsureClientUnlocked();
        pDil = m_pDil;
        clientId = m_clientId;
        m_cs.Unlock();
        if (!ok || pDil == NULL || clientId == 0)
        {
            return E_FAIL;
        }
        return pDil->DILC_SendMsg(clientId, msg);
    }

    LONG CurrentGen()
    {
        return InterlockedCompareExchange(&m_txGen, 0, 0);
    }

    void NoteBlockPause(LONG gen, int off, int seq)
    {
        m_cs.Lock();
        if (m_activeGen == gen)
        {
            m_txOff = off;
            m_txSeq = seq;
            m_txState = ST_WAIT_FC;
        }
        m_cs.Unlock();
    }

    void NoteTxDone(LONG gen)
    {
        m_cs.Lock();
        if (m_activeGen == gen && m_txState == ST_SENDING)
        {
            m_txState = ST_IDLE;
        }
        m_cs.Unlock();
    }

private:
    enum { RX_IDLE, RX_MORE };
    enum { ST_IDLE, ST_WAIT_FC, ST_SENDING };

    struct OutAct
    {
        BOOL hasFrame;
        STCAN_MSG frame;
        TxJob* job;
        char log[280];
    };

    void AddExampleUnlocked(BYTE sid, const char* match, const char* data)
    {
        BYTE mb[MAX_MATCH];
        BYTE db[MAX_DATA];
        int ml = ParseHex(match, mb, MAX_MATCH);
        int dl = ParseHex(data, db, MAX_DATA);
        if (ml < 0)
        {
            ml = 0;
        }
        if (dl < 0)
        {
            dl = 0;
        }
        for (int i = 0; i < m_nRows; i++)
        {
            if (m_rows[i].sid == sid && m_rows[i].matchLen == ml &&
                    (ml == 0 || memcmp(m_rows[i].match, mb, ml) == 0))
            {
                m_rows[i].on = TRUE;
                m_rows[i].dataLen = dl;
                if (dl > 0)
                {
                    memcpy(m_rows[i].data, db, dl);
                }
                m_rows[i].nrc = -1;
                return;
            }
        }
        if (m_nRows >= MAX_ROWS)
        {
            return;
        }
        SvcRow& row = m_rows[m_nRows++];
        memset(&row, 0, sizeof(row));
        row.on = TRUE;
        row.sid = sid;
        row.matchLen = ml;
        if (ml > 0)
        {
            memcpy(row.match, mb, ml);
        }
        row.dataLen = dl;
        if (dl > 0)
        {
            memcpy(row.data, db, dl);
        }
        row.nrc = -1;
    }

    void ConfigPath(char* out, int cap)
    {
        out[0] = 0;
        HMODULE hMod = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(void*)&UdsServer_OnCan, &hMod))
        {
            return;
        }
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
        {
            return;
        }
        char* slash = strrchr(path, '\\');
        if (slash == NULL)
        {
            return;
        }
        slash[1] = 0;
        sprintf_s(out, cap, "%sUdsServer.cfg", path);
    }

    static void WriteHexOrDash(FILE* f, const BYTE* p, int n)
    {
        if (n <= 0)
        {
            fprintf(f, "-");
            return;
        }
        for (int i = 0; i < n; i++)
        {
            fprintf(f, "%02X", p[i]);
        }
    }

    BOOL SaveToPath(const char* path)
    {
        if (path == NULL || path[0] == 0)
        {
            return FALSE;
        }
        FILE* f = NULL;
        if (fopen_s(&f, path, "w") != 0 || f == NULL)
        {
            return FALSE;
        }
        fprintf(f, "UDS_SERVER 1\n");
        fprintf(f, "H %d %d %X %X %d\n", m_enable ? 1 : 0, m_channel, m_reqId, m_respId, m_ext ? 1 : 0);
        fprintf(f, "U %d ", m_unlockWrite ? 1 : 0);
        WriteHexOrDash(f, m_seed, m_seedLen);
        fprintf(f, " ");
        WriteHexOrDash(f, m_key, m_keyLen);
        fprintf(f, "\n");
        fprintf(f, "G %02X %d %d\n", m_level, m_seedAlgo, m_keyAlgo);
        if (m_dll[0] != 0)
        {
            fprintf(f, "L %s\n", m_dll);
        }
        if (m_variant[0] != 0)
        {
            fprintf(f, "V %s\n", m_variant);
        }
        for (int i = 0; i < m_nRows; i++)
        {
            fprintf(f, "R %d %02X ", m_rows[i].on ? 1 : 0, m_rows[i].sid);
            WriteHexOrDash(f, m_rows[i].match, m_rows[i].matchLen);
            fprintf(f, " ");
            if (m_rows[i].nrc < 0)
            {
                fprintf(f, "-");
            }
            else
            {
                fprintf(f, "%02X", m_rows[i].nrc);
            }
            fprintf(f, " ");
            WriteHexOrDash(f, m_rows[i].data, m_rows[i].dataLen);
            fprintf(f, "\n");
            if (m_rows[i].file[0] != 0)
            {
                fprintf(f, "F %s\n", m_rows[i].file);
            }
            if (m_rows[i].writable)
            {
                fprintf(f, "W 1\n");
            }
        }
        fclose(f);
        return TRUE;
    }

    void SaveUnlocked()
    {
        char path[MAX_PATH];
        ConfigPath(path, MAX_PATH);
        SaveToPath(path);
    }

    void ResetDefaults()
    {
        m_enable = FALSE;
        m_channel = 1;
        m_reqId = 0x7E0;
        m_respId = 0x7E8;
        m_ext = FALSE;
        m_nRows = 0;
        m_unlockWrite = FALSE;
        m_dll[0] = 0;
        m_variant[0] = 0;
        m_level = 0x01;
        m_seedAlgo = SEED_FIXED;
        m_keyAlgo = KEY_FIXED;
        m_seedLen = 0;
        m_keyLen = 0;
        m_pendingLen = 0;
        m_unlocked = FALSE;
        m_keyFails = 0;
        FreeAllCache();
    }

    BOOL LoadFromPath(const char* path)
    {
        if (path == NULL || path[0] == 0)
        {
            return FALSE;
        }
        FILE* f = NULL;
        if (fopen_s(&f, path, "r") != 0 || f == NULL)
        {
            return FALSE;
        }
        ResetDefaults();
        m_loaded = TRUE;
        BOOL algoFromFile = FALSE;
        char line[1600];
        while (fgets(line, sizeof(line), f) != NULL)
        {
            if (line[0] == 'H')
            {
                int en = 0;
                int ch = 1;
                int ext = 0;
                unsigned int req = 0;
                unsigned int resp = 0;
                if (sscanf(line, "H %d %d %x %x %d", &en, &ch, &req, &resp, &ext) == 5)
                {
                    m_enable = en ? TRUE : FALSE;
                    m_channel = ch;
                    m_reqId = req;
                    m_respId = resp;
                    m_ext = ext ? TRUE : FALSE;
                    if (m_reqId > 0x7FF || m_respId > 0x7FF)
                    {
                        m_ext = TRUE;
                    }
                }
            }
            else if (line[0] == 'R')
            {
                char on[8] = { 0 };
                char sid[16] = { 0 };
                char match[160] = { 0 };
                char nrc[16] = { 0 };
                char data[1400] = { 0 };
                if (sscanf(line, "R %7s %15s %159s %15s %1399s", on, sid, match, nrc, data) < 4)
                {
                    continue;
                }
                if (m_nRows >= MAX_ROWS)
                {
                    continue;
                }
                BYTE sidb = 0;
                if (ParseHexToken(sid, &sidb, 1) != 1)
                {
                    continue;
                }
                SvcRow row;
                memset(&row, 0, sizeof(row));
                row.on = (on[0] == '1') ? TRUE : FALSE;
                row.sid = sidb;
                int ml = ParseHexToken(match, row.match, MAX_MATCH);
                if (ml < 0)
                {
                    continue;
                }
                row.matchLen = ml;
                if (strcmp(nrc, "-") == 0 || nrc[0] == 0)
                {
                    row.nrc = -1;
                }
                else
                {
                    BYTE nb = 0;
                    if (ParseHexToken(nrc, &nb, 1) != 1)
                    {
                        continue;
                    }
                    row.nrc = nb;
                }
                int dl = ParseHexToken(data, row.data, MAX_DATA);
                if (dl < 0)
                {
                    continue;
                }
                row.dataLen = dl;
                m_rows[m_nRows++] = row;
            }
            else if (line[0] == 'F' && line[1] == ' ' && m_nRows > 0)
            {
                TrimLine(line + 2);
                strncpy_s(m_rows[m_nRows - 1].file, line + 2, _TRUNCATE);
            }
            else if (line[0] == 'W' && m_nRows > 0)
            {
                int w = 0;
                sscanf(line, "W %d", &w);
                m_rows[m_nRows - 1].writable = w ? TRUE : FALSE;
            }
            else if (line[0] == 'U')
            {
                int on = 0;
                char seed[80] = { 0 };
                char key[80] = { 0 };
                if (sscanf(line, "U %d %79s %79s", &on, seed, key) >= 1)
                {
                    m_unlockWrite = on ? TRUE : FALSE;
                    int sl = ParseHexToken(seed, m_seed, 16);
                    int kl = ParseHexToken(key, m_key, 16);
                    m_seedLen = (sl < 0) ? 0 : sl;
                    m_keyLen = (kl < 0) ? 0 : kl;
                }
            }
            else if (line[0] == 'G')
            {
                unsigned int level = 1;
                int seedAlgo = 0;
                int keyAlgo = 1;
                if (sscanf(line, "G %x %d %d", &level, &seedAlgo, &keyAlgo) >= 1)
                {
                    m_level = (BYTE)(level & 0x7F);
                    if (m_level == 0)
                    {
                        m_level = 0x01;
                    }
                    m_seedAlgo = (seedAlgo >= 0 && seedAlgo <= SEED_INC) ? seedAlgo : SEED_FIXED;
                    m_keyAlgo = (keyAlgo >= 0 && keyAlgo <= KEY_SWAP) ? keyAlgo : KEY_FIXED;
                    algoFromFile = TRUE;
                }
            }
            else if (line[0] == 'L' && line[1] == ' ')
            {
                TrimLine(line + 2);
                strncpy_s(m_dll, line + 2, _TRUNCATE);
            }
            else if (line[0] == 'V' && line[1] == ' ')
            {
                TrimLine(line + 2);
                strncpy_s(m_variant, line + 2, _TRUNCATE);
            }
        }
        fclose(f);
        if (!algoFromFile && m_dll[0] != 0)
        {
            m_keyAlgo = KEY_DLL;
        }
        for (int i = 0; i < m_nRows; i++)
        {
            LoadCacheAt(i);
        }
        return TRUE;
    }

    void LoadUnlocked()
    {
        ResetDefaults();
        m_loaded = TRUE;
    }

    BOOL EnsureClientUnlocked()
    {
        if (m_pDil == NULL)
        {
            if (DIL_GetInterface(CAN, (void**)&m_pDil) != S_OK || m_pDil == NULL)
            {
                m_pDil = NULL;
                return FALSE;
            }
        }
        if (m_clientId == 0)
        {
            char name[] = "UDS_SERVER";
            m_pDil->DILC_RegisterClient(TRUE, m_clientId, name);
            if (m_clientId == 0)
            {
                return FALSE;
            }
        }
        return TRUE;
    }

    int FindRow(const BYTE* pdu, int len) const
    {
        for (int i = 0; i < m_nRows; i++)
        {
            if (RowMatches(m_rows[i], pdu, len))
            {
                return i;
            }
        }
        return -1;
    }

    void FillSf(STCAN_MSG& msg, const BYTE* pdu, int len, unsigned char ch)
    {
        memset(&msg, 0, sizeof(msg));
        msg.m_unMsgID = m_respId;
        msg.m_ucEXTENDED = m_ext ? 1 : 0;
        msg.m_ucChannel = ch;
        msg.m_bCANFD = g_udsCanFd ? true : false;
        int dlc = g_udsCanFd ? g_udsFdDlc : 8;
        if (dlc < 8)
        {
            dlc = 8;
        }
        if (dlc > 64)
        {
            dlc = 64;
        }
        msg.m_ucDataLen = (unsigned char)dlc;
        if (dlc > 8)
        {
            msg.m_ucData[0] = 0x00;
            msg.m_ucData[1] = (BYTE)len;
            if (len > 0)
            {
                memcpy(&msg.m_ucData[2], pdu, len);
            }
            return;
        }
        msg.m_ucData[0] = (BYTE)len;
        if (len > 0)
        {
            memcpy(&msg.m_ucData[1], pdu, len);
        }
    }

    int FillFf(STCAN_MSG& msg, const BYTE* pdu, int len, unsigned char ch)
    {
        memset(&msg, 0, sizeof(msg));
        msg.m_unMsgID = m_respId;
        msg.m_ucEXTENDED = m_ext ? 1 : 0;
        msg.m_ucChannel = ch;
        msg.m_bCANFD = g_udsCanFd ? true : false;
        int dlc = g_udsCanFd ? g_udsFdDlc : 8;
        if (dlc < 8)
        {
            dlc = 8;
        }
        if (dlc > 64)
        {
            dlc = 64;
        }
        msg.m_ucDataLen = (unsigned char)dlc;
        if (len > 0xFFF)
        {
            msg.m_ucData[0] = 0x10;
            msg.m_ucData[1] = 0x00;
            msg.m_ucData[2] = (BYTE)((len >> 24) & 0xFF);
            msg.m_ucData[3] = (BYTE)((len >> 16) & 0xFF);
            msg.m_ucData[4] = (BYTE)((len >> 8) & 0xFF);
            msg.m_ucData[5] = (BYTE)(len & 0xFF);
            int n = dlc - 6;
            if (n > len)
            {
                n = len;
            }
            if (n < 0)
            {
                n = 0;
            }
            if (n > 0)
            {
                memcpy(&msg.m_ucData[6], pdu, n);
            }
            return n;
        }
        msg.m_ucData[0] = (BYTE)(0x10 | ((len >> 8) & 0x0F));
        msg.m_ucData[1] = (BYTE)(len & 0xFF);
        int n = dlc - 2;
        if (n > len)
        {
            n = len;
        }
        if (n > 0)
        {
            memcpy(&msg.m_ucData[2], pdu, n);
        }
        return n;
    }

    void EmitResponse(const BYTE* resp, int rlen, unsigned char ch, OutAct& act, const char* log)
    {
        if (resp == NULL || rlen <= 0 || rlen > MAX_FILE)
        {
            return;
        }
        unsigned char useCh = (m_channel == 0) ? ch : (unsigned char)m_channel;
        if (useCh == 0)
        {
            useCh = 1;
        }
        if (log != NULL)
        {
            _snprintf(act.log, sizeof(act.log), "%s", log);
            act.log[sizeof(act.log) - 1] = 0;
        }
        int txDlc = g_udsCanFd ? g_udsFdDlc : 8;
        if (txDlc < 8)
        {
            txDlc = 8;
        }
        int sfMax = (txDlc > 8) ? (txDlc - 2) : 7;
        if (rlen <= sfMax)
        {
            FillSf(act.frame, resp, rlen, useCh);
            act.hasFrame = TRUE;
            return;
        }
        int sent = FillFf(act.frame, resp, rlen, useCh);
        if (rlen <= MAX_XFER)
        {
            delete[] m_txHold;
            m_txHold = NULL;
            memcpy(m_tx, resp, rlen);
        }
        else
        {
            delete[] m_txHold;
            m_txHold = new BYTE[rlen];
            memcpy(m_txHold, resp, rlen);
        }
        m_txLen = rlen;
        m_txOff = sent;
        m_txSeq = 1;
        m_txCh = useCh;
        m_txState = ST_WAIT_FC;
        act.hasFrame = TRUE;
    }

    void EmitNrc(BYTE sid, BYTE nrc, unsigned char ch, OutAct& act, const char* log)
    {
        BYTE resp[3];
        resp[0] = 0x7F;
        resp[1] = sid;
        resp[2] = nrc;
        EmitResponse(resp, 3, ch, act, log);
    }

    BOOL StoreWrite(int idx, const BYTE* body, int bodyLen)
    {
        if (bodyLen < 0)
        {
            bodyLen = 0;
        }
        if (m_rows[idx].file[0] != 0)
        {
            if (!SaveDataFile(m_rows[idx].file, body, bodyLen))
            {
                return FALSE;
            }
            FreeCacheAt(idx);
            m_cache[idx].fileBytes = bodyLen;
            if (bodyLen > MAX_FILE)
            {
                m_cache[idx].len = -3;
                return TRUE;
            }
            if (bodyLen > 0 && body != NULL)
            {
                m_cache[idx].data = new BYTE[bodyLen];
                memcpy(m_cache[idx].data, body, bodyLen);
                m_cache[idx].len = bodyLen;
            }
            return TRUE;
        }
        if (bodyLen > MAX_DATA)
        {
            return FALSE;
        }
        if (bodyLen > 0 && body != NULL)
        {
            memcpy(m_rows[idx].data, body, bodyLen);
        }
        m_rows[idx].dataLen = bodyLen;
        SaveUnlocked();
        return TRUE;
    }

    int FindWriteRow(const BYTE* pdu, int len, int* idLen)
    {
        int fallback = -1;
        int fallbackLen = 2;
        for (int i = 0; i < m_nRows; i++)
        {
            if (!m_rows[i].on || !m_rows[i].writable)
            {
                continue;
            }
            if (m_rows[i].sid != 0x22 && m_rows[i].sid != 0x2E)
            {
                continue;
            }
            int ml = m_rows[i].matchLen;
            if (ml <= 0)
            {
                ml = 2;
            }
            if (len < 1 + ml)
            {
                continue;
            }
            if (m_rows[i].matchLen > 0 && memcmp(m_rows[i].match, pdu + 1, m_rows[i].matchLen) != 0)
            {
                continue;
            }
            if (m_rows[i].sid == 0x22)
            {
                *idLen = ml;
                return i;
            }
            fallback = i;
            fallbackLen = ml;
        }
        if (fallback >= 0)
        {
            *idLen = fallbackLen;
        }
        return fallback;
    }

    BOOL EnsureSeedDll(CString& err)
    {
        if (m_dll[0] == 0)
        {
            return FALSE;
        }
        if (m_seedDll.IsLoaded() && m_seedDll.GetPath().CompareNoCase(m_dll) == 0)
        {
            return TRUE;
        }
        return m_seedDll.Load(CString(m_dll), err);
    }

    BYTE NextRng()
    {
        if (m_rng == 0)
        {
            m_rng = GetTickCount() | 1;
        }
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return (BYTE)m_rng;
    }

    int SeedSize() const
    {
        return (m_seedLen > 0) ? m_seedLen : 4;
    }

    void MakeSeed(BYTE* out, int len)
    {
        if (len <= 0)
        {
            return;
        }
        if (m_seedAlgo == SEED_RANDOM)
        {
            for (int i = 0; i < len; i++)
            {
                out[i] = NextRng();
            }
            return;
        }
        if (m_seedAlgo == SEED_INC)
        {
            if (m_pendingLen == len)
            {
                memcpy(out, m_pending, len);
                for (int i = len - 1; i >= 0; --i)
                {
                    out[i] = (BYTE)(out[i] + 1);
                    if (out[i] != 0)
                    {
                        break;
                    }
                }
            }
            else if (m_seedLen > 0)
            {
                memset(out, 0, len);
                int n = (m_seedLen < len) ? m_seedLen : len;
                memcpy(out, m_seed, n);
            }
            else
            {
                memset(out, 0, len);
                out[len - 1] = 1;
            }
            return;
        }
        if (m_seedLen <= 0)
        {
            BYTE fallback[4] = { 0x01, 0x02, 0x03, 0x04 };
            int n = (len < 4) ? len : 4;
            memset(out, 0, len);
            memcpy(out, fallback, n);
            return;
        }
        memset(out, 0, len);
        int n = (m_seedLen < len) ? m_seedLen : len;
        memcpy(out, m_seed, n);
    }

    BOOL ExpectedKey(BYTE* key, UINT cap, UINT& keyLen, CString& err)
    {
        keyLen = 0;
        int seedLen = m_pendingLen;
        if (seedLen <= 0)
        {
            err = "No seed has been issued";
            return FALSE;
        }
        if (m_keyAlgo == KEY_DLL)
        {
            if (!EnsureSeedDll(err))
            {
                return FALSE;
            }
            UINT level = m_level;
            if (m_seedDll.GenerateKey(m_pending, (UINT)seedLen, level, m_variant, key, cap, keyLen, err))
            {
                return TRUE;
            }
            UINT alt = (UINT)((m_level + 1) / 2);
            return m_seedDll.GenerateKey(m_pending, (UINT)seedLen, alt, m_variant, key, cap, keyLen, err);
        }
        if (m_keyAlgo == KEY_FIXED)
        {
            if (m_keyLen <= 0 || (UINT)m_keyLen > cap)
            {
                err = "Fixed key is empty";
                return FALSE;
            }
            memcpy(key, m_key, m_keyLen);
            keyLen = (UINT)m_keyLen;
            return TRUE;
        }
        if ((UINT)seedLen > cap)
        {
            return FALSE;
        }
        if (m_keyAlgo == KEY_SWAP)
        {
            for (int i = 0; i < seedLen; i++)
            {
                key[i] = m_pending[seedLen - 1 - i];
            }
            keyLen = (UINT)seedLen;
            return TRUE;
        }
        for (int i = 0; i < seedLen; i++)
        {
            BYTE s = m_pending[i];
            BYTE c = 0;
            if (m_keyLen > 0)
            {
                c = m_key[i % m_keyLen];
            }
            else if (m_keyAlgo == KEY_XOR)
            {
                c = 0xFF;
            }
            else if (m_keyAlgo == KEY_ADD)
            {
                c = m_level;
            }
            if (m_keyAlgo == KEY_NOT)
            {
                key[i] = (BYTE)(~s);
            }
            else if (m_keyAlgo == KEY_ADD)
            {
                key[i] = (BYTE)(s + c);
            }
            else
            {
                key[i] = (BYTE)(s ^ c);
            }
        }
        keyLen = (UINT)seedLen;
        return TRUE;
    }

    BOOL HandleSecurity(const BYTE* pdu, int len, unsigned char ch, OutAct& act)
    {
        if (len < 2)
        {
            EmitNrc(0x27, 0x13, ch, act, "RX 27 (invalid length)");
            return TRUE;
        }
        BYTE sf = (BYTE)(pdu[1] & 0x7F);
        BOOL suppress = (pdu[1] & 0x80) != 0;
        BYTE level = m_level ? m_level : 0x01;
        BYTE keySf = (BYTE)(level + 1);
        if (sf != level && sf != keySf)
        {
            EmitNrc(0x27, 0x12, ch, act, "RX 27 (level not configured)");
            return TRUE;
        }
        if (sf == level)
        {
            int seedLen = SeedSize();
            if (seedLen > 16)
            {
                seedLen = 16;
            }
            BYTE resp[20];
            resp[0] = 0x67;
            resp[1] = sf;
            if (m_unlocked)
            {
                memset(resp + 2, 0, seedLen);
            }
            else
            {
                MakeSeed(m_pending, seedLen);
                m_pendingLen = seedLen;
                memcpy(resp + 2, m_pending, seedLen);
            }
            if (!suppress)
            {
                EmitResponse(resp, 2 + seedLen, ch, act, "RX 27 seed");
            }
            return TRUE;
        }
        if (m_unlocked)
        {
            if (!suppress)
            {
                BYTE resp[2];
                resp[0] = 0x67;
                resp[1] = sf;
                EmitResponse(resp, 2, ch, act, "RX 27 already unlocked");
            }
            return TRUE;
        }
        if (m_keyFails >= 3)
        {
            EmitNrc(0x27, 0x36, ch, act, "RX 27 (attempts exceeded)");
            return TRUE;
        }
        if (m_pendingLen <= 0)
        {
            EmitNrc(0x27, 0x24, ch, act, "RX 27 (seed was not requested)");
            return TRUE;
        }
        BYTE expect[256];
        UINT expectLen = 0;
        CString err;
        if (!ExpectedKey(expect, sizeof(expect), expectLen, err))
        {
            EmitNrc(0x27, 0x22, ch, act, "RX 27 (unlock algorithm not ready)");
            return TRUE;
        }
        int gotLen = len - 2;
        BOOL match = (gotLen == (int)expectLen && expectLen > 0 && memcmp(pdu + 2, expect, expectLen) == 0);
        if (!match)
        {
            m_keyFails++;
            EmitNrc(0x27, (m_keyFails >= 3) ? 0x36 : 0x35, ch, act, "RX 27 (invalid key)");
            return TRUE;
        }
        m_unlocked = TRUE;
        m_keyFails = 0;
        m_pendingLen = 0;
        if (!suppress)
        {
            BYTE resp[2];
            resp[0] = 0x67;
            resp[1] = sf;
            EmitResponse(resp, 2, ch, act, "RX 27 unlocked");
        }
        return TRUE;
    }

    BOOL HandleWrite(const BYTE* pdu, int len, unsigned char ch, OutAct& act)
    {
        int idLen = 0;
        int idx = FindWriteRow(pdu, len, &idLen);
        if (idx < 0)
        {
            return FALSE;
        }
        if (m_unlockWrite && !m_unlocked)
        {
            EmitNrc(0x2E, 0x33, ch, act, "RX 2E (security access denied)");
            return TRUE;
        }
        if (idLen > len - 1)
        {
            idLen = len - 1;
        }
        if (idLen < 0)
        {
            idLen = 0;
        }
        const BYTE* body = pdu + 1 + idLen;
        int bodyLen = len - 1 - idLen;
        if (!StoreWrite(idx, body, bodyLen))
        {
            EmitNrc(0x2E, 0x31, ch, act, "RX 2E (data does not fit)");
            return TRUE;
        }
        BYTE resp[16];
        resp[0] = 0x6E;
        int echo = idLen;
        if (echo > 14)
        {
            echo = 14;
        }
        if (echo > 0)
        {
            memcpy(resp + 1, pdu + 1, echo);
        }
        EmitResponse(resp, 1 + echo, ch, act, "RX 2E -> TX 6E");
        return TRUE;
    }

    void OnPduLocked(const BYTE* pdu, int len, unsigned char ch, OutAct& act)
    {
        m_activeGen = InterlockedIncrement(&m_txGen);
        m_txState = ST_IDLE;
        if (len >= 1 && pdu[0] == 0x10)
        {
            m_unlocked = FALSE;
            m_keyFails = 0;
            m_pendingLen = 0;
        }
        if (len >= 1 && pdu[0] == 0x27 && m_unlockWrite)
        {
            HandleSecurity(pdu, len, ch, act);
            return;
        }
        if (len >= 1 && pdu[0] == 0x2E && HandleWrite(pdu, len, ch, act))
        {
            return;
        }
        int idx = FindRow(pdu, len);
        if (idx < 0)
        {
            return;
        }
        const SvcRow& row = m_rows[idx];
        if (row.nrc < 0 && SidHasSub(pdu[0]) && len >= 2 && (pdu[1] & 0x80))
        {
            char rx[80];
            HexPreview(rx, sizeof(rx), pdu, len);
            _snprintf(act.log, sizeof(act.log), "RX %s (suppress positive response)", rx);
            act.log[sizeof(act.log) - 1] = 0;
            return;
        }
        const BYTE* fileBytes = NULL;
        int fileLen = 0;
        SvcRow reply = row;
        if (row.file[0] != 0 && row.nrc < 0)
        {
            if (m_cache[idx].len == -4)
            {
                EmitNrc(row.sid, 0x21, ch, act, "RX file is still being parsed");
                return;
            }
            if (m_cache[idx].len == -3)
            {
                EmitNrc(row.sid, 0x31, ch, act, "RX file is larger than 16 MB");
                return;
            }
            if (m_cache[idx].len < 0 || m_cache[idx].data == NULL)
            {
                EmitNrc(row.sid, 0x22, ch, act, "RX file was not parsed");
                return;
            }
            fileBytes = m_cache[idx].data;
            fileLen = m_cache[idx].len;
            reply.dataLen = 0;
            reply.file[0] = 0;
        }
        int keyLen = reply.matchLen;
        if (keyLen > len - 1)
        {
            keyLen = len - 1;
        }
        if (keyLen < 0)
        {
            keyLen = 0;
        }
        int rlen = 0;
        BYTE* resp = NULL;
        BOOL heap = FALSE;
        if (fileBytes != NULL)
        {
            BOOL full = (fileLen >= 1 && fileBytes[0] == (BYTE)(reply.sid + 0x40));
            rlen = full ? fileLen : (1 + keyLen + fileLen);
            if (rlen <= 0 || rlen > MAX_FILE)
            {
                EmitNrc(row.sid, 0x31, ch, act, "RX file too large");
                return;
            }
            resp = new BYTE[rlen];
            heap = TRUE;
            if (full)
            {
                memcpy(resp, fileBytes, fileLen);
            }
            else
            {
                resp[0] = (BYTE)(reply.sid + 0x40);
                for (int i = 0; i < keyLen; i++)
                {
                    BYTE b = pdu[1 + i];
                    if (i == 0 && SidHasSub(reply.sid))
                    {
                        b = (BYTE)(b & 0x7F);
                    }
                    resp[1 + i] = b;
                }
                if (fileLen > 0 && fileBytes != NULL)
                {
                    memcpy(resp + 1 + keyLen, fileBytes, fileLen);
                }
            }
        }
        else
        {
            BYTE stack[MAX_PDU];
            rlen = BuildResponse(reply, pdu, len, stack, MAX_PDU);
            if (rlen <= 0)
            {
                return;
            }
            resp = new BYTE[rlen];
            heap = TRUE;
            memcpy(resp, stack, rlen);
        }
        char rx[80];
        char tx[80];
        HexPreview(rx, sizeof(rx), pdu, len);
        HexPreview(tx, sizeof(tx), resp, rlen);
        char log[180];
        _snprintf(log, sizeof(log), "RX %s -> TX %s", rx, tx);
        log[sizeof(log) - 1] = 0;
        EmitResponse(resp, rlen, ch, act, log);
        if (heap)
        {
            delete[] resp;
        }
    }

    static int DecodeSt(BYTE s)
    {
        if (s <= 0x7F)
        {
            return (int)s;
        }
        if (s >= 0xF1 && s <= 0xF9)
        {
            return 1;
        }
        return 0;
    }

    void OnFlowLocked(const BYTE* d, int dlc, OutAct& act)
    {
        if (dlc < 3 || m_txLen <= m_txOff)
        {
            m_txState = ST_IDLE;
            return;
        }
        int fs = d[0] & 0x0F;
        if (fs == 2)
        {
            m_txState = ST_IDLE;
            _snprintf(act.log, sizeof(act.log), "ISO-TP aborted by tester");
            act.log[sizeof(act.log) - 1] = 0;
            return;
        }
        if (fs != 0)
        {
            return;
        }
        TxJob* job = new TxJob;
        memset(job, 0, sizeof(TxJob));
        job->gen = m_activeGen;
        job->len = m_txLen;
        job->data = new BYTE[m_txLen > 0 ? m_txLen : 1];
        memcpy(job->data, (m_txHold != NULL) ? m_txHold : m_tx, m_txLen);
        delete[] m_txHold;
        m_txHold = NULL;
        job->startOff = m_txOff;
        job->seq = m_txSeq;
        job->bs = d[1];
        job->stMs = DecodeSt(d[2]);
        job->id = m_respId;
        job->ext = m_ext ? 1 : 0;
        job->ch = m_txCh;
        m_txState = ST_SENDING;
        act.job = job;
    }

    void HandleLocked(const STCAN_MSG& msg, OutAct& act)
    {
        if (!m_loaded)
        {
            LoadUnlocked();
        }
        if (!m_enable || msg.m_ucRTR)
        {
            return;
        }
        int dlc = msg.m_ucDataLen;
        if (dlc <= 0)
        {
            return;
        }
        if (dlc > 64)
        {
            dlc = 64;
        }
        if (m_channel != 0 && msg.m_ucChannel != (unsigned char)m_channel)
        {
            return;
        }
        if (msg.m_unMsgID != m_reqId)
        {
            return;
        }
        const BYTE* d = msg.m_ucData;
        int type = (d[0] >> 4) & 0x0F;
        unsigned char ch = msg.m_ucChannel;
        if (ch == 0)
        {
            ch = 1;
        }
        if (type == 3 && m_txState == ST_WAIT_FC)
        {
            OnFlowLocked(d, dlc, act);
            return;
        }
        if (type == 0)
        {
            int len;
            int start;
            if ((d[0] & 0x0F) == 0 && dlc > 8)
            {
                len = d[1];
                start = 2;
            }
            else
            {
                len = d[0] & 0x0F;
                start = 1;
            }
            if (len <= 0 || start + len > dlc || len > MAX_PDU)
            {
                return;
            }
            m_rxState = RX_IDLE;
            BYTE pdu[MAX_PDU];
            memcpy(pdu, d + start, len);
            OnPduLocked(pdu, len, ch, act);
            return;
        }
        if (type == 1)
        {
            int len;
            int dataOff;
            int n;
            if (d[0] == 0x10 && d[1] == 0x00 && dlc >= 8)
            {
                len = ((int)d[2] << 24) | ((int)d[3] << 16) | ((int)d[4] << 8) | (int)d[5];
                dataOff = 6;
                n = 2;
            }
            else
            {
                len = ((d[0] & 0x0F) << 8) | d[1];
                dataOff = 2;
                n = 6;
            }
            if (len < 8 || len > MAX_XFER || dlc < 8)
            {
                return;
            }
            if (n > len)
            {
                n = len;
            }
            if (dataOff + n > dlc)
            {
                return;
            }
            memcpy(m_rx, d + dataOff, n);
            m_rxGot = n;
            m_rxExpect = len;
            m_rxSeq = 1;
            m_rxCh = ch;
            if (m_rxGot >= m_rxExpect)
            {
                m_rxState = RX_IDLE;
                OnPduLocked(m_rx, m_rxExpect, ch, act);
            }
            else
            {
                m_rxState = RX_MORE;
                unsigned char useCh = (m_channel == 0) ? ch : (unsigned char)m_channel;
                if (useCh == 0)
                {
                    useCh = 1;
                }
                memset(&act.frame, 0, sizeof(act.frame));
                act.frame.m_unMsgID = m_respId;
                act.frame.m_ucEXTENDED = m_ext ? 1 : 0;
                act.frame.m_ucChannel = useCh;
                int dlc = SizeFC;
                if (g_udsCanFd)
                {
                    dlc = g_udsFdDlc;
                    if (dlc < 8)
                    {
                        dlc = 8;
                    }
                    if (dlc > 64)
                    {
                        dlc = 64;
                    }
                    act.frame.m_bCANFD = true;
                }
                else if (dlc < 3 || dlc > 8)
                {
                    dlc = 8;
                }
                act.frame.m_ucDataLen = (unsigned char)dlc;
                act.frame.m_ucData[0] = 0x30;
                act.frame.m_ucData[1] = (BYTE)(BSize & 0xFF);
                act.frame.m_ucData[2] = (BYTE)(SSTMin & 0xFF);
                act.hasFrame = TRUE;
            }
            return;
        }
        if (type == 2)
        {
            if (m_rxState != RX_MORE)
            {
                return;
            }
            int seq = d[0] & 0x0F;
            if (seq != m_rxSeq)
            {
                m_rxState = RX_IDLE;
                _snprintf(act.log, sizeof(act.log), "ISO-TP sequence error");
                act.log[sizeof(act.log) - 1] = 0;
                return;
            }
            int remain = m_rxExpect - m_rxGot;
            int n = 7;
            if (n > remain)
            {
                n = remain;
            }
            if (n <= 0 || 1 + n > dlc)
            {
                m_rxState = RX_IDLE;
                return;
            }
            memcpy(m_rx + m_rxGot, d + 1, n);
            m_rxGot += n;
            m_rxSeq = (m_rxSeq + 1) & 0x0F;
            if (m_rxGot >= m_rxExpect)
            {
                int got = m_rxExpect;
                unsigned char rxCh = m_rxCh;
                m_rxState = RX_IDLE;
                OnPduLocked(m_rx, got, rxCh, act);
            }
        }
    }

    static void StartTxThread(TxJob* job);

    CCriticalSection m_cs;
    BOOL m_enable;
    int m_channel;
    UINT m_reqId;
    UINT m_respId;
    BOOL m_ext;
    SvcRow m_rows[MAX_ROWS];
    int m_nRows;
    BOOL m_loaded;
    int m_rxState;
    BYTE m_rx[MAX_XFER];
    int m_rxExpect;
    int m_rxGot;
    int m_rxSeq;
    unsigned char m_rxCh;
    int m_txState;
    BYTE m_tx[MAX_XFER];
    BYTE* m_txHold;
    BOOL m_unlockWrite;
    char m_dll[MAX_PATH];
    char m_variant[64];
    BYTE m_level;
    int m_seedAlgo;
    int m_keyAlgo;
    BYTE m_seed[16];
    int m_seedLen;
    BYTE m_key[16];
    int m_keyLen;
    BYTE m_pending[16];
    int m_pendingLen;
    unsigned int m_rng;
    BOOL m_unlocked;
    int m_keyFails;
    CVectorSeedKeyDll m_seedDll;
    struct FileCache
    {
        BYTE* data;
        int len;
        int fileBytes;
        LONG gen;
    };
    struct ParseJob
    {
        int index;
        LONG gen;
        char path[MAX_PATH];
        ParseJob* next;
    };
    FileCache m_cache[MAX_ROWS];
    ParseJob* m_parseHead;
    ParseJob* m_parseTail;
    LONG m_parseLeft;
    LONG m_parseStop;
    HANDLE m_parseSem;
    HANDLE m_parseThread;

    void FreeCacheAt(int index)
    {
        if (index < 0 || index >= MAX_ROWS)
        {
            return;
        }
        delete[] m_cache[index].data;
        m_cache[index].data = NULL;
        m_cache[index].len = 0;
        m_cache[index].fileBytes = 0;
        InterlockedIncrement(&m_cache[index].gen);
    }

    void FreeAllCache()
    {
        for (int i = 0; i < MAX_ROWS; i++)
        {
            FreeCacheAt(i);
        }
    }

    int LoadCacheAt(int index)
    {
        if (index < 0 || index >= m_nRows)
        {
            return -1;
        }
        FreeCacheAt(index);
        if (m_rows[index].file[0] == 0)
        {
            return 0;
        }
        long long sz = FileSizeOf(m_rows[index].file);
        if (sz < 0)
        {
            m_cache[index].len = -1;
            return -1;
        }
        m_cache[index].fileBytes = (sz > 0x7FFFFFFF) ? 0x7FFFFFFF : (int)sz;
        if (sz > MAX_FILE)
        {
            m_cache[index].len = -3;
            return -3;
        }
        if (sz == 0)
        {
            m_cache[index].len = -1;
            return -1;
        }
        m_cache[index].len = -4;
        QueueParse(index);
        return -4;
    }

public:
    int ParsePending()
    {
        return (int)m_parseLeft;
    }

private:

    void QueueParse(int index)
    {
        ParseJob* job = new ParseJob;
        memset(job, 0, sizeof(ParseJob));
        job->index = index;
        job->gen = m_cache[index].gen;
        strncpy_s(job->path, m_rows[index].file, _TRUNCATE);
        if (m_parseTail != NULL)
        {
            m_parseTail->next = job;
        }
        else
        {
            m_parseHead = job;
        }
        m_parseTail = job;
        InterlockedIncrement(&m_parseLeft);
        if (m_parseSem == NULL)
        {
            m_parseSem = CreateSemaphore(NULL, 0, 1024, NULL);
        }
        if (m_parseThread == NULL && m_parseSem != NULL)
        {
            m_parseThread = (HANDLE)_beginthreadex(NULL, 0, ParseThread, this, 0, NULL);
        }
        if (m_parseThread == NULL)
        {
            m_parseHead = job->next;
            if (m_parseHead == NULL)
            {
                m_parseTail = NULL;
            }
            delete job;
            InterlockedDecrement(&m_parseLeft);
            m_cache[index].len = -1;
            return;
        }
        ReleaseSemaphore(m_parseSem, 1, NULL);
    }

    static unsigned __stdcall ParseThread(void* param)
    {
        CUdsServerEngine* self = (CUdsServerEngine*)param;
        for (;;)
        {
            WaitForSingleObject(self->m_parseSem, INFINITE);
            if (self->m_parseStop)
            {
                return 0;
            }
            self->RunOneParse();
        }
    }

    void RunOneParse()
    {
        m_cs.Lock();
        ParseJob* job = m_parseHead;
        if (job != NULL)
        {
            m_parseHead = job->next;
            if (m_parseHead == NULL)
            {
                m_parseTail = NULL;
            }
        }
        m_cs.Unlock();
        if (job == NULL)
        {
            return;
        }
        BYTE* img = NULL;
        int n = 0;
        BOOL ok = LoadDataFile(job->path, &img, &n, MAX_FILE) && img != NULL && n > 0;
        m_cs.Lock();
        int slot = -1;
        for (int i = 0; i < m_nRows; i++)
        {
            if (m_cache[i].gen == job->gen)
            {
                slot = i;
                break;
            }
        }
        int result = -1;
        if (slot >= 0)
        {
            delete[] m_cache[slot].data;
            if (!ok)
            {
                delete[] img;
                m_cache[slot].data = NULL;
                m_cache[slot].len = -1;
            }
            else
            {
                m_cache[slot].data = img;
                m_cache[slot].len = n;
                img = NULL;
            }
            result = m_cache[slot].len;
        }
        else
        {
            delete[] img;
        }
        LONG left = InterlockedDecrement(&m_parseLeft);
        int index = (slot >= 0) ? slot : job->index;
        m_cs.Unlock();
        delete job;
        PostParse(index, result, left);
    }
    int m_txLen;
    int m_txOff;
    int m_txSeq;
    unsigned char m_txCh;
    LONG m_txGen;
    LONG m_activeGen;
    CBaseDIL_CAN* m_pDil;
    DWORD m_clientId;
};

static CUdsServerEngine& Engine()
{
    static CUdsServerEngine engine;
    return engine;
}

static unsigned __stdcall TxThread(void* param)
{
    TxJob* job = (TxJob*)param;
    int off = job->startOff;
    int seq = job->seq;
    int inBlock = 0;
    BOOL failed = FALSE;
    CUdsServerEngine& engine = Engine();
    while (off < job->len)
    {
        if (engine.CurrentGen() != job->gen)
        {
            break;
        }
        if (job->bs != 0 && inBlock >= job->bs)
        {
            engine.NoteBlockPause(job->gen, off, seq);
            FreeJob(job);
            return 0;
        }
        if (inBlock > 0 && job->stMs > 0)
        {
            Sleep((DWORD)job->stMs);
        }
        if (engine.CurrentGen() != job->gen)
        {
            break;
        }
        int cfMax = 7;
        int txDlc = 8;
        if (g_udsCanFd)
        {
            txDlc = g_udsFdDlc;
            if (txDlc < 8)
            {
                txDlc = 8;
            }
            if (txDlc > 64)
            {
                txDlc = 64;
            }
            cfMax = txDlc - 1;
        }
        int n = job->len - off;
        if (n > cfMax)
        {
            n = cfMax;
        }
        STCAN_MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.m_unMsgID = job->id;
        msg.m_ucEXTENDED = job->ext;
        msg.m_ucChannel = job->ch;
        msg.m_bCANFD = g_udsCanFd ? true : false;
        msg.m_ucDataLen = (unsigned char)txDlc;
        msg.m_ucData[0] = (BYTE)(0x20 | (seq & 0x0F));
        memcpy(&msg.m_ucData[1], job->data + off, n);
        if (engine.SendFrame(msg) != S_OK && !failed)
        {
            failed = TRUE;
            PostStatus("Diagnostic server send failed");
        }
        off += n;
        seq = (seq + 1) & 0x0F;
        inBlock++;
        if (job->stMs == 0 && (inBlock & 63) == 0)
        {
            Sleep(0);
        }
    }
    if (off >= job->len)
    {
        engine.NoteTxDone(job->gen);
    }
    FreeJob(job);
    return 0;
}

void CUdsServerEngine::StartTxThread(TxJob* job)
{
    uintptr_t h = _beginthreadex(NULL, 0, TxThread, job, 0, NULL);
    if (h == 0)
    {
        Engine().NoteTxDone(job->gen);
        FreeJob(job);
        return;
    }
    CloseHandle((HANDLE)h);
}

void UdsServer_OnCan(const STCAN_MSG& msg)
{
    Engine().OnCan(msg);
}

class CUDSServerWnd : public CDialog
{
public:
    CUDSServerWnd(CWnd* pParent) : CDialog(IDD_UDS_SERVER, pParent)
    {
        m_bReady = FALSE;
        m_bFill = FALSE;
        m_bFormatData = FALSE;
        m_matchCount = 0;
    }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();
    virtual void PostNcDestroy();
    afx_msg void OnDestroy();
    afx_msg void OnAdd();
    afx_msg void OnUpdate();
    afx_msg void OnDelete();
    afx_msg void OnExamples();
    afx_msg void OnLoadCfg();
    afx_msg void OnSaveCfg();
    afx_msg void OnDataChange();
    afx_msg void OnBrowseData();
    afx_msg void OnBrowseDll();
    afx_msg void OnSidChanged();
    afx_msg void OnHeaderChanged();
    afx_msg void OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnStatusMsg(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

    BOOL ApplyHeader();
    BOOL ReadRow(SvcRow& row);
    void LoadHeader();
    void FillList(int selectIndex);
    void LoadEditors(int index);
    void FillSidList();
    void SelectSid(BYTE sid);
    void FillMatchList(BYTE sid);
    void ShowMatch(const CString& hex);
    void FillUnlockLists();
    void ShowLevel(BYTE level);
    void RememberCfg(const CString& path);
    void SetParseButtons(BOOL enable);
    afx_msg LRESULT OnParseDone(WPARAM wParam, LPARAM lParam);

    CListCtrl m_list;
    CComboBox m_sid;
    CComboBox m_match;
    CComboBox m_level;
    CComboBox m_seedAlgo;
    CComboBox m_keyAlgo;
    CString m_matchHex[64];
    int m_matchCount;
    BOOL m_bReady;
    BOOL m_bFill;
    BOOL m_bFormatData;
    CString m_cfgPath;
};

static CUDSServerWnd* g_pWnd = NULL;

BEGIN_MESSAGE_MAP(CUDSServerWnd, CDialog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_SVR_ADD, OnAdd)
    ON_BN_CLICKED(IDC_SVR_UPD, OnUpdate)
    ON_BN_CLICKED(IDC_SVR_DEL, OnDelete)
    ON_BN_CLICKED(IDC_SVR_EXAMPLE, OnExamples)
    ON_BN_CLICKED(IDC_SVR_LOAD, OnLoadCfg)
    ON_BN_CLICKED(IDC_SVR_SAVE, OnSaveCfg)
    ON_EN_CHANGE(IDC_SVR_DATA, OnDataChange)
    ON_BN_CLICKED(IDC_SVR_BROWSE, OnBrowseData)
    ON_BN_CLICKED(IDC_SVR_DLLBROWSE, OnBrowseDll)
    ON_BN_CLICKED(IDC_SVR_ENABLE, OnHeaderChanged)
    ON_BN_CLICKED(IDC_SVR_UNLOCK, OnHeaderChanged)
    ON_BN_CLICKED(IDC_SVR_EXT, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_CH, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_REQ, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_RESP, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_DLL, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_VARIANT, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_SEED, OnHeaderChanged)
    ON_EN_KILLFOCUS(IDC_SVR_KEY, OnHeaderChanged)
    ON_CBN_SELCHANGE(IDC_SVR_SID, OnSidChanged)
    ON_CBN_SELCHANGE(IDC_SVR_LEVEL, OnHeaderChanged)
    ON_CBN_KILLFOCUS(IDC_SVR_LEVEL, OnHeaderChanged)
    ON_CBN_SELCHANGE(IDC_SVR_SEEDALGO, OnHeaderChanged)
    ON_CBN_SELCHANGE(IDC_SVR_KEYALGO, OnHeaderChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_SVR_LIST, OnItemChanged)
    ON_MESSAGE(WM_UDS_SERVER_STATUS, OnStatusMsg)
    ON_MESSAGE(WM_UDS_SERVER_PARSE, OnParseDone)
END_MESSAGE_MAP()

BOOL CUDSServerWnd::OnInitDialog()
{
    CDialog::OnInitDialog();
    Engine().EnsureLoaded();
    m_list.SubclassDlgItem(IDC_SVR_LIST, this);
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_list.InsertColumn(0, "On", LVCFMT_LEFT, 36);
    m_list.InsertColumn(1, "SID", LVCFMT_LEFT, 200);
    m_list.InsertColumn(2, "Match", LVCFMT_LEFT, 160);
    m_list.InsertColumn(3, "Data", LVCFMT_LEFT, 180);
    m_list.InsertColumn(4, "NRC", LVCFMT_LEFT, 40);
    m_list.InsertColumn(5, "W", LVCFMT_LEFT, 28);
    m_sid.SubclassDlgItem(IDC_SVR_SID, this);
    m_match.SubclassDlgItem(IDC_SVR_MATCH, this);
    m_match.LimitText(96);
    m_level.SubclassDlgItem(IDC_SVR_LEVEL, this);
    m_seedAlgo.SubclassDlgItem(IDC_SVR_SEEDALGO, this);
    m_keyAlgo.SubclassDlgItem(IDC_SVR_KEYALGO, this);
    m_level.LimitText(32);
    FillUnlockLists();
    FillSidList();
    ((CEdit*)GetDlgItem(IDC_SVR_DATA))->LimitText(16384);
    LoadHeader();
    FillList(-1);
    CheckDlgButton(IDC_SVR_ON, BST_CHECKED);
    g_hStatus = m_hWnd;
    if (Engine().RowCount() == 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "No services loaded. Use Load to import a configuration, or Examples for the sample.");
    }
    else
    {
        SetDlgItemText(IDC_SVR_STATUS, "Diagnostic server is idle");
    }
    m_bReady = TRUE;
    return TRUE;
}

void CUDSServerWnd::LoadHeader()
{
    BOOL enable = FALSE;
    BOOL ext = FALSE;
    int channel = 1;
    UINT req = 0x7E0;
    UINT resp = 0x7E8;
    Engine().GetHeader(enable, channel, req, resp, ext);
    CheckDlgButton(IDC_SVR_ENABLE, enable ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_SVR_EXT, ext ? BST_CHECKED : BST_UNCHECKED);
    CString text;
    text.Format("%d", channel);
    SetDlgItemText(IDC_SVR_CH, text);
    text.Format("%X", req);
    SetDlgItemText(IDC_SVR_REQ, text);
    text.Format("%X", resp);
    SetDlgItemText(IDC_SVR_RESP, text);
    BOOL unlock = FALSE;
    int level = 0x01;
    int seedAlgo = 0;
    int keyAlgo = 1;
    CString dll;
    CString variant;
    CString seed;
    CString key;
    Engine().GetSecurity(unlock, level, seedAlgo, keyAlgo, dll, variant, seed, key);
    CheckDlgButton(IDC_SVR_UNLOCK, unlock ? BST_CHECKED : BST_UNCHECKED);
    ShowLevel((BYTE)level);
    if (seedAlgo < 0 || seedAlgo >= m_seedAlgo.GetCount())
    {
        seedAlgo = 0;
    }
    if (keyAlgo < 0 || keyAlgo >= m_keyAlgo.GetCount())
    {
        keyAlgo = 0;
    }
    m_seedAlgo.SetCurSel(seedAlgo);
    m_keyAlgo.SetCurSel(keyAlgo);
    SetDlgItemText(IDC_SVR_DLL, dll);
    SetDlgItemText(IDC_SVR_VARIANT, variant);
    SetDlgItemText(IDC_SVR_SEED, seed);
    SetDlgItemText(IDC_SVR_KEY, key);
}

void CUDSServerWnd::FillList(int selectIndex)
{
    m_bFill = TRUE;
    m_list.DeleteAllItems();
    int n = Engine().RowCount();
    for (int i = 0; i < n; i++)
    {
        SvcRow row;
        if (!Engine().GetRow(i, row))
        {
            continue;
        }
        int item = m_list.InsertItem(i, row.on ? "Yes" : "No");
        CString text;
        m_list.SetItemText(item, 1, SidText(row.sid));
        m_list.SetItemText(item, 2, MatchLabel(row.sid, row.match, row.matchLen));
        if (row.file[0] != 0)
        {
            m_list.SetItemText(item, 3, row.file);
        }
        else
        {
            m_list.SetItemText(item, 3, HexSpaced(row.data, row.dataLen));
        }
        if (row.nrc < 0)
        {
            text = "";
        }
        else
        {
            text.Format("%02X", row.nrc);
        }
        m_list.SetItemText(item, 4, text);
        m_list.SetItemText(item, 5, row.writable ? "Yes" : "");
    }
    m_bFill = FALSE;
    if (selectIndex >= n)
    {
        selectIndex = n - 1;
    }
    if (selectIndex >= 0)
    {
        m_list.SetItemState(selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_list.EnsureVisible(selectIndex, FALSE);
    }
}

void CUDSServerWnd::LoadEditors(int index)
{
    SvcRow row;
    if (!Engine().GetRow(index, row))
    {
        return;
    }
    CheckDlgButton(IDC_SVR_ON, row.on ? BST_CHECKED : BST_UNCHECKED);
    SelectSid(row.sid);
    CString text;
    ShowMatch(HexSpaced(row.match, row.matchLen));
    if (row.file[0] != 0)
    {
        SetDlgItemText(IDC_SVR_DATA, row.file);
    }
    else
    {
        SetDlgItemText(IDC_SVR_DATA, HexSpaced(row.data, row.dataLen));
    }
    CheckDlgButton(IDC_SVR_WRITE, row.writable ? BST_CHECKED : BST_UNCHECKED);
    if (row.nrc < 0)
    {
        SetDlgItemText(IDC_SVR_NRC, "");
    }
    else
    {
        text.Format("%02X", row.nrc);
        SetDlgItemText(IDC_SVR_NRC, text);
    }
}

BOOL CUDSServerWnd::ApplyHeader()
{
    CString chText;
    CString reqText;
    CString respText;
    GetDlgItemText(IDC_SVR_CH, chText);
    GetDlgItemText(IDC_SVR_REQ, reqText);
    GetDlgItemText(IDC_SVR_RESP, respText);
    chText.Trim();
    int channel = 1;
    if (!chText.IsEmpty())
    {
        char* end = NULL;
        channel = (int)strtoul((LPCTSTR)chText, &end, 10);
        if (end == (char*)(LPCTSTR)chText || end == NULL || *end != 0 || channel < 0 || channel > 32)
        {
            SetDlgItemText(IDC_SVR_STATUS, "Channel must be 0..32");
            return FALSE;
        }
    }
    UINT req = 0;
    UINT resp = 0;
    if (!ParseId(reqText, req) || !ParseId(respText, resp))
    {
        SetDlgItemText(IDC_SVR_STATUS, "Request and response ID must be hex");
        return FALSE;
    }
    BOOL ext = (IsDlgButtonChecked(IDC_SVR_EXT) == BST_CHECKED);
    if (req > 0x7FF || resp > 0x7FF)
    {
        ext = TRUE;
        CheckDlgButton(IDC_SVR_EXT, BST_CHECKED);
    }
    BOOL enable = (IsDlgButtonChecked(IDC_SVR_ENABLE) == BST_CHECKED);
    CString dll;
    CString variant;
    CString seedText;
    CString keyText;
    GetDlgItemText(IDC_SVR_DLL, dll);
    GetDlgItemText(IDC_SVR_VARIANT, variant);
    GetDlgItemText(IDC_SVR_SEED, seedText);
    GetDlgItemText(IDC_SVR_KEY, keyText);
    dll.Trim();
    variant.Trim();
    seedText = Strip0x(seedText);
    keyText = Strip0x(keyText);
    BYTE seed[16];
    BYTE key[16];
    int seedLen = 0;
    int keyLen = 0;
    if (!seedText.IsEmpty())
    {
        seedLen = ParseHex((LPCTSTR)seedText, seed, 16);
        if (seedLen < 0)
        {
            SetDlgItemText(IDC_SVR_STATUS, "Seed must be hex bytes");
            return FALSE;
        }
    }
    if (!keyText.IsEmpty())
    {
        keyLen = ParseHex((LPCTSTR)keyText, key, 16);
        if (keyLen < 0)
        {
            SetDlgItemText(IDC_SVR_STATUS, "Key must be hex bytes");
            return FALSE;
        }
    }
    BOOL unlock = (IsDlgButtonChecked(IDC_SVR_UNLOCK) == BST_CHECKED);
    CString levelText;
    m_level.GetWindowText(levelText);
    BYTE levelBytes[4];
    int levelN = ParseMatchText((LPCTSTR)levelText, levelBytes, 4);
    int level = 0x01;
    if (levelText.Trim().IsEmpty())
    {
        level = 0x01;
    }
    else if (levelN != 1 || (levelBytes[0] & 1) == 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Level must be an odd requestSeed sub-function, such as 01 or 03");
        return FALSE;
    }
    else
    {
        level = levelBytes[0];
    }
    int seedAlgo = m_seedAlgo.GetCurSel();
    int keyAlgo = m_keyAlgo.GetCurSel();
    if (seedAlgo < 0)
    {
        seedAlgo = 0;
    }
    if (keyAlgo < 0)
    {
        keyAlgo = 1;
    }
    Engine().SetHeader(enable, channel, req, resp, ext);
    Engine().SetSecurity(unlock, level, seedAlgo, keyAlgo, (LPCTSTR)dll, (LPCTSTR)variant, seed, seedLen, key, keyLen);
    return TRUE;
}

BOOL CUDSServerWnd::ReadRow(SvcRow& row)
{
    memset(&row, 0, sizeof(row));
    row.nrc = -1;
    row.on = (IsDlgButtonChecked(IDC_SVR_ON) == BST_CHECKED);
    row.writable = (IsDlgButtonChecked(IDC_SVR_WRITE) == BST_CHECKED);
    int sidSel = m_sid.GetCurSel();
    if (sidSel < 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Select a service");
        return FALSE;
    }
    BYTE sid = (BYTE)m_sid.GetItemData(sidSel);
    CString matchText;
    CString dataText;
    CString nrcText;
    GetDlgItemText(IDC_SVR_MATCH, matchText);
    GetDlgItemText(IDC_SVR_DATA, dataText);
    GetDlgItemText(IDC_SVR_NRC, nrcText);
    dataText.Trim();
    nrcText = Strip0x(nrcText);
    int ml = ParseMatchText((LPCTSTR)matchText, row.match, MAX_MATCH);
    if (ml < 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Match must be hex bytes");
        return FALSE;
    }
    int dl = 0;
    BOOL asFile = FALSE;
    if (!dataText.IsEmpty())
    {
        if (IsDataFileExt((LPCTSTR)dataText) || dataText.Find('\\') >= 0 || dataText.Find('/') >= 0)
        {
            asFile = TRUE;
        }
        else
        {
            CString hex = Strip0x(dataText);
            dl = ParseHex((LPCTSTR)hex, row.data, MAX_DATA);
            if (dl < 0)
            {
                SetDlgItemText(IDC_SVR_STATUS, "Data must be hex bytes or a file path");
                return FALSE;
            }
        }
    }
    if (!nrcText.IsEmpty())
    {
        BYTE nrc = 0;
        if (ParseHex((LPCTSTR)nrcText, &nrc, 1) != 1)
        {
            SetDlgItemText(IDC_SVR_STATUS, "NRC must be one hex byte, or empty");
            return FALSE;
        }
        row.nrc = nrc;
    }
    if (!asFile && 1 + ml + dl > MAX_PDU)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Response is too long");
        return FALSE;
    }
    row.sid = sid;
    row.matchLen = ml;
    row.dataLen = asFile ? 0 : dl;
    if (asFile)
    {
        strncpy_s(row.file, (LPCTSTR)dataText, _TRUNCATE);
    }
    return TRUE;
}

void CUDSServerWnd::FillSidList()
{
    m_sid.ResetContent();
    int select = 0;
    for (int i = 0; i < (int)(sizeof(kIso14229) / sizeof(kIso14229[0])); i++)
    {
        int idx = m_sid.AddString(SidText(kIso14229[i].sid));
        m_sid.SetItemData(idx, kIso14229[i].sid);
        if (kIso14229[i].sid == 0x22)
        {
            select = idx;
        }
    }
    m_sid.SetCurSel(select);
    m_sid.SetDroppedWidth(260);
    OnSidChanged();
}

void CUDSServerWnd::FillMatchList(BYTE sid)
{
    m_match.ResetContent();
    m_matchCount = 0;
    for (int i = 0; i < (int)(sizeof(kMatchOpt) / sizeof(kMatchOpt[0])); i++)
    {
        if (kMatchOpt[i].sid != sid || m_matchCount >= 64)
        {
            continue;
        }
        CString label;
        if (kMatchOpt[i].hex[0] == 0)
        {
            label = kMatchOpt[i].name;
        }
        else
        {
            label.Format("%s  %s", kMatchOpt[i].hex, kMatchOpt[i].name);
        }
        int idx = m_match.AddString(label);
        m_matchHex[m_matchCount] = kMatchOpt[i].hex;
        m_match.SetItemData(idx, (DWORD_PTR)m_matchCount);
        m_matchCount++;
    }
    m_match.SetDroppedWidth(280);
    if (m_match.GetCount() > 0)
    {
        m_match.SetCurSel(0);
    }
}

void CUDSServerWnd::ShowMatch(const CString& hex)
{
    CString want = CompactHex((LPCTSTR)hex);
    for (int i = 0; i < m_match.GetCount(); i++)
    {
        DWORD_PTR ix = m_match.GetItemData(i);
        if (ix >= (DWORD_PTR)m_matchCount)
        {
            continue;
        }
        if (CompactHex((LPCTSTR)m_matchHex[ix]) == want && !want.IsEmpty())
        {
            m_match.SetCurSel((int)i);
            return;
        }
    }
    m_match.SetWindowText(hex);
}

void CUDSServerWnd::OnSidChanged()
{
    int sel = m_sid.GetCurSel();
    if (sel < 0 || m_match.GetSafeHwnd() == NULL)
    {
        return;
    }
    FillMatchList((BYTE)m_sid.GetItemData(sel));
}

void CUDSServerWnd::SelectSid(BYTE sid)
{
    int n = m_sid.GetCount();
    for (int i = 0; i < n; i++)
    {
        if ((BYTE)m_sid.GetItemData(i) == sid)
        {
            m_sid.SetCurSel(i);
            OnSidChanged();
            return;
        }
    }
    int idx = m_sid.AddString(SidText(sid));
    m_sid.SetItemData(idx, sid);
    m_sid.SetCurSel(idx);
    OnSidChanged();
}

void CUDSServerWnd::OnBrowseData()
{
    CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    "Data files (*.bin;*.hex;*.mot;*.s19;*.s28;*.s37;*.srec;*.txt)|*.bin;*.hex;*.mot;*.s19;*.s28;*.s37;*.srec;*.txt|All files (*.*)|*.*||",
                    this);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }
    SetDlgItemText(IDC_SVR_DATA, dlg.GetPathName());
}

void CUDSServerWnd::OnBrowseDll()
{
    CFileDialog dlg(TRUE, "dll", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    "Seed&Key DLL (*.dll)|*.dll|All files (*.*)|*.*||", this);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }
    SetDlgItemText(IDC_SVR_DLL, dlg.GetPathName());
    if (m_bReady)
    {
        ApplyHeader();
    }
}

void CUDSServerWnd::OnHeaderChanged()
{
    if (!m_bReady)
    {
        return;
    }
    ApplyHeader();
}

void CUDSServerWnd::FillUnlockLists()
{
    m_level.ResetContent();
    static const BYTE kLevels[] = { 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x11, 0x21, 0x41 };
    static const char* kLevelName[] = {
        "level 1", "level 2", "level 3", "level 4", "level 5",
        "level 6", "level 7", "level 9", "level 17", "level 33"
    };
    for (int i = 0; i < 10; i++)
    {
        CString text;
        text.Format("%02X  %s", kLevels[i], kLevelName[i]);
        m_level.AddString(text);
    }
    m_level.SetDroppedWidth(150);
    m_level.SetWindowText("01");
    m_seedAlgo.ResetContent();
    m_seedAlgo.AddString("Fixed");
    m_seedAlgo.AddString("Random");
    m_seedAlgo.AddString("Increment");
    m_seedAlgo.SetCurSel(0);
    m_keyAlgo.ResetContent();
    m_keyAlgo.AddString("DLL");
    m_keyAlgo.AddString("Fixed key");
    m_keyAlgo.AddString("XOR");
    m_keyAlgo.AddString("NOT");
    m_keyAlgo.AddString("ADD");
    m_keyAlgo.AddString("Swap");
    m_keyAlgo.SetCurSel(1);
}

void CUDSServerWnd::ShowLevel(BYTE level)
{
    CString hex;
    hex.Format("%02X", level);
    int n = m_level.GetCount();
    for (int i = 0; i < n; i++)
    {
        CString text;
        m_level.GetLBText(i, text);
        BYTE b[4];
        if (ParseMatchText((LPCTSTR)text, b, 4) == 1 && b[0] == level)
        {
            m_level.SetCurSel(i);
            return;
        }
    }
    m_level.SetWindowText(hex);
}

static BOOL ConfiguredDataExceedsCan(const SvcRow& row)
{
    if (g_udsCanFd)
    {
        return FALSE;
    }
    if (row.file[0] == 0)
    {
        return row.dataLen > 4095;
    }
    long long sz = FileSizeOf(row.file);
    if (sz < 0)
    {
        return FALSE;
    }
    if (ExtEq(row.file, "bin"))
    {
        return sz > 4095;
    }
    if (sz > (long long)4096 * 4)
    {
        return TRUE;
    }
    BYTE* img = NULL;
    int n = 0;
    BOOL ok = LoadDataFile(row.file, &img, &n, 4096);
    delete[] img;
    return ok && n > 4095;
}

static void SetFileStatus(const char* action, int index, const SvcRow& row)
{
    CString status = action;
    if (row.file[0] != 0)
    {
        int n = Engine().CachedLength(index);
        int sz = Engine().CachedFileBytes(index);
        if (n == -4)
        {
            status.Format("%s, parsing file (%d KB)...", action, (sz + 1023) / 1024);
        }
        else if (n == -3)
        {
            status.Format("%s, file is larger than 16 MB and was not loaded", action);
        }
        else if (n < 0)
        {
            status.Format("%s, but the file could not be parsed", action);
        }
        else
        {
            status.Format("%s, file parsed (%d bytes)", action, n);
        }
    }
    if (g_hStatus != NULL && ::IsWindow(g_hStatus))
    {
        ::SetDlgItemText(g_hStatus, IDC_SVR_STATUS, status);
    }
}

void CUDSServerWnd::SetParseButtons(BOOL enable)
{
    CWnd* add = GetDlgItem(IDC_SVR_ADD);
    CWnd* upd = GetDlgItem(IDC_SVR_UPD);
    if (add != NULL)
    {
        add->EnableWindow(enable);
    }
    if (upd != NULL)
    {
        upd->EnableWindow(enable);
    }
}

void CUDSServerWnd::OnAdd()
{
    if (!ApplyHeader())
    {
        return;
    }
    SvcRow row;
    if (!ReadRow(row))
    {
        return;
    }
    if (ConfiguredDataExceedsCan(row))
    {
        SetDlgItemText(IDC_SVR_STATUS, "CAN data cannot exceed 4095 bytes");
        return;
    }
    if (row.file[0] != 0)
    {
        SetParseButtons(FALSE);
        SetDlgItemText(IDC_SVR_STATUS, "Parsing file...");
    }
    if (!Engine().AddRow(row))
    {
        if (Engine().ParsePending() == 0)
        {
            SetParseButtons(TRUE);
        }
        SetDlgItemText(IDC_SVR_STATUS, "The service list is full (64)");
        return;
    }
    int added = Engine().RowCount() - 1;
    FillList(added);
    if (Engine().CachedLength(added) != -4)
    {
        if (Engine().ParsePending() == 0)
        {
            SetParseButtons(TRUE);
        }
        SetFileStatus("Service added", added, row);
    }
}

void CUDSServerWnd::OnUpdate()
{
    int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Select a service first");
        return;
    }
    if (!ApplyHeader())
    {
        return;
    }
    SvcRow row;
    if (!ReadRow(row))
    {
        return;
    }
    if (ConfiguredDataExceedsCan(row))
    {
        SetDlgItemText(IDC_SVR_STATUS, "CAN data cannot exceed 4095 bytes");
        return;
    }
    if (row.file[0] != 0)
    {
        SetParseButtons(FALSE);
        SetDlgItemText(IDC_SVR_STATUS, "Parsing file...");
    }
    if (!Engine().UpdateRow(sel, row))
    {
        if (Engine().ParsePending() == 0)
        {
            SetParseButtons(TRUE);
        }
        return;
    }
    FillList(sel);
    if (Engine().CachedLength(sel) != -4)
    {
        if (Engine().ParsePending() == 0)
        {
            SetParseButtons(TRUE);
        }
        SetFileStatus("Service updated", sel, row);
    }
}

void CUDSServerWnd::OnDelete()
{
    int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
    {
        SetDlgItemText(IDC_SVR_STATUS, "Select a service first");
        return;
    }
    Engine().DeleteRow(sel);
    FillList(sel);
    SetDlgItemText(IDC_SVR_STATUS, "Service deleted");
}

void CUDSServerWnd::OnExamples()
{
    Engine().AddExamples();
    m_bReady = FALSE;
    LoadHeader();
    m_bReady = TRUE;
    FillList(0);
    SetDlgItemText(IDC_SVR_STATUS, "Example services are ready and the server is enabled");
}

static void SplitConfigDir(const char* full, char* dir, int dirCap)
{
    dir[0] = 0;
    if (full == NULL || full[0] == 0 || dirCap <= 1)
    {
        return;
    }
    strncpy_s(dir, dirCap, full, _TRUNCATE);
    char* slash = strrchr(dir, '\\');
    if (slash != NULL)
    {
        *slash = 0;
    }
}

void CUDSServerWnd::OnLoadCfg()
{
    char defPath[MAX_PATH];
    char dir[MAX_PATH];
    Engine().DefaultConfigPath(defPath, MAX_PATH);
    SplitConfigDir(defPath, dir, MAX_PATH);
    CFileDialog dlg(TRUE, "cfg", "UdsServer.cfg",
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    "Server config (*.cfg)|*.cfg|All files (*.*)|*.*||", this);
    if (dir[0] != 0)
    {
        dlg.m_ofn.lpstrInitialDir = dir;
    }
    if (defPath[0] != 0 && dlg.m_ofn.lpstrFile != NULL && dlg.m_ofn.nMaxFile > 0)
    {
        strncpy_s(dlg.m_ofn.lpstrFile, dlg.m_ofn.nMaxFile, defPath, _TRUNCATE);
    }
    if (dlg.DoModal() != IDOK)
    {
        return;
    }
    CString path = dlg.GetPathName();
    if (!Engine().LoadFile((LPCTSTR)path))
    {
        SetDlgItemText(IDC_SVR_STATUS, "Could not load the configuration");
        return;
    }
    m_bReady = FALSE;
    LoadHeader();
    m_bReady = TRUE;
    FillList(Engine().RowCount() > 0 ? 0 : -1);
    RememberCfg(path);
    CString status;
    if (Engine().ParsePending() > 0)
    {
        SetParseButtons(FALSE);
        status.Format("Loaded %s, parsing files...", (LPCTSTR)path);
    }
    else
    {
        status.Format("Loaded %s", (LPCTSTR)path);
    }
    SetDlgItemText(IDC_SVR_STATUS, status);
}

void CUDSServerWnd::RememberCfg(const CString& path)
{
    m_cfgPath = path;
    CString title = "Diagnostic Server";
    int slash = path.ReverseFind('\\');
    CString name = (slash >= 0) ? path.Mid(slash + 1) : path;
    if (!name.IsEmpty())
    {
        title += " - ";
        title += name;
    }
    SetWindowText(title);
}

void CUDSServerWnd::OnDataChange()
{
    if (m_bFormatData)
    {
        return;
    }
    CEdit* edit = (CEdit*)GetDlgItem(IDC_SVR_DATA);
    if (edit == NULL)
    {
        return;
    }
    CString text;
    edit->GetWindowText(text);
    if (text.Find('\\') >= 0 || text.Find('/') >= 0 || text.Find(':') >= 0 || text.Find('.') >= 0)
    {
        return;
    }
    int selStart = 0;
    int selEnd = 0;
    edit->GetSel(selStart, selEnd);
    int digitsBefore = 0;
    CString compact;
    for (int i = 0; i < text.GetLength(); i++)
    {
        char c = (char)text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            continue;
        }
        if (c >= 'a' && c <= 'f')
        {
            c = (char)(c - 'a' + 'A');
        }
        BOOL hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if (!hex)
        {
            return;
        }
        if (i < selStart)
        {
            digitsBefore++;
        }
        compact += c;
    }
    CString formatted;
    for (int i = 0; i < compact.GetLength(); i++)
    {
        if (i > 0 && (i % 2) == 0)
        {
            formatted += ' ';
        }
        formatted += compact[i];
    }
    if (formatted == text)
    {
        return;
    }
    m_bFormatData = TRUE;
    edit->SetWindowText(formatted);
    int pos = 0;
    int seen = 0;
    while (pos < formatted.GetLength() && seen < digitsBefore)
    {
        char c = (char)formatted[pos];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
        {
            seen++;
        }
        pos++;
    }
    if (digitsBefore > 0 && (digitsBefore % 2) == 0 && pos < formatted.GetLength() && formatted[pos] == ' ')
    {
        pos++;
    }
    edit->SetSel(pos, pos);
    m_bFormatData = FALSE;
}

void CUDSServerWnd::OnSaveCfg()
{
    if (!ApplyHeader())
    {
        return;
    }
    CString path = m_cfgPath;
    if (path.IsEmpty())
    {
        char defPath[MAX_PATH];
        char dir[MAX_PATH];
        Engine().DefaultConfigPath(defPath, MAX_PATH);
        SplitConfigDir(defPath, dir, MAX_PATH);
        CFileDialog dlg(FALSE, "cfg", "UdsServer.cfg",
                        OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                        "Server config (*.cfg)|*.cfg|All files (*.*)|*.*||", this);
        if (dir[0] != 0)
        {
            dlg.m_ofn.lpstrInitialDir = dir;
        }
        if (defPath[0] != 0 && dlg.m_ofn.lpstrFile != NULL && dlg.m_ofn.nMaxFile > 0)
        {
            strncpy_s(dlg.m_ofn.lpstrFile, dlg.m_ofn.nMaxFile, defPath, _TRUNCATE);
        }
        if (dlg.DoModal() != IDOK)
        {
            return;
        }
        path = dlg.GetPathName();
    }
    if (!Engine().SaveFile((LPCTSTR)path))
    {
        SetDlgItemText(IDC_SVR_STATUS, "Could not save the configuration");
        return;
    }
    RememberCfg(path);
    CString status;
    status.Format("Saved %s", (LPCTSTR)path);
    SetDlgItemText(IDC_SVR_STATUS, status);
}

void CUDSServerWnd::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    if (!m_bReady || m_bFill)
    {
        return;
    }
    NMLISTVIEW* pView = (NMLISTVIEW*)pNMHDR;
    if ((pView->uChanged & LVIF_STATE) == 0)
    {
        return;
    }
    if ((pView->uNewState & LVIS_SELECTED) == 0)
    {
        return;
    }
    LoadEditors(pView->iItem);
}

LRESULT CUDSServerWnd::OnParseDone(WPARAM /*wParam*/, LPARAM lParam)
{
    ParseNote* note = (ParseNote*)lParam;
    if (note == NULL)
    {
        return 0;
    }
    if (note->left > 0)
    {
        SetParseButtons(FALSE);
        SetDlgItemText(IDC_SVR_STATUS, "Parsing file...");
    }
    else
    {
        SetParseButtons(TRUE);
        SvcRow row;
        if (Engine().GetRow(note->index, row))
        {
            SetFileStatus("Ready", note->index, row);
        }
        else
        {
            SetDlgItemText(IDC_SVR_STATUS, "File parsed");
        }
    }
    free(note);
    return 0;
}

LRESULT CUDSServerWnd::OnStatusMsg(WPARAM /*wParam*/, LPARAM lParam)
{
    char* text = (char*)lParam;
    if (text != NULL)
    {
        SetDlgItemText(IDC_SVR_STATUS, text);
        free(text);
    }
    return 0;
}

void CUDSServerWnd::OnDestroy()
{
    if (m_bReady)
    {
        ApplyHeader();
    }
    if (g_hStatus == m_hWnd)
    {
        g_hStatus = NULL;
    }
    CDialog::OnDestroy();
}

void CUDSServerWnd::OnOK()
{
    DestroyWindow();
}

void CUDSServerWnd::OnCancel()
{
    DestroyWindow();
}

void CUDSServerWnd::PostNcDestroy()
{
    if (g_pWnd == this)
    {
        g_pWnd = NULL;
    }
    CDialog::PostNcDestroy();
    delete this;
}

HRESULT UdsServer_Show(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (g_pWnd != NULL && ::IsWindow(g_pWnd->GetSafeHwnd()))
    {
        g_pWnd->ShowWindow(SW_SHOW);
        g_pWnd->SetForegroundWindow();
        return S_OK;
    }
    CWnd* pParent = CWnd::FromHandle(hParent);
    g_pWnd = new CUDSServerWnd(pParent);
    if (!g_pWnd->Create(IDD_UDS_SERVER, pParent))
    {
        delete g_pWnd;
        g_pWnd = NULL;
        return E_FAIL;
    }
    g_pWnd->ShowWindow(SW_SHOW);
    g_pWnd->SetForegroundWindow();
    return S_OK;
}
