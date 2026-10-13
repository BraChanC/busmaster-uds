#include "CAN_USBCAN_stdafx.h"
#include "CAN_USBCAN.h"

#include <string>

#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "Error.h"
#include "DIL_Interface/HardwareListingCAN.h"

#define USAGE_EXPORT
#include "CAN_USBCAN_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_USBCANApp, CWinApp)
END_MESSAGE_MAP()

CCAN_USBCANApp::CCAN_USBCANApp()
{
}

CCAN_USBCANApp theApp;

BOOL CCAN_USBCANApp::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

#define MAX_CLIENT_ALLOWED      16
#define MAX_BUFF_ALLOWED        16
#define MAX_USBCAN_CHANNELS     16
#define MAX_USBCAN_DEVICES      16

#define VCI_USBCAN1             3
#define VCI_USBCAN2             4
#define VCI_USBCAN_E_U          20
#define VCI_USBCAN_2E_U         21
#define VCI_STATUS_OK           1

enum
{
    STATE_DRIVER_SELECTED = 0x0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};

typedef struct tagClientBufMap
{
    DWORD m_dwClientID;
    CBaseCANBufFSE* m_pClientBuf[MAX_BUFF_ALLOWED];
    char m_acClientName[MAX_PATH];
    UINT m_unBufCount;
    tagClientBufMap()
    {
        m_dwClientID = 0;
        m_unBufCount = 0;
        memset(m_acClientName, 0, sizeof(m_acClientName));
        for (INT i = 0; i < MAX_BUFF_ALLOWED; i++)
        {
            m_pClientBuf[i] = nullptr;
        }
    }
} SCLIENTBUFMAP;

typedef struct tagVciBoardInfo
{
    USHORT hw_Version;
    USHORT fw_Version;
    USHORT dr_Version;
    USHORT in_Version;
    USHORT irq_Num;
    BYTE can_Num;
    CHAR str_Serial_Num[20];
    CHAR str_hw_Type[40];
    USHORT Reserved[4];
} VCI_BOARD_INFO;

typedef struct tagVciCanObj
{
    UINT ID;
    UINT TimeStamp;
    BYTE TimeFlag;
    BYTE SendType;
    BYTE RemoteFlag;
    BYTE ExternFlag;
    BYTE DataLen;
    BYTE Data[8];
    BYTE Reserved[3];
} VCI_CAN_OBJ;

typedef struct tagVciInitConfig
{
    DWORD AccCode;
    DWORD AccMask;
    DWORD Reserved;
    UCHAR Filter;
    UCHAR Timing0;
    UCHAR Timing1;
    UCHAR Mode;
} VCI_INIT_CONFIG;

typedef DWORD (__stdcall* PFN_VCI_OpenDevice)(DWORD, DWORD, DWORD);
typedef DWORD (__stdcall* PFN_VCI_CloseDevice)(DWORD, DWORD);
typedef DWORD (__stdcall* PFN_VCI_InitCAN)(DWORD, DWORD, DWORD, VCI_INIT_CONFIG*);
typedef DWORD (__stdcall* PFN_VCI_ReadBoardInfo)(DWORD, DWORD, VCI_BOARD_INFO*);
typedef DWORD (__stdcall* PFN_VCI_ClearBuffer)(DWORD, DWORD, DWORD);
typedef DWORD (__stdcall* PFN_VCI_StartCAN)(DWORD, DWORD, DWORD);
typedef DWORD (__stdcall* PFN_VCI_ResetCAN)(DWORD, DWORD, DWORD);
typedef ULONG (__stdcall* PFN_VCI_Transmit)(DWORD, DWORD, DWORD, VCI_CAN_OBJ*, ULONG);
typedef ULONG (__stdcall* PFN_VCI_Receive)(DWORD, DWORD, DWORD, VCI_CAN_OBJ*, ULONG, INT);
typedef DWORD (__stdcall* PFN_VCI_FindUsbDevice2)(VCI_BOARD_INFO*);

static SCLIENTBUFMAP sg_asClientToBufMap[MAX_CLIENT_ALLOWED];
static UINT sg_unClientCnt = 0;
static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;
static CRITICAL_SECTION sg_DIL_CriticalSection;
static BOOL sg_bCsInit = FALSE;
static HWND sg_hOwnerWnd = nullptr;
static SYSTEMTIME sg_CurrSysTime = {0};
static UINT64 sg_TimeStamp = 0;
static LARGE_INTEGER sg_QueryTickCount;
static LARGE_INTEGER sg_lnFrequency;
static std::string sg_acErrStr;
static INT sg_nNoOfChannels = 0;
static DWORD sg_adwType[MAX_USBCAN_CHANNELS];
static DWORD sg_adwDevIndex[MAX_USBCAN_CHANNELS];
static DWORD sg_adwCh[MAX_USBCAN_CHANNELS];
static unsigned int sg_aunBaud[MAX_USBCAN_CHANNELS];
static BOOL sg_abListen[MAX_USBCAN_CHANNELS];
static STCAN_MSG sg_asLastTx[MAX_USBCAN_CHANNELS];
static BOOL sg_abLastTxValid[MAX_USBCAN_CHANNELS];
static LARGE_INTEGER sg_alnLastTxQpc[MAX_USBCAN_CHANNELS];
static HANDLE sg_hRxThread = nullptr;
static volatile BOOL sg_bRxRun = FALSE;
static BOOL sg_bHwOpen = FALSE;

static HMODULE sg_hDll = nullptr;
static PFN_VCI_OpenDevice sg_OpenDevice = nullptr;
static PFN_VCI_CloseDevice sg_CloseDevice = nullptr;
static PFN_VCI_InitCAN sg_InitCAN = nullptr;
static PFN_VCI_ReadBoardInfo sg_ReadBoardInfo = nullptr;
static PFN_VCI_ClearBuffer sg_ClearBuffer = nullptr;
static PFN_VCI_StartCAN sg_StartCAN = nullptr;
static PFN_VCI_ResetCAN sg_ResetCAN = nullptr;
static PFN_VCI_Transmit sg_Transmit = nullptr;
static PFN_VCI_Receive sg_Receive = nullptr;
static PFN_VCI_FindUsbDevice2 sg_FindUsbDevice2 = nullptr;

class CDIL_CAN_USBCAN : public CBaseDIL_CAN_Controller
{
public:
    HRESULT CAN_PerformInitOperations(void);
    HRESULT CAN_PerformClosureOperations(void);
    HRESULT CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData);
    HRESULT CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT CAN_DeselectHwInterface(void);
    HRESULT CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length);
    HRESULT CAN_StartHardware(void);
    HRESULT CAN_StopHardware(void);
    HRESULT CAN_GetCurrStatus(STATUSMSG& StatusData);
    HRESULT CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg);
    HRESULT CAN_GetLastErrorString(std::string& acErrorStr);
    HRESULT CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam);
    HRESULT CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetAppParams(HWND hWndOwner);
    HRESULT CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj);
    HRESULT CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName);
    HRESULT CAN_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus);
    HRESULT CAN_LoadDriverLibrary(void);
    HRESULT CAN_UnloadDriverLibrary(void);
    HRESULT CAN_SetHardwareChannel(PSCONTROLLER_DETAILS, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCount);
};

static CDIL_CAN_USBCAN* g_pouDIL_CAN_USBCAN = nullptr;

static BOOL bIsBufferExists(const SCLIENTBUFMAP& sClientObj, const CBaseCANBufFSE* pBuf)
{
    for (UINT i = 0; i < sClientObj.m_unBufCount; i++)
    {
        if (pBuf == sClientObj.m_pClientBuf[i])
        {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bRemoveClientBuffer(CBaseCANBufFSE* RootBufferArray[MAX_BUFF_ALLOWED], UINT& unCount, CBaseCANBufFSE* BufferToRemove)
{
    for (UINT i = 0; i < unCount; i++)
    {
        if (RootBufferArray[i] == BufferToRemove)
        {
            if (i < (unCount - 1))
            {
                RootBufferArray[i] = RootBufferArray[unCount - 1];
            }
            unCount--;
            break;
        }
    }
    return TRUE;
}

static BOOL bGetClientObj(DWORD dwClientID, UINT& unClientIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].m_dwClientID == dwClientID)
        {
            unClientIndex = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bClientExist(const char* pcClientName, INT& Index)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (_tcscmp(pcClientName, sg_asClientToBufMap[i].m_acClientName) == 0)
        {
            Index = (INT)i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bClientIdExist(const DWORD& dwClientId)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].m_dwClientID == dwClientId)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bRemoveClient(DWORD dwClientId)
{
    UINT unClientIndex = 0;
    if (sg_unClientCnt == 0 || !bGetClientObj(dwClientId, unClientIndex))
    {
        return FALSE;
    }
    sg_asClientToBufMap[unClientIndex].m_dwClientID = 0;
    memset(sg_asClientToBufMap[unClientIndex].m_acClientName, 0, sizeof(sg_asClientToBufMap[unClientIndex].m_acClientName));
    sg_asClientToBufMap[unClientIndex].m_unBufCount = 0;
    if ((unClientIndex + 1) < sg_unClientCnt)
    {
        sg_asClientToBufMap[unClientIndex] = sg_asClientToBufMap[sg_unClientCnt - 1];
    }
    sg_unClientCnt--;
    return TRUE;
}

static DWORD dwGetAvailableClientSlot(void)
{
    DWORD nClientId = 2;
    for (INT i = 0; i < MAX_CLIENT_ALLOWED; i++)
    {
        if (bClientIdExist(nClientId))
        {
            nClientId += 1;
        }
        else
        {
            break;
        }
    }
    return nClientId;
}

static void vWriteIntoClientsBuffer(STCANDATA& can_data)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        for (UINT j = 0; j < sg_asClientToBufMap[i].m_unBufCount; j++)
        {
            if (sg_asClientToBufMap[i].m_pClientBuf[j] != nullptr)
            {
                sg_asClientToBufMap[i].m_pClientBuf[j]->WriteIntoBuffer(&can_data);
            }
        }
    }
}

static void CopyField(char* pszOut, size_t nOut, const char* pszIn, size_t nIn)
{
    size_t n = 0;
    if (pszIn == nullptr)
    {
        pszOut[0] = 0;
        return;
    }
    while (n < nIn && n + 1 < nOut && pszIn[n] != 0 && (unsigned char)pszIn[n] >= 0x20 && (unsigned char)pszIn[n] < 0x7F)
    {
        pszOut[n] = pszIn[n];
        n++;
    }
    pszOut[n] = 0;
    while (n > 0 && pszOut[n - 1] == ' ')
    {
        pszOut[--n] = 0;
    }
}

static const char* TypeName(DWORD dwType)
{
    if (dwType == VCI_USBCAN1) { return "USBCAN1"; }
    if (dwType == VCI_USBCAN2) { return "USBCAN2"; }
    if (dwType == VCI_USBCAN_E_U) { return "USBCAN-E-U"; }
    if (dwType == VCI_USBCAN_2E_U) { return "USBCAN-2E-U"; }
    return "USBCAN";
}

static DWORD TypeFromText(const char* psz, int nCanNum)
{
    char ac[64] = {0};
    strncpy_s(ac, psz ? psz : "", _TRUNCATE);
    _strupr_s(ac);
    if (strstr(ac, "2E") != nullptr) { return VCI_USBCAN_2E_U; }
    if (strstr(ac, "E-U") != nullptr || strstr(ac, "E_U") != nullptr) { return VCI_USBCAN_E_U; }
    if (strstr(ac, "USBCAN2") != nullptr || strstr(ac, "USBCAN-2") != nullptr || strstr(ac, "CAN2") != nullptr)
    {
        return VCI_USBCAN2;
    }
    if (strstr(ac, "USBCAN1") != nullptr || strstr(ac, "USBCAN-1") != nullptr || strstr(ac, "CAN1") != nullptr)
    {
        return VCI_USBCAN1;
    }
    return (nCanNum >= 2) ? VCI_USBCAN2 : VCI_USBCAN1;
}

static int DefaultCanCount(DWORD dwType)
{
    if (dwType == VCI_USBCAN2 || dwType == VCI_USBCAN_2E_U)
    {
        return 2;
    }
    return 1;
}

static unsigned int BaudFromText(const char* psz)
{
    if (psz == nullptr || psz[0] == 0)
    {
        return 500000;
    }
    unsigned int unBaud = (unsigned int)atoi(psz);
    if (unBaud == 0)
    {
        return 500000;
    }
    if (unBaud < 10000)
    {
        unBaud *= 1000;
    }
    return unBaud;
}

static void TimingFromBaud(unsigned int unBaud, UCHAR& ucT0, UCHAR& ucT1)
{
    struct STiming
    {
        unsigned int unBaud;
        UCHAR ucT0;
        UCHAR ucT1;
    };
    static const STiming asTable[] =
    {
        {1000000, 0x00, 0x14},
        {800000, 0x00, 0x16},
        {666000, 0x80, 0xB6},
        {500000, 0x00, 0x1C},
        {400000, 0x80, 0xFA},
        {250000, 0x01, 0x1C},
        {200000, 0x81, 0xFA},
        {125000, 0x03, 0x1C},
        {100000, 0x04, 0x1C},
        {80000, 0x83, 0xFF},
        {50000, 0x09, 0x1C},
        {40000, 0x87, 0xFF},
        {20000, 0x18, 0x1C},
        {10000, 0x31, 0x1C},
        {5000, 0xBF, 0xFF}
    };
    const STiming* pBest = &asTable[3];
    unsigned int unBest = 0xFFFFFFFF;
    for (int i = 0; i < (int)(sizeof(asTable) / sizeof(asTable[0])); i++)
    {
        unsigned int unDiff = (unBaud > asTable[i].unBaud) ? (unBaud - asTable[i].unBaud) : (asTable[i].unBaud - unBaud);
        if (unDiff < unBest)
        {
            unBest = unDiff;
            pBest = &asTable[i];
        }
    }
    ucT0 = pBest->ucT0;
    ucT1 = pBest->ucT1;
}

static void ApplyControllerConfig(PSCONTROLLER_DETAILS pDetails, int nCount)
{
    if (pDetails == nullptr)
    {
        return;
    }
    INT nLim = nCount;
    if (nLim <= 0)
    {
        nLim = 1;
    }
    if (nLim > MAX_USBCAN_CHANNELS)
    {
        nLim = MAX_USBCAN_CHANNELS;
    }
    for (INT i = 0; i < nLim; i++)
    {
        sg_aunBaud[i] = BaudFromText(pDetails[i].m_omStrBaudrate.c_str());
        sg_abListen[i] = (pDetails[i].m_ucControllerMode == 2) ? TRUE : FALSE;
        pDetails[i].m_bcanFDEnabled = false;
        pDetails[i].m_bSupportCANFD = false;
    }
}

static void DecodeHw(const INTERFACE_HW& hw, DWORD& dwType, DWORD& dwIndex, DWORD& dwCh)
{
    dwType = (DWORD)hw.m_dwVendor;
    dwIndex = (DWORD)((hw.m_dwIdInterface >> 8) & 0xFF);
    dwCh = (DWORD)(hw.m_dwIdInterface & 0xFF);
}

static void FillFromSelected(const INTERFACE_HW_LIST& sSel, INT nCount)
{
    sg_nNoOfChannels = nCount > MAX_USBCAN_CHANNELS ? MAX_USBCAN_CHANNELS : nCount;
    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        DecodeHw(sSel[i], sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]);
        if (sg_aunBaud[i] == 0)
        {
            sg_aunBaud[i] = 500000;
        }
    }
}

static HMODULE LoadControlCan(void)
{
    char acPath[MAX_PATH] = {0};
    HMODULE hSelf = GetModuleHandleA("CAN_USBCAN.dll");
    if (hSelf != nullptr && GetModuleFileNameA(hSelf, acPath, MAX_PATH) > 0)
    {
        char* pcSlash = strrchr(acPath, '\\');
        if (pcSlash != nullptr)
        {
            if (strcpy_s(pcSlash + 1, MAX_PATH - (size_t)(pcSlash + 1 - acPath), "ControlCAN.dll") == 0)
            {
                HMODULE h = LoadLibraryA(acPath);
                if (h != nullptr)
                {
                    return h;
                }
            }
        }
    }
    if (GetModuleFileNameA(nullptr, acPath, MAX_PATH) > 0)
    {
        char* pcSlash = strrchr(acPath, '\\');
        if (pcSlash != nullptr)
        {
            if (strcpy_s(pcSlash + 1, MAX_PATH - (size_t)(pcSlash + 1 - acPath), "ControlCAN.dll") == 0)
            {
                HMODULE h = LoadLibraryA(acPath);
                if (h != nullptr)
                {
                    return h;
                }
            }
        }
    }
    return LoadLibraryA("ControlCAN.dll");
}

static DWORD OpenDeviceRetry(DWORD dwType, DWORD dwIndex)
{
    if (sg_OpenDevice == nullptr)
    {
        return 0;
    }
    for (int nTry = 0; nTry < 6; nTry++)
    {
        DWORD dw = sg_OpenDevice(dwType, dwIndex, 0);
        if (dw == VCI_STATUS_OK)
        {
            return dw;
        }
        Sleep(100);
    }
    return 0;
}

struct SUsbDev
{
    DWORD dwType;
    DWORD dwIndex;
    int nCanNum;
    char acType[48];
    char acSn[24];
};

static int CountSameType(const SUsbDev* asDev, int nDev, DWORD dwType)
{
    int n = 0;
    for (int i = 0; i < nDev; i++)
    {
        if (asDev[i].dwType == dwType)
        {
            n++;
        }
    }
    return n;
}

static void AddBoard(SUsbDev* asDev, int& nDev, const VCI_BOARD_INFO& sInfo, DWORD dwType, DWORD dwIndex)
{
    if (nDev >= MAX_USBCAN_DEVICES)
    {
        return;
    }
    if (dwType == 0)
    {
        dwType = TypeFromText(sInfo.str_hw_Type, sInfo.can_Num);
    }
    if (dwIndex == 0xFFFFFFFF)
    {
        dwIndex = (DWORD)CountSameType(asDev, nDev, dwType);
    }
    SUsbDev& sDev = asDev[nDev];
    memset(&sDev, 0, sizeof(sDev));
    sDev.dwType = dwType;
    sDev.dwIndex = dwIndex;
    sDev.nCanNum = sInfo.can_Num;
    if (sDev.nCanNum <= 0 || sDev.nCanNum > 4)
    {
        sDev.nCanNum = DefaultCanCount(dwType);
    }
    CopyField(sDev.acType, sizeof(sDev.acType), sInfo.str_hw_Type, sizeof(sInfo.str_hw_Type));
    if (sDev.acType[0] == 0)
    {
        strncpy_s(sDev.acType, TypeName(dwType), _TRUNCATE);
    }
    CopyField(sDev.acSn, sizeof(sDev.acSn), sInfo.str_Serial_Num, sizeof(sInfo.str_Serial_Num));
    nDev++;
}

static int CollectDevices(SUsbDev* asDev)
{
    int nDev = 0;
    if (sg_FindUsbDevice2 != nullptr)
    {
        // ControlCAN writes a fixed 50-board buffer even when nothing is plugged in.
        const int nCap = 50;
        VCI_BOARD_INFO* asInfo = new VCI_BOARD_INFO[nCap];
        memset(asInfo, 0, sizeof(VCI_BOARD_INFO) * nCap);
        DWORD dwRet = sg_FindUsbDevice2(asInfo);
        int nBoards = 0;
        if (dwRet > 0 && dwRet <= (DWORD)nCap)
        {
            nBoards = (int)dwRet;
        }
        else if (dwRet > (DWORD)nCap)
        {
            nBoards = nCap;
        }
        for (int i = 0; i < nBoards; i++)
        {
            if (asInfo[i].can_Num == 0 && asInfo[i].str_hw_Type[0] == 0 && asInfo[i].str_Serial_Num[0] == 0)
            {
                continue;
            }
            AddBoard(asDev, nDev, asInfo[i], 0, 0xFFFFFFFF);
        }
        delete[] asInfo;
    }
    if (nDev > 0 || sg_OpenDevice == nullptr || sg_CloseDevice == nullptr)
    {
        return nDev;
    }
    static const DWORD adwProbe[] = { VCI_USBCAN2, VCI_USBCAN1, VCI_USBCAN_2E_U, VCI_USBCAN_E_U };
    for (int nType = 0; nType < (int)(sizeof(adwProbe) / sizeof(adwProbe[0])); nType++)
    {
        for (DWORD dwIndex = 0; dwIndex < 8 && nDev < MAX_USBCAN_DEVICES; dwIndex++)
        {
            if (sg_OpenDevice(adwProbe[nType], dwIndex, 0) != VCI_STATUS_OK)
            {
                break;
            }
            VCI_BOARD_INFO sInfo;
            memset(&sInfo, 0, sizeof(sInfo));
            if (sg_ReadBoardInfo != nullptr)
            {
                sg_ReadBoardInfo(adwProbe[nType], dwIndex, &sInfo);
            }
            sg_CloseDevice(adwProbe[nType], dwIndex);
            AddBoard(asDev, nDev, sInfo, adwProbe[nType], dwIndex);
        }
    }
    return nDev;
}

static INT EnumerateChannels(INTERFACE_HW_LIST& asList)
{
    SUsbDev asDev[MAX_USBCAN_DEVICES];
    memset(asDev, 0, sizeof(asDev));
    int nDev = CollectDevices(asDev);
    INT nFound = 0;
    for (int i = 0; i < nDev; i++)
    {
        for (int nCh = 0; nCh < asDev[i].nCanNum && nFound < MAX_USBCAN_CHANNELS; nCh++)
        {
            INTERFACE_HW& hw = asList[nFound];
            hw.m_dwVendor = asDev[i].dwType;
            hw.m_dwIdInterface = (asDev[i].dwIndex << 8) | (DWORD)nCh;
            hw.m_bytNetworkID = (BYTE)nCh;
            char acDesc[128] = {0};
            sprintf_s(acDesc, "%s CH%d", asDev[i].acType, nCh);
            hw.m_acDescription = acDesc;
            hw.m_acNameInterface = "USBCAN";
            hw.m_acDeviceName = asDev[i].acType;
            hw.m_acAdditionalInfo = asDev[i].acSn;
            nFound++;
        }
    }
    return nFound;
}

static BOOL IsTxEcho(int nChannelIndex, UINT unId, const BYTE* pucData, BYTE ucLen, BYTE ucRtr, BYTE ucExt)
{
    if (nChannelIndex < 0 || nChannelIndex >= MAX_USBCAN_CHANNELS || !sg_abLastTxValid[nChannelIndex])
    {
        return FALSE;
    }
    const STCAN_MSG& tx = sg_asLastTx[nChannelIndex];
    if (tx.m_unMsgID != unId || tx.m_ucDataLen != ucLen || tx.m_ucRTR != ucRtr || tx.m_ucEXTENDED != ucExt)
    {
        return FALSE;
    }
    if (ucLen > 0 && memcmp(tx.m_ucData, pucData, ucLen) != 0)
    {
        return FALSE;
    }
    LARGE_INTEGER lnNow;
    QueryPerformanceCounter(&lnNow);
    LONGLONG llFreq = sg_lnFrequency.QuadPart != 0 ? sg_lnFrequency.QuadPart : 1;
    if ((lnNow.QuadPart - sg_alnLastTxQpc[nChannelIndex].QuadPart) > (llFreq / 20))
    {
        return FALSE;
    }
    sg_abLastTxValid[nChannelIndex] = FALSE;
    return TRUE;
}

static void DispatchFrame(int nChannelIndex, const VCI_CAN_OBJ& obj)
{
    BYTE ucLen = obj.DataLen > 8 ? 8 : obj.DataLen;
    if (IsTxEcho(nChannelIndex, obj.ID, obj.Data, ucLen, obj.RemoteFlag ? 1 : 0, obj.ExternFlag ? 1 : 0))
    {
        return;
    }
    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = RX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_unMsgID = obj.ID;
    can_data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = obj.ExternFlag ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucRTR = obj.RemoteFlag ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen = ucLen;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nChannelIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = false;
    memcpy(can_data.m_uDataInfo.m_sCANMsg.m_ucData, obj.Data, ucLen);
    if (obj.TimeFlag)
    {
        can_data.m_lTickCount.QuadPart = (LONGLONG)obj.TimeStamp;
    }
    else
    {
        LARGE_INTEGER lnNow;
        QueryPerformanceCounter(&lnNow);
        LONGLONG llFreq = sg_lnFrequency.QuadPart != 0 ? sg_lnFrequency.QuadPart : 1;
        can_data.m_lTickCount.QuadPart = ((lnNow.QuadPart - sg_QueryTickCount.QuadPart) * 10000) / llFreq;
    }
    vWriteIntoClientsBuffer(can_data);
}

static DWORD WINAPI UsbCanRxThread(LPVOID)
{
    VCI_CAN_OBJ asObj[64];
    while (sg_bRxRun)
    {
        BOOL bGot = FALSE;
        for (INT i = 0; i < sg_nNoOfChannels && sg_bRxRun; i++)
        {
            if (sg_Receive == nullptr)
            {
                continue;
            }
            ULONG n = sg_Receive(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i], asObj, 64, 0);
            if (n == 0 || n == 0xFFFFFFFF)
            {
                continue;
            }
            if (n > 64)
            {
                n = 64;
            }
            bGot = TRUE;
            EnterCriticalSection(&sg_DIL_CriticalSection);
            for (ULONG k = 0; k < n; k++)
            {
                DispatchFrame(i, asObj[k]);
            }
            LeaveCriticalSection(&sg_DIL_CriticalSection);
        }
        if (!bGot)
        {
            Sleep(2);
        }
    }
    return 0;
}

static BOOL DeviceAlready(const DWORD* adwType, const DWORD* adwIndex, int nCount, DWORD dwType, DWORD dwIndex)
{
    for (int i = 0; i < nCount; i++)
    {
        if (adwType[i] == dwType && adwIndex[i] == dwIndex)
        {
            return TRUE;
        }
    }
    return FALSE;
}

USAGEMODE HRESULT __cdecl GetIDIL_CAN_Controller(void** ppvInterface)
{
    HRESULT hResult = S_OK;
    if (g_pouDIL_CAN_USBCAN == nullptr)
    {
        g_pouDIL_CAN_USBCAN = new CDIL_CAN_USBCAN;
        if (g_pouDIL_CAN_USBCAN == nullptr)
        {
            hResult = S_FALSE;
        }
    }
    *ppvInterface = (void*)g_pouDIL_CAN_USBCAN;
    return hResult;
}

HRESULT CDIL_CAN_USBCAN::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0;
    sg_QueryTickCount.QuadPart = 0;
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_LoadDriverLibrary(void)
{
    if (sg_hDll != nullptr)
    {
        return DLL_ALREADY_LOADED;
    }
    sg_hDll = LoadControlCan();
    if (sg_hDll == nullptr)
    {
        sg_acErrStr = "ControlCAN.dll not found.";
        return ERR_LOAD_DRIVER;
    }
    sg_OpenDevice = (PFN_VCI_OpenDevice)GetProcAddress(sg_hDll, "VCI_OpenDevice");
    sg_CloseDevice = (PFN_VCI_CloseDevice)GetProcAddress(sg_hDll, "VCI_CloseDevice");
    sg_InitCAN = (PFN_VCI_InitCAN)GetProcAddress(sg_hDll, "VCI_InitCAN");
    sg_ReadBoardInfo = (PFN_VCI_ReadBoardInfo)GetProcAddress(sg_hDll, "VCI_ReadBoardInfo");
    sg_ClearBuffer = (PFN_VCI_ClearBuffer)GetProcAddress(sg_hDll, "VCI_ClearBuffer");
    sg_StartCAN = (PFN_VCI_StartCAN)GetProcAddress(sg_hDll, "VCI_StartCAN");
    sg_ResetCAN = (PFN_VCI_ResetCAN)GetProcAddress(sg_hDll, "VCI_ResetCAN");
    sg_Transmit = (PFN_VCI_Transmit)GetProcAddress(sg_hDll, "VCI_Transmit");
    sg_Receive = (PFN_VCI_Receive)GetProcAddress(sg_hDll, "VCI_Receive");
    sg_FindUsbDevice2 = (PFN_VCI_FindUsbDevice2)GetProcAddress(sg_hDll, "VCI_FindUsbDevice2");
    if (sg_OpenDevice == nullptr || sg_CloseDevice == nullptr || sg_InitCAN == nullptr ||
            sg_StartCAN == nullptr || sg_Transmit == nullptr || sg_Receive == nullptr)
    {
        FreeLibrary(sg_hDll);
        sg_hDll = nullptr;
        sg_acErrStr = "ControlCAN.dll is missing required exports.";
        return ERR_LOAD_DRIVER;
    }
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_UnloadDriverLibrary(void)
{
    CAN_StopHardware();
    if (sg_hDll != nullptr)
    {
        FreeLibrary(sg_hDll);
        sg_hDll = nullptr;
    }
    sg_OpenDevice = nullptr;
    sg_CloseDevice = nullptr;
    sg_InitCAN = nullptr;
    sg_ReadBoardInfo = nullptr;
    sg_ClearBuffer = nullptr;
    sg_StartCAN = nullptr;
    sg_ResetCAN = nullptr;
    sg_Transmit = nullptr;
    sg_Receive = nullptr;
    sg_FindUsbDevice2 = nullptr;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_PerformInitOperations(void)
{
    if (!sg_bCsInit)
    {
        InitializeCriticalSection(&sg_DIL_CriticalSection);
        sg_bCsInit = TRUE;
    }
    QueryPerformanceFrequency(&sg_lnFrequency);
    DWORD dwClientID = 0;
    CAN_RegisterClient(TRUE, dwClientID, CAN_MONITOR_NODE);
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_PerformClosureOperations(void)
{
    CAN_StopHardware();
    while (sg_unClientCnt > 0)
    {
        bRemoveClient(sg_asClientToBufMap[0].m_dwClientID);
    }
    if (sg_bCsInit)
    {
        DeleteCriticalSection(&sg_DIL_CriticalSection);
        sg_bCsInit = FALSE;
    }
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    INTERFACE_HW_LIST asFound = {};
    INT nFound = EnumerateChannels(asFound);
    if (nFound <= 0)
    {
        nCount = 0;
        sg_acErrStr = "No USBCAN device available.";
        return S_FALSE;
    }
    if (InitData != nullptr)
    {
        for (INT i = 0; i < nFound; i++)
        {
            InitData[i].m_bcanFDEnabled = false;
            InitData[i].m_bSupportCANFD = false;
        }
    }

    int anSelList[CHANNEL_ALLOWED];
    for (int i = 0; i < CHANNEL_ALLOWED; i++)
    {
        anSelList[i] = -1;
    }
    int nRestored = 0;
    for (INT i = 0; i < sg_nNoOfChannels && nRestored < CHANNEL_ALLOWED; i++)
    {
        for (INT j = 0; j < nFound; j++)
        {
            DWORD dwType = 0;
            DWORD dwIndex = 0;
            DWORD dwCh = 0;
            DecodeHw(asFound[j], dwType, dwIndex, dwCh);
            if (dwType == sg_adwType[i] && dwIndex == sg_adwDevIndex[i] && dwCh == sg_adwCh[i])
            {
                anSelList[nRestored++] = j;
                break;
            }
        }
    }

    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd objMainWnd;
    objMainWnd.Attach(sg_hOwnerWnd);
    CHardwareListingCAN HwList(asFound, nFound, anSelList, CAN, nFound, &objMainWnd, InitData, nullptr);
    INT nRet = HwList.DoModal();
    objMainWnd.Detach();
    if (nRet != IDOK)
    {
        return HW_INTERFACE_NO_SEL;
    }
    nCount = HwList.nGetSelectedList(anSelList);
    if (nCount <= 0)
    {
        return HW_INTERFACE_NO_SEL;
    }
    if (nCount > nFound)
    {
        nCount = nFound;
    }
    for (INT i = 0; i < nCount; i++)
    {
        INT nSrc = anSelList[i];
        if (nSrc < 0 || nSrc >= nFound)
        {
            nSrc = i;
        }
        sSelHwInterface[i] = asFound[nSrc];
    }
    FillFromSelected(sSelHwInterface, nCount);
    if (InitData != nullptr)
    {
        ApplyControllerConfig(InitData, nCount);
    }
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount)
{
    if (nCount <= 0)
    {
        return S_FALSE;
    }
    FillFromSelected(sSelHwInterface, nCount);
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_DeselectHwInterface(void)
{
    CAN_StopHardware();
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    ApplyControllerConfig(InitData, Length > 0 ? Length : 1);
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS pDetails, DWORD /*dwDriverId*/, bool /*bIsHardwareListed*/, unsigned int unChannelCount)
{
    if (pDetails != nullptr)
    {
        ApplyControllerConfig(pDetails, (int)unChannelCount);
    }
    if (unChannelCount > 0 && unChannelCount <= (unsigned int)MAX_USBCAN_CHANNELS)
    {
        sg_nNoOfChannels = (INT)unChannelCount;
    }
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_StartHardware(void)
{
    if (sg_OpenDevice == nullptr || sg_nNoOfChannels <= 0)
    {
        sg_acErrStr = "No channel selected.";
        return S_FALSE;
    }
    CAN_StopHardware();

    DWORD adwOpenedType[MAX_USBCAN_CHANNELS];
    DWORD adwOpenedIndex[MAX_USBCAN_CHANNELS];
    int nOpened = 0;
    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        if (!DeviceAlready(adwOpenedType, adwOpenedIndex, nOpened, sg_adwType[i], sg_adwDevIndex[i]))
        {
            if (OpenDeviceRetry(sg_adwType[i], sg_adwDevIndex[i]) != VCI_STATUS_OK)
            {
                sg_acErrStr = "VCI_OpenDevice failed.";
                for (int n = 0; n < nOpened; n++)
                {
                    sg_CloseDevice(adwOpenedType[n], adwOpenedIndex[n]);
                }
                return S_FALSE;
            }
            adwOpenedType[nOpened] = sg_adwType[i];
            adwOpenedIndex[nOpened] = sg_adwDevIndex[i];
            nOpened++;
        }

        VCI_INIT_CONFIG sCfg;
        memset(&sCfg, 0, sizeof(sCfg));
        sCfg.AccCode = 0;
        sCfg.AccMask = 0xFFFFFFFF;
        sCfg.Filter = 1;
        TimingFromBaud(sg_aunBaud[i] ? sg_aunBaud[i] : 500000, sCfg.Timing0, sCfg.Timing1);
        sCfg.Mode = sg_abListen[i] ? 1 : 0;
        if (sg_InitCAN(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i], &sCfg) != VCI_STATUS_OK)
        {
            sg_acErrStr = "VCI_InitCAN failed.";
            for (int n = 0; n < nOpened; n++)
            {
                sg_CloseDevice(adwOpenedType[n], adwOpenedIndex[n]);
            }
            return S_FALSE;
        }
        if (sg_StartCAN(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]) != VCI_STATUS_OK)
        {
            Sleep(50);
            if (sg_StartCAN(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]) != VCI_STATUS_OK)
            {
                sg_acErrStr = "VCI_StartCAN failed.";
                if (sg_ResetCAN != nullptr)
                {
                    sg_ResetCAN(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]);
                }
                for (int n = 0; n < nOpened; n++)
                {
                    sg_CloseDevice(adwOpenedType[n], adwOpenedIndex[n]);
                }
                return S_FALSE;
            }
        }
        if (sg_ClearBuffer != nullptr)
        {
            sg_ClearBuffer(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]);
        }
        sg_abLastTxValid[i] = FALSE;
    }

    GetLocalTime(&sg_CurrSysTime);
    QueryPerformanceFrequency(&sg_lnFrequency);
    QueryPerformanceCounter(&sg_QueryTickCount);
    sg_TimeStamp = 0;
    sg_bHwOpen = TRUE;
    sg_bRxRun = TRUE;
    sg_hRxThread = CreateThread(nullptr, 0, UsbCanRxThread, nullptr, 0, nullptr);
    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_StopHardware(void)
{
    sg_bRxRun = FALSE;
    if (sg_hRxThread != nullptr)
    {
        WaitForSingleObject(sg_hRxThread, 1000);
        CloseHandle(sg_hRxThread);
        sg_hRxThread = nullptr;
    }
    if (sg_bHwOpen && sg_CloseDevice != nullptr)
    {
        DWORD adwOpenedType[MAX_USBCAN_CHANNELS];
        DWORD adwOpenedIndex[MAX_USBCAN_CHANNELS];
        int nOpened = 0;
        for (INT i = 0; i < sg_nNoOfChannels; i++)
        {
            if (sg_ResetCAN != nullptr)
            {
                sg_ResetCAN(sg_adwType[i], sg_adwDevIndex[i], sg_adwCh[i]);
            }
            if (!DeviceAlready(adwOpenedType, adwOpenedIndex, nOpened, sg_adwType[i], sg_adwDevIndex[i]))
            {
                adwOpenedType[nOpened] = sg_adwType[i];
                adwOpenedIndex[nOpened] = sg_adwDevIndex[i];
                nOpened++;
            }
        }
        for (int n = 0; n < nOpened; n++)
        {
            sg_CloseDevice(adwOpenedType[n], adwOpenedIndex[n]);
        }
        sg_bHwOpen = FALSE;
    }
    if (sg_bCurrState == STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    if (sg_bCurrState != STATE_CONNECTED || sg_Transmit == nullptr)
    {
        return S_FALSE;
    }
    if (!bClientIdExist(dwClientID))
    {
        return ERR_NO_CLIENT_EXIST;
    }
    if (sCanTxMsg.m_bCANFD || sCanTxMsg.m_ucDataLen > 8)
    {
        sg_acErrStr = "USBCAN supports classic CAN frames up to 8 bytes.";
        return S_FALSE;
    }
    INT nIndex = (INT)sCanTxMsg.m_ucChannel - 1;
    if (nIndex < 0)
    {
        nIndex = 0;
    }
    if (nIndex >= sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }

    VCI_CAN_OBJ obj;
    memset(&obj, 0, sizeof(obj));
    obj.ID = sCanTxMsg.m_unMsgID;
    obj.SendType = 0;
    obj.RemoteFlag = sCanTxMsg.m_ucRTR ? 1 : 0;
    obj.ExternFlag = sCanTxMsg.m_ucEXTENDED ? 1 : 0;
    obj.DataLen = sCanTxMsg.m_ucDataLen;
    memcpy(obj.Data, sCanTxMsg.m_ucData, obj.DataLen);
    ULONG nSent = sg_Transmit(sg_adwType[nIndex], sg_adwDevIndex[nIndex], sg_adwCh[nIndex], &obj, 1);
    if (nSent != 1)
    {
        sg_acErrStr = "VCI_Transmit failed.";
        return S_FALSE;
    }

    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = TX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = false;
    QueryPerformanceCounter(&can_data.m_lTickCount);
    if (nIndex >= 0 && nIndex < MAX_USBCAN_CHANNELS)
    {
        sg_asLastTx[nIndex] = can_data.m_uDataInfo.m_sCANMsg;
        sg_abLastTxValid[nIndex] = TRUE;
        sg_alnLastTxQpc[nIndex] = can_data.m_lTickCount;
    }
    EnterCriticalSection(&sg_DIL_CriticalSection);
    vWriteIntoClientsBuffer(can_data);
    LeaveCriticalSection(&sg_DIL_CriticalSection);
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_acErrStr;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = sg_nNoOfChannels > 0 ? sg_nNoOfChannels : 1;
            break;
        case DRIVER_STATUS:
            lParam = TRUE;
            break;
        case HW_MODE:
            lParam = (nChannel < (UINT)sg_nNoOfChannels) ? defMODE_ACTIVE : (defCONTROLLER_BUSOFF + 1);
            break;
        case CON_TEST:
            lParam = TRUE;
            break;
        default:
            lParam = 0;
            break;
    }
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_SetControllerParams(int /*nValue*/, ECONTR_PARAM /*eContrparam*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT /*nChannel*/, ECONTR_PARAM /*eContrParam*/)
{
    sErrorCnt.m_ucTxErrCount = 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_USBCAN::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    HRESULT hResult = S_FALSE;
    UINT unClientIndex = 0;
    if (ClientID != 0)
    {
        if (bGetClientObj(ClientID, unClientIndex))
        {
            SCLIENTBUFMAP& sClientObj = sg_asClientToBufMap[unClientIndex];
            if (bAction == MSGBUF_ADD)
            {
                if (pBufObj != nullptr && sClientObj.m_unBufCount < MAX_BUFF_ALLOWED &&
                        bIsBufferExists(sClientObj, pBufObj) == FALSE)
                {
                    sClientObj.m_pClientBuf[sClientObj.m_unBufCount++] = pBufObj;
                    hResult = S_OK;
                }
                else if (pBufObj != nullptr)
                {
                    hResult = ERR_BUFFER_EXISTS;
                }
            }
            else if (bAction == MSGBUF_CLEAR)
            {
                if (pBufObj != nullptr)
                {
                    bRemoveClientBuffer(sClientObj.m_pClientBuf, sClientObj.m_unBufCount, pBufObj);
                }
                else
                {
                    sClientObj.m_unBufCount = 0;
                }
                hResult = S_OK;
            }
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    else if (bAction == MSGBUF_CLEAR)
    {
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            sg_asClientToBufMap[i].m_unBufCount = 0;
        }
        hResult = S_OK;
    }
    return hResult;
}

HRESULT CDIL_CAN_USBCAN::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    if (bRegister)
    {
        if (sg_unClientCnt >= MAX_CLIENT_ALLOWED)
        {
            return ERR_NO_MORE_CLIENT_ALLOWED;
        }
        INT Index = 0;
        if (bClientExist(pacClientName, Index))
        {
            ClientID = sg_asClientToBufMap[Index].m_dwClientID;
            return ERR_CLIENT_EXISTS;
        }
        if (_tcscmp(pacClientName, CAN_MONITOR_NODE) == 0)
        {
            ClientID = 1;
            _tcscpy_s(sg_asClientToBufMap[0].m_acClientName, pacClientName);
            sg_asClientToBufMap[0].m_dwClientID = ClientID;
            sg_asClientToBufMap[0].m_unBufCount = 0;
        }
        else
        {
            Index = (INT)sg_unClientCnt;
            ClientID = dwGetAvailableClientSlot();
            _tcscpy_s(sg_asClientToBufMap[Index].m_acClientName, pacClientName);
            sg_asClientToBufMap[Index].m_dwClientID = ClientID;
            sg_asClientToBufMap[Index].m_unBufCount = 0;
        }
        sg_unClientCnt++;
        return S_OK;
    }
    return bRemoveClient(ClientID) ? S_OK : ERR_NO_CLIENT_EXIST;
}
