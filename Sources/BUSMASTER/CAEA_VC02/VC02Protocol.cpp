#include "VC02Protocol.h"

#include <setupapi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "setupapi.lib")

static const int VC02_BAUD = 500000;
static const int VC02_MAX_SESSIONS = 8;
static const int VC02_QUEUE = 4096;
static const int VC02_PARSE_CAP = 65536;
static const int VC02_MAX_FRAME = 512;
/* CAN TX flow control. The device has no TX ack/busy reply and silently drops
   frames once its TX buffer is full (about 75 classic frames measured at
   1 Mbit/s). It does report every frame it transmitted (cmd 1, dir 0x10), so
   vc02_tx_can keeps at most VC02_TX_WINDOW frames unreported per channel.
   Flow control starts once the first report is seen on that channel. If no
   report arrives for VC02_TX_STALL_MS the window is resynchronised. */
static const int VC02_TX_WINDOW = 32;
static const DWORD VC02_TX_STALL_MS = 50;
static const int VC02_TX_MAX_CH = 8;

static char g_lastError[256] = "";
static CRITICAL_SECTION g_lock;
static LONG g_lockInit = 0;

static void SetError(const char* text)
{
    if (text == nullptr)
    {
        g_lastError[0] = '\0';
        return;
    }
    strncpy_s(g_lastError, text, _TRUNCATE);
}

static void EnsureLock(void)
{
    if (InterlockedCompareExchange(&g_lockInit, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_lock);
    }
}

static unsigned char FrameChecksum(const unsigned char* data, int len)
{
    unsigned int sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += data[i];
    }
    while (sum > 255)
    {
        sum -= 255;
    }
    return (unsigned char)(~sum);
}

struct VC02_SESSION
{
    HANDLE port;
    HANDLE thread;
    HANDLE response;
    CRITICAL_SECTION cs;
    int com;
    int ref;
    int stop;
    int configuring;
    int canCount;
    int linCount;
    int family;
    char version[64];
    unsigned char parse[VC02_PARSE_CAP];
    int parseLen;
    VC02_MSG canQueue[VC02_QUEUE];
    int canHead;
    int canTail;
    VC02_MSG linQueue[VC02_QUEUE];
    int linHead;
    int linTail;
    volatile LONG txSubmitted[VC02_TX_MAX_CH + 1];
    volatile LONG txReported[VC02_TX_MAX_CH + 1];
    volatile LONG txSeen[VC02_TX_MAX_CH + 1];
};

static VC02_SESSION* g_sessions[VC02_MAX_SESSIONS];

static int WriteRaw(VC02_SESSION* session, const unsigned char* data, int len)
{
    DWORD written = 0;
    if (session == nullptr || session->port == INVALID_HANDLE_VALUE || data == nullptr || len <= 0)
    {
        return 0;
    }
    EnterCriticalSection(&session->cs);
    BOOL ok = WriteFile(session->port, data, (DWORD)len, &written, nullptr);
    LeaveCriticalSection(&session->cs);
    return ok && written == (DWORD)len;
}

static int SendPayload(VC02_SESSION* session, const unsigned char* payload, int count)
{
    unsigned char frame[VC02_MAX_FRAME];
    int length = count + 4;
    if (count < 0 || length > VC02_MAX_FRAME)
    {
        return 0;
    }
    frame[0] = 0xAA;
    frame[1] = (unsigned char)(length >> 8);
    frame[2] = (unsigned char)length;
    if (count > 0)
    {
        memcpy(frame + 3, payload, count);
    }
    frame[length - 1] = FrameChecksum(frame, length - 1);
    return WriteRaw(session, frame, length);
}

static int SendAndWait(VC02_SESSION* session, const unsigned char* payload, int count, DWORD timeoutMs)
{
    ResetEvent(session->response);
    if (!SendPayload(session, payload, count))
    {
        return 0;
    }
    return WaitForSingleObject(session->response, timeoutMs) == WAIT_OBJECT_0;
}

static void Enqueue(VC02_SESSION* session, const VC02_MSG& msg)
{
    EnterCriticalSection(&session->cs);
    if (msg.kind == VC02_KIND_LIN)
    {
        int next = (session->linHead + 1) % VC02_QUEUE;
        if (next != session->linTail)
        {
            session->linQueue[session->linHead] = msg;
            session->linHead = next;
        }
    }
    else
    {
        int next = (session->canHead + 1) % VC02_QUEUE;
        if (next != session->canTail)
        {
            session->canQueue[session->canHead] = msg;
            session->canHead = next;
        }
    }
    LeaveCriticalSection(&session->cs);
}

static unsigned int ReadBe32(const unsigned char* data)
{
    return ((unsigned int)data[0] << 24) | ((unsigned int)data[1] << 16) |
           ((unsigned int)data[2] << 8) | data[3];
}

static int SendFixed(VC02_SESSION* session, unsigned char* frame)
{
    frame[22] = FrameChecksum(frame, 22);
    return WriteRaw(session, frame, 23);
}

static void DecodeVc01Version(VC02_SESSION* session, const unsigned char* frame)
{
    session->version[0] = '\0';
    if (frame[5] == 'V' && frame[6] == 'C' && frame[7] == '0' && frame[8] == '2')
    {
        sprintf_s(session->version, "VC02-%u", (unsigned)frame[9]);
        return;
    }
    if (frame[5] != 0 || frame[6] != 0 || frame[7] != 0 || frame[8] != 0)
    {
        return;
    }
    if (frame[9] == 1 && frame[10] == 1)
    {
        strcpy_s(session->version, "VC01-1");
    }
    else if (frame[9] == 1 && frame[10] == 2)
    {
        strcpy_s(session->version, "VC01-2");
    }
    else if (frame[9] == 2 && frame[10] == 1)
    {
        strcpy_s(session->version, "VC02-1");
    }
}

static unsigned int ReadVc01Id(const unsigned char* frame)
{
    return ((unsigned int)(frame[4] & 0x1F) << 24) |
           ((unsigned int)frame[5] << 16) |
           ((unsigned int)frame[6] << 8) |
           frame[7];
}

static double ReadVc01Time(const unsigned char* frame)
{
    unsigned int usec = ((unsigned int)frame[18] << 24) |
                        ((unsigned int)frame[19] << 16) |
                        ((unsigned int)frame[20] << 8) |
                        frame[21];
    if (usec == 0)
    {
        return 0.0;
    }
    return (double)frame[17] * 3600.0 + (double)usec / 1000000.0;
}

static void HandleVc01Frame(VC02_SESSION* session, const unsigned char* frame)
{
    if (frame[1] == 1 && frame[2] == 0x40 && frame[3] == 0x46)
    {
        DecodeVc01Version(session, frame);
        SetEvent(session->response);
        return;
    }
    if (frame[1] == 1)
    {
        SetEvent(session->response);
        return;
    }
    if (frame[1] != 2 && frame[1] != 3)
    {
        return;
    }
    double timeSec = ReadVc01Time(frame);
    if (timeSec == 0.0)
    {
        return;
    }
    VC02_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.timeSec = timeSec;
    msg.isTx = ((frame[3] & 0x30) == 0x10) ? 1 : 0;
    msg.isErr = (frame[3] & 0x01) ? 1 : 0;
    msg.dlc = frame[8];
    if (msg.dlc < 0)
    {
        msg.dlc = 0;
    }
    if (msg.dlc > 8)
    {
        msg.dlc = 8;
    }
    if (frame[1] == 2)
    {
        if (frame[2] != 1 && frame[2] != 2)
        {
            return;
        }
        msg.kind = VC02_KIND_CAN;
        msg.channel = frame[2];
        msg.id = ReadVc01Id(frame);
        msg.isExt = msg.id > 2047 ? 1 : 0;
    }
    else
    {
        if (frame[2] == 17)
        {
            msg.channel = 1;
        }
        else if (frame[2] == 18)
        {
            msg.channel = 2;
        }
        else
        {
            return;
        }
        msg.kind = VC02_KIND_LIN;
        msg.id = frame[7] & 0x3F;
        if (msg.isErr)
        {
            msg.errCode = frame[4];
        }
    }
    if (msg.dlc > 0)
    {
        memcpy(msg.data, frame + 9, msg.dlc);
    }
    Enqueue(session, msg);
}

static void ConsumeVc01(VC02_SESSION* session)
{
    int index = 0;
    int dropTo = 0;
    while (index + 23 <= session->parseLen)
    {
        if (session->parse[index] != 0xAA)
        {
            index++;
            continue;
        }
        if (FrameChecksum(session->parse + index, 22) != session->parse[index + 22])
        {
            index++;
            continue;
        }
        HandleVc01Frame(session, session->parse + index);
        index += 23;
        dropTo = index;
    }
    if (dropTo > 0)
    {
        memmove(session->parse, session->parse + dropTo, session->parseLen - dropTo);
        session->parseLen -= dropTo;
    }
    else if (session->parseLen > VC02_PARSE_CAP - 1024)
    {
        session->parseLen = 0;
    }
}

static double ReadTime(const unsigned char* frame, int length)
{
    if (length < 7)
    {
        return 0;
    }
    unsigned char hour = frame[length - 6];
    unsigned int usec = ((unsigned int)frame[length - 5] << 24) |
                        ((unsigned int)frame[length - 4] << 16) |
                        ((unsigned int)frame[length - 3] << 8) |
                        frame[length - 2];
    return (double)hour * 3600.0 + (double)usec / 1000000.0;
}

static void CopyText(char* dest, int destLen, const unsigned char* src, int count)
{
    int n = 0;
    while (n < count && n < destLen - 1 && src[n] >= 32 && src[n] < 127)
    {
        dest[n] = (char)src[n];
        n++;
    }
    dest[n] = '\0';
}

static void HandleFrame(VC02_SESSION* session, const unsigned char* frame, int length)
{
    if (length < 4)
    {
        return;
    }
    unsigned char cmd = frame[3];
    if (cmd == 0)
    {
        if (length > 6 && frame[4] == 10 && frame[5] == 0)
        {
            CopyText(session->version, (int)sizeof(session->version), frame + 6, length - 7);
        }
        SetEvent(session->response);
        return;
    }
    if (cmd != 1 && cmd != 2)
    {
        return;
    }

    VC02_MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.timeSec = ReadTime(frame, length);
    msg.channel = frame[4];
    msg.isTx = ((frame[5] & 0x30) == 0x10) ? 1 : 0;
    msg.isErr = (frame[5] & 0x01) ? 1 : 0;
    msg.errCode = frame[6];

    if (cmd == 1)
    {
        if (length < 20)
        {
            return;
        }
        msg.kind = VC02_KIND_CAN;
        msg.isFd = (frame[8] & 0x02) ? 1 : 0;
        msg.isExt = (frame[8] & 0x04) ? 1 : 0;
        msg.id = ReadBe32(frame + 9);
        msg.dlc = frame[13];
        if (msg.dlc < 0)
        {
            msg.dlc = 0;
        }
        if (msg.dlc > 64)
        {
            msg.dlc = 64;
        }
        if (14 + msg.dlc > length - 6)
        {
            msg.dlc = length - 6 - 14;
            if (msg.dlc < 0)
            {
                msg.dlc = 0;
            }
        }
        if (msg.dlc > 0)
        {
            memcpy(msg.data, frame + 14, msg.dlc);
        }
        if (msg.isTx && msg.channel >= 1 && msg.channel <= VC02_TX_MAX_CH)
        {
            InterlockedIncrement(&session->txReported[msg.channel]);
            session->txSeen[msg.channel] = 1;
        }
    }
    else
    {
        if (length < 16)
        {
            return;
        }
        msg.kind = VC02_KIND_LIN;
        msg.id = frame[8] & 0x3F;
        msg.dlc = frame[9];
        if (msg.dlc > 8)
        {
            msg.dlc = 8;
        }
        if (10 + msg.dlc > length - 6)
        {
            msg.dlc = length - 6 - 10;
            if (msg.dlc < 0)
            {
                msg.dlc = 0;
            }
        }
        if (msg.dlc > 0)
        {
            memcpy(msg.data, frame + 10, msg.dlc);
        }
    }
    Enqueue(session, msg);
}

static void Consume(VC02_SESSION* session)
{
    if (session->family == VC02_FAMILY_VC01)
    {
        ConsumeVc01(session);
        return;
    }
    int index = 0;
    int dropTo = 0;
    while (index < session->parseLen - 2)
    {
        if (session->parse[index] != 0xAA)
        {
            index++;
            continue;
        }
        int length = (session->parse[index + 1] << 8) | session->parse[index + 2];
        if (length <= 0 || length > VC02_MAX_FRAME)
        {
            index++;
            continue;
        }
        if (index + length > session->parseLen)
        {
            break;
        }
        unsigned char checksum = FrameChecksum(session->parse + index, length - 1);
        if (checksum == session->parse[index + length - 1])
        {
            HandleFrame(session, session->parse + index, length);
            index += length;
            dropTo = index;
            continue;
        }
        index++;
    }
    if (dropTo > 0)
    {
        memmove(session->parse, session->parse + dropTo, session->parseLen - dropTo);
        session->parseLen -= dropTo;
    }
    else if (session->parseLen > VC02_PARSE_CAP - 1024)
    {
        session->parseLen = 0;
    }
}

static DWORD WINAPI ReaderProc(LPVOID param)
{
    VC02_SESSION* session = (VC02_SESSION*)param;
    unsigned char temp[4096];
    while (!session->stop)
    {
        DWORD got = 0;
        if (!ReadFile(session->port, temp, sizeof(temp), &got, nullptr))
        {
            Sleep(10);
            continue;
        }
        if (got == 0)
        {
            Sleep(1);
            continue;
        }
        EnterCriticalSection(&session->cs);
        if (session->parseLen + (int)got > VC02_PARSE_CAP)
        {
            session->parseLen = 0;
        }
        memcpy(session->parse + session->parseLen, temp, got);
        session->parseLen += (int)got;
        Consume(session);
        LeaveCriticalSection(&session->cs);
    }
    return 0;
}

static int ConfigurePort(HANDLE port)
{
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(port, &dcb))
    {
        return 0;
    }
    dcb.BaudRate = VC02_BAUD;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(port, &dcb))
    {
        return 0;
    }
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 200;
    return SetCommTimeouts(port, &timeouts) ? 1 : 0;
}

static VC02_SESSION* CreateSession(int com, int family)
{
    char path[32];
    sprintf_s(path, "\\\\.\\COM%d", com);
    HANDLE port = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8; attempt++)
    {
        port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (port != INVALID_HANDLE_VALUE)
        {
            break;
        }
        DWORD err = GetLastError();
        if (err != ERROR_ACCESS_DENIED && err != ERROR_GEN_FAILURE && err != ERROR_SHARING_VIOLATION)
        {
            break;
        }
        Sleep(100);
    }
    if (port == INVALID_HANDLE_VALUE)
    {
        SetError("VC02 serial port is busy or missing");
        return nullptr;
    }
    if (!ConfigurePort(port))
    {
        CloseHandle(port);
        SetError("VC02 serial port setup failed");
        return nullptr;
    }
    VC02_SESSION* session = new VC02_SESSION();
    memset(session, 0, sizeof(VC02_SESSION));
    session->port = port;
    session->com = com;
    session->family = family;
    session->ref = 1;
    session->response = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    InitializeCriticalSection(&session->cs);
    session->thread = CreateThread(nullptr, 0, ReaderProc, session, 0, nullptr);
    if (session->thread == nullptr || session->response == nullptr)
    {
        session->stop = 1;
        CloseHandle(port);
        if (session->response != nullptr)
        {
            CloseHandle(session->response);
        }
        DeleteCriticalSection(&session->cs);
        delete session;
        SetError("VC02 reader thread failed");
        return nullptr;
    }
    return session;
}

static void DestroySession(VC02_SESSION* session)
{
    if (session == nullptr)
    {
        return;
    }
    session->stop = 1;
    if (session->thread != nullptr)
    {
        WaitForSingleObject(session->thread, 1000);
        CloseHandle(session->thread);
    }
    if (session->port != INVALID_HANDLE_VALUE)
    {
        CloseHandle(session->port);
    }
    if (session->response != nullptr)
    {
        CloseHandle(session->response);
    }
    DeleteCriticalSection(&session->cs);
    delete session;
}

static int QueryVersion(VC02_SESSION* session)
{
    session->version[0] = '\0';
    if (session->family == VC02_FAMILY_VC01)
    {
        unsigned char frame[23];
        memset(frame, 0, sizeof(frame));
        frame[0] = 0xAA;
        frame[1] = 1;
        frame[2] = 0x40;
        frame[3] = 0x46;
        ResetEvent(session->response);
        if (!SendFixed(session, frame))
        {
            return 0;
        }
        if (WaitForSingleObject(session->response, 400) != WAIT_OBJECT_0)
        {
            return 0;
        }
        return session->version[0] != '\0';
    }
    unsigned char payload[3] = { 0, 10, 0 };
    if (!SendAndWait(session, payload, 3, 400))
    {
        return 0;
    }
    return session->version[0] != '\0';
}

static int AcceptVersion(VC02_SESSION* session, VC02_DEVICE* device)
{
    if (session->family == VC02_FAMILY_VC01)
    {
        if (_strnicmp(session->version, "VC01-", 5) != 0 &&
            _strnicmp(session->version, "VC02-", 5) != 0)
        {
            return 0;
        }
        device->canCount = 2;
        device->linCount = 2;
    }
    else if (_strnicmp(session->version, "VC02A-", 6) == 0)
    {
        device->canCount = 5;
        device->linCount = 4;
    }
    else if (_strnicmp(session->version, "VC02C-", 6) == 0)
    {
        device->canCount = 2;
        device->linCount = 4;
    }
    else
    {
        return 0;
    }
    device->com = session->com;
    device->family = session->family;
    strncpy_s(device->version, session->version, _TRUNCATE);
    session->canCount = device->canCount;
    session->linCount = device->linCount;
    return 1;
}

static int ParseComNumber(const char* text)
{
    if (text == nullptr)
    {
        return 0;
    }
    const char* mark = strstr(text, "COM");
    if (mark == nullptr)
    {
        mark = strstr(text, "com");
    }
    if (mark == nullptr)
    {
        return 0;
    }
    int number = atoi(mark + 3);
    return number > 0 ? number : 0;
}

static int ContainsText(const char* text, const char* token)
{
    return text != nullptr && token != nullptr && strstr(text, token) != nullptr;
}

static int MatchesFamily(int family, const char* hardwareId, const char* instanceId, const char* friendly)
{
    int nxp = ContainsText(hardwareId, "VID_1FC9&PID_0094") || ContainsText(instanceId, "VID_1FC9&PID_0094");
    int st = ContainsText(friendly, "STMicroelectronics Virtual COM Port");
    int ftdi = ContainsText(hardwareId, "VID_0403&PID_6015") || ContainsText(instanceId, "VID_0403&PID_6015");
    if (family == VC02_FAMILY_VC01)
    {
        return (!nxp && (st || ftdi)) ? 1 : 0;
    }
    return nxp ? 1 : 0;
}

static int AddCom(int* coms, int count, int maxCount, int com)
{
    if (com <= 0 || count >= maxCount)
    {
        return count;
    }
    for (int i = 0; i < count; i++)
    {
        if (coms[i] == com)
        {
            return count;
        }
    }
    coms[count] = com;
    return count + 1;
}

static int ReadPortNumber(HDEVINFO info, SP_DEVINFO_DATA* dev, const char* friendly)
{
    int com = 0;
    HKEY key = SetupDiOpenDevRegKey(info, dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key != INVALID_HANDLE_VALUE)
    {
        char portName[64] = { 0 };
        DWORD size = sizeof(portName);
        DWORD type = 0;
        if (RegQueryValueExA(key, "PortName", nullptr, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS)
        {
            com = ParseComNumber(portName);
        }
        RegCloseKey(key);
    }
    if (com == 0)
    {
        com = ParseComNumber(friendly);
    }
    return com;
}

static int EnumComPorts(int* coms, int maxCount, int family)
{
    static const GUID kPortsClass =
    { 0x4d36e978, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    HDEVINFO info = SetupDiGetClassDevsA(&kPortsClass, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    int count = 0;
    for (DWORD index = 0; count < maxCount; index++)
    {
        SP_DEVINFO_DATA dev;
        memset(&dev, 0, sizeof(dev));
        dev.cbSize = sizeof(dev);
        if (!SetupDiEnumDeviceInfo(info, index, &dev))
        {
            break;
        }
        char hardware[512] = { 0 };
        char instance[512] = { 0 };
        char friendly[512] = { 0 };
        SetupDiGetDeviceRegistryPropertyA(info, &dev, SPDRP_HARDWAREID, nullptr, (PBYTE)hardware, sizeof(hardware), nullptr);
        SetupDiGetDeviceInstanceIdA(info, &dev, instance, sizeof(instance), nullptr);
        SetupDiGetDeviceRegistryPropertyA(info, &dev, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)friendly, sizeof(friendly), nullptr);
        if (!MatchesFamily(family, hardware, instance, friendly))
        {
            continue;
        }
        count = AddCom(coms, count, maxCount, ReadPortNumber(info, &dev, friendly));
    }
    SetupDiDestroyDeviceInfoList(info);
    return count;
}

struct COM_FAMILY
{
    int com;
    int family;
};

static COM_FAMILY g_known[32];
static int g_knownCount = 0;

static void RememberFamily(int com, int family)
{
    for (int i = 0; i < g_knownCount; i++)
    {
        if (g_known[i].com == com)
        {
            g_known[i].family = family;
            return;
        }
    }
    if (g_knownCount < 32)
    {
        g_known[g_knownCount].com = com;
        g_known[g_knownCount].family = family;
        g_knownCount++;
    }
}

static int RecallFamily(int com)
{
    for (int i = 0; i < g_knownCount; i++)
    {
        if (g_known[i].com == com)
        {
            return g_known[i].family;
        }
    }
    return 0;
}

static VC02_SESSION* FindOpen(int com)
{
    for (int i = 0; i < VC02_MAX_SESSIONS; i++)
    {
        if (g_sessions[i] != nullptr && g_sessions[i]->com == com)
        {
            return g_sessions[i];
        }
    }
    return nullptr;
}

extern "C" {

static int ProbeFamily(int family, VC02_DEVICE* devices, int found, int maxCount)
{
    int coms[16];
    int comCount = EnumComPorts(coms, 16, family);
    for (int i = 0; i < comCount && found < maxCount; i++)
    {
        EnterCriticalSection(&g_lock);
        VC02_SESSION* open = FindOpen(coms[i]);
        if (open != nullptr)
        {
            VC02_DEVICE device;
            memset(&device, 0, sizeof(device));
            if (open->family == family && AcceptVersion(open, &device))
            {
                devices[found++] = device;
                RememberFamily(coms[i], family);
            }
            LeaveCriticalSection(&g_lock);
            continue;
        }
        LeaveCriticalSection(&g_lock);

        VC02_SESSION* probe = CreateSession(coms[i], family);
        if (probe == nullptr)
        {
            continue;
        }
        VC02_DEVICE device;
        memset(&device, 0, sizeof(device));
        if (QueryVersion(probe) && AcceptVersion(probe, &device))
        {
            devices[found++] = device;
            RememberFamily(coms[i], family);
        }
        DestroySession(probe);
    }
    return found;
}

static VC02_SESSION* ProbeNewSession(int com, int family)
{
    VC02_SESSION* session = CreateSession(com, family);
    if (session == nullptr)
    {
        return nullptr;
    }
    VC02_DEVICE device;
    memset(&device, 0, sizeof(device));
    if (!QueryVersion(session) || !AcceptVersion(session, &device))
    {
        DestroySession(session);
        return nullptr;
    }
    RememberFamily(com, family);
    return session;
}

int vc02_find(VC02_DEVICE* devices, int maxCount)
{
    EnsureLock();
    if (devices == nullptr || maxCount <= 0)
    {
        return 0;
    }
    int found = ProbeFamily(VC02_FAMILY_VC02, devices, 0, maxCount);
    found = ProbeFamily(VC02_FAMILY_VC01, devices, found, maxCount);
    if (found == 0)
    {
        SetError("No CAEA serial device answered");
    }
    return found;
}

void* vc02_open(int com)
{
    EnsureLock();
    EnterCriticalSection(&g_lock);
    VC02_SESSION* existing = FindOpen(com);
    if (existing != nullptr)
    {
        existing->ref++;
        LeaveCriticalSection(&g_lock);
        return existing;
    }
    LeaveCriticalSection(&g_lock);

    int family = RecallFamily(com);
    VC02_SESSION* session = ProbeNewSession(com, family == VC02_FAMILY_VC01 ? VC02_FAMILY_VC01 : VC02_FAMILY_VC02);
    if (session == nullptr && family == 0)
    {
        session = ProbeNewSession(com, VC02_FAMILY_VC01);
    }
    if (session == nullptr)
    {
        SetError("Serial device did not answer as VC02 or VC01");
        return nullptr;
    }
    EnterCriticalSection(&g_lock);
    existing = FindOpen(com);
    if (existing != nullptr)
    {
        existing->ref++;
        LeaveCriticalSection(&g_lock);
        DestroySession(session);
        return existing;
    }
    for (int i = 0; i < VC02_MAX_SESSIONS; i++)
    {
        if (g_sessions[i] == nullptr)
        {
            g_sessions[i] = session;
            LeaveCriticalSection(&g_lock);
            return session;
        }
    }
    LeaveCriticalSection(&g_lock);
    DestroySession(session);
    SetError("Too many VC02 sessions");
    return nullptr;
}

void vc02_close(void* handle)
{
    EnsureLock();
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr)
    {
        return;
    }
    EnterCriticalSection(&g_lock);
    session->ref--;
    if (session->ref > 0)
    {
        LeaveCriticalSection(&g_lock);
        return;
    }
    for (int i = 0; i < VC02_MAX_SESSIONS; i++)
    {
        if (g_sessions[i] == session)
        {
            g_sessions[i] = nullptr;
        }
    }
    LeaveCriticalSection(&g_lock);
    DestroySession(session);
}

static int SetCanVc01(VC02_SESSION* session, int channel, unsigned baud, int termination, int listenOnly, int resend)
{
    if (resend < 0)
    {
        resend = 0;
    }
    if (resend > 2)
    {
        resend = 2;
    }
    session->configuring = 1;
    unsigned char frame[23];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 1;
    frame[2] = (unsigned char)channel;
    frame[9] = (unsigned char)(baud >> 16);
    frame[10] = (unsigned char)(baud >> 8);
    frame[11] = (unsigned char)baud;
    frame[12] = (unsigned char)(listenOnly ? 1 : 0);
    frame[13] = (unsigned char)resend;
    frame[14] = 1;
    int ok = SendFixed(session, frame);
    Sleep(40);
    unsigned char term[23];
    memset(term, 0, sizeof(term));
    term[0] = 0xAA;
    term[1] = 1;
    term[2] = (unsigned char)channel;
    term[3] = (unsigned char)(channel == 1 ? 83 : 84);
    term[9] = (unsigned char)(termination ? 1 : 0);
    ok = SendFixed(session, term) && ok;
    Sleep(80);
    session->configuring = 0;
    return ok;
}

static int SetLinVc01(VC02_SESSION* session, int channel, unsigned baud, int master)
{
    unsigned char wire = (unsigned char)(channel == 1 ? 17 : 18);
    session->configuring = 1;
    unsigned char frame[23];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 1;
    frame[2] = wire;
    frame[3] = 0x10;
    frame[7] = wire;
    frame[9] = (unsigned char)(baud >> 16);
    frame[10] = (unsigned char)(baud >> 8);
    frame[11] = (unsigned char)baud;
    int ok = SendFixed(session, frame);
    Sleep(40);
    unsigned char role[23];
    memset(role, 0, sizeof(role));
    role[0] = 0xAA;
    role[1] = 1;
    role[2] = wire;
    role[3] = 81;
    role[9] = (unsigned char)(master ? 1 : 0);
    ok = SendFixed(session, role) && ok;
    Sleep(80);
    session->configuring = 0;
    return ok;
}

static int TxCanVc01(VC02_SESSION* session, int channel, unsigned id, const unsigned char* data, int dlc)
{
    if (dlc > 8)
    {
        dlc = 8;
    }
    unsigned char frame[23];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 2;
    frame[2] = (unsigned char)channel;
    frame[3] = 1;
    frame[4] = (unsigned char)(id >> 24);
    frame[5] = (unsigned char)(id >> 16);
    frame[6] = (unsigned char)(id >> 8);
    frame[7] = (unsigned char)id;
    frame[8] = (unsigned char)dlc;
    if (dlc > 0 && data != nullptr)
    {
        memcpy(frame + 9, data, dlc);
    }
    return SendFixed(session, frame);
}

static int TxLinVc01(VC02_SESSION* session, int channel, unsigned id, const unsigned char* data, int dlc)
{
    unsigned char wire = (unsigned char)(channel == 1 ? 17 : 18);
    unsigned char frame[23];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 3;
    frame[2] = wire;
    frame[3] = 1;
    frame[7] = (unsigned char)(id & 0x3F);
    frame[8] = (unsigned char)dlc;
    if (dlc > 0 && data != nullptr)
    {
        memcpy(frame + 9, data, dlc);
    }
    return SendFixed(session, frame);
}

int vc02_set_can(void* handle, int channel, unsigned baud, unsigned fdBaud, int enableFd, int termination, int listenOnly, int resend)
{
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr || channel < 1 || channel > session->canCount)
    {
        return 0;
    }
    if (session->family == VC02_FAMILY_VC01)
    {
        if (baud == 0)
        {
            baud = 500000;
        }
        return SetCanVc01(session, channel, baud, termination, listenOnly, resend);
    }
    if (baud == 0)
    {
        baud = 500000;
    }
    session->configuring = 1;
    unsigned char enable[4] = { 0, (unsigned char)channel, 0, (unsigned char)((enableFd && channel <= 4) ? 2 : 1) };
    unsigned char rate[6] = { 0, (unsigned char)channel, 1, (unsigned char)(baud >> 16), (unsigned char)(baud >> 8), (unsigned char)baud };
    unsigned char modeSub = (channel == 5) ? 3 : 4;
    unsigned char ackSub = (channel == 5) ? 4 : 5;
    unsigned char termSub = (channel == 5) ? 2 : 3;
    if (resend < 0)
    {
        resend = 0;
    }
    if (resend > 2)
    {
        resend = 2;
    }
    unsigned char mode[4] = { 0, (unsigned char)channel, modeSub, (unsigned char)(listenOnly ? 1 : 0) };
    unsigned char ack[4] = { 0, (unsigned char)channel, ackSub, (unsigned char)resend };
    unsigned char term[4] = { 0, (unsigned char)channel, termSub, (unsigned char)(termination ? 1 : 0) };
    int ok = SendPayload(session, enable, 4);
    Sleep(20);
    ok = SendPayload(session, rate, 6) && ok;
    Sleep(20);
    ok = SendPayload(session, mode, 4) && ok;
    Sleep(20);
    ok = SendPayload(session, ack, 4) && ok;
    Sleep(20);
    ok = SendPayload(session, term, 4) && ok;
    if (enableFd && channel <= 4)
    {
        if (fdBaud == 0)
        {
            fdBaud = 2000000;
        }
        unsigned char fd[6] = { 0, (unsigned char)channel, 2, (unsigned char)(fdBaud >> 16), (unsigned char)(fdBaud >> 8), (unsigned char)fdBaud };
        Sleep(20);
        ok = SendPayload(session, fd, 6) && ok;
    }
    Sleep(80);
    session->configuring = 0;
    return ok;
}

int vc02_set_lin(void* handle, int channel, unsigned baud, int master)
{
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr || channel < 1 || channel > session->linCount)
    {
        return 0;
    }
    if (baud == 0)
    {
        baud = 19200;
    }
    if (session->family == VC02_FAMILY_VC01)
    {
        return SetLinVc01(session, channel, baud, master);
    }
    int wire = channel + 5;
    session->configuring = 1;
    unsigned char enable[4] = { 0, (unsigned char)wire, 4, 1 };
    unsigned char role[4] = { 0, (unsigned char)wire, 1, (unsigned char)(master ? 1 : 0) };
    unsigned char rate[5] = { 0, (unsigned char)wire, 2, (unsigned char)(baud >> 8), (unsigned char)baud };
    int ok = SendPayload(session, enable, 4);
    Sleep(20);
    ok = SendPayload(session, role, 4) && ok;
    Sleep(20);
    ok = SendPayload(session, rate, 5) && ok;
    Sleep(80);
    session->configuring = 0;
    return ok;
}

int vc02_tx_can(void* handle, int channel, unsigned id, int ext, int fd, int brs, const unsigned char* data, int dlc)
{
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr || session->configuring || channel < 1 || channel > session->canCount)
    {
        return 0;
    }
    if (session->family == VC02_FAMILY_VC01)
    {
        if (dlc < 0)
        {
            dlc = 0;
        }
        return TxCanVc01(session, channel, id, data, dlc);
    }
    if (fd && channel > 4)
    {
        return 0;
    }
    if (dlc < 0)
    {
        dlc = 0;
    }
    if (fd)
    {
        if (dlc > 64)
        {
            dlc = 64;
        }
    }
    else if (dlc > 8)
    {
        dlc = 8;
    }
    int length = dlc + 20;
    unsigned char frame[VC02_MAX_FRAME];
    memset(frame, 0, length);
    frame[0] = 0xAA;
    frame[1] = (unsigned char)(length >> 8);
    frame[2] = (unsigned char)length;
    frame[3] = 1;
    frame[4] = (unsigned char)channel;
    frame[5] = 16;
    frame[8] = (unsigned char)((fd ? 2 : 0) | (ext ? 4 : 0) | ((fd && brs) ? 1 : 0));
    frame[9] = (unsigned char)(id >> 24);
    frame[10] = (unsigned char)(id >> 16);
    frame[11] = (unsigned char)(id >> 8);
    frame[12] = (unsigned char)id;
    frame[13] = (unsigned char)dlc;
    if (dlc > 0 && data != nullptr)
    {
        memcpy(frame + 14, data, dlc);
    }
    frame[length - 1] = FrameChecksum(frame, length - 1);
    if (channel <= VC02_TX_MAX_CH && session->txSeen[channel])
    {
        LONG lastRep = session->txReported[channel];
        DWORD t0 = GetTickCount();
        while (session->txSubmitted[channel] - session->txReported[channel] >= VC02_TX_WINDOW)
        {
            LONG rep = session->txReported[channel];
            if (rep != lastRep)
            {
                lastRep = rep;
                t0 = GetTickCount();
            }
            else if (GetTickCount() - t0 >= VC02_TX_STALL_MS)
            {
                InterlockedExchange(&session->txSubmitted[channel], rep);
                break;
            }
            SwitchToThread();
        }
    }
    if (!WriteRaw(session, frame, length))
    {
        return 0;
    }
    if (channel <= VC02_TX_MAX_CH)
    {
        InterlockedIncrement(&session->txSubmitted[channel]);
    }
    return 1;
}

int vc02_tx_lin(void* handle, int channel, unsigned id, const unsigned char* data, int dlc)
{
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr || session->configuring || channel < 1 || channel > session->linCount)
    {
        return 0;
    }
    if (dlc < 0)
    {
        dlc = 0;
    }
    if (dlc > 8)
    {
        dlc = 8;
    }
    if (session->family == VC02_FAMILY_VC01)
    {
        return TxLinVc01(session, channel, id, data, dlc);
    }
    int wire = channel + 5;
    int length = dlc + 16;
    unsigned char frame[VC02_MAX_FRAME];
    memset(frame, 0, length);
    frame[0] = 0xAA;
    frame[1] = (unsigned char)(length >> 8);
    frame[2] = (unsigned char)length;
    frame[3] = 2;
    frame[4] = (unsigned char)wire;
    frame[5] = 16;
    frame[8] = (unsigned char)(id & 0x3F);
    frame[9] = (unsigned char)dlc;
    if (dlc > 0 && data != nullptr)
    {
        memcpy(frame + 10, data, dlc);
    }
    frame[length - 1] = FrameChecksum(frame, length - 1);
    return WriteRaw(session, frame, length);
}

int vc02_read(void* handle, VC02_MSG* msgs, int maxCount, int kind)
{
    VC02_SESSION* session = (VC02_SESSION*)handle;
    if (session == nullptr || msgs == nullptr || maxCount <= 0)
    {
        return 0;
    }
    int count = 0;
    EnterCriticalSection(&session->cs);
    if (kind == VC02_KIND_LIN)
    {
        while (count < maxCount && session->linTail != session->linHead)
        {
            msgs[count++] = session->linQueue[session->linTail];
            session->linTail = (session->linTail + 1) % VC02_QUEUE;
        }
    }
    else
    {
        while (count < maxCount && session->canTail != session->canHead)
        {
            msgs[count++] = session->canQueue[session->canTail];
            session->canTail = (session->canTail + 1) % VC02_QUEUE;
        }
    }
    LeaveCriticalSection(&session->cs);
    return count;
}

const char* vc02_last_error(void)
{
    return g_lastError;
}

}
