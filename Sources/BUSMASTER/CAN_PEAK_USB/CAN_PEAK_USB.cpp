#include "CAN_PEAK_USB_stdafx.h"
#include "CAN_PEAK_USB.h"
#include "PCANBasicLite.h"

#include <string>
#include <vector>

#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "Error.h"
#include "DIL_Interface/HardwareListingCAN.h"
#include "DIL_Interface/IChangeRegisters.h"
#include "ChangeRegisters.h"

#define USAGE_EXPORT
#include "CAN_PEAK_USB_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_PEAK_USBApp, CWinApp)
END_MESSAGE_MAP()

CCAN_PEAK_USBApp::CCAN_PEAK_USBApp()
{
}

CCAN_PEAK_USBApp theApp;

BOOL CCAN_PEAK_USBApp::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

#define MAX_CLIENT_ALLOWED  16
#define MAX_BUFF_ALLOWED    16
#define MAX_PEAK_CHANNELS   16

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
static TPCANHandle sg_anHandles[MAX_PEAK_CHANNELS] = {0};
static HANDLE sg_ahRxEvent[MAX_PEAK_CHANNELS] = {0};
static HANDLE sg_hRxThread = nullptr;
static volatile BOOL sg_bRxRun = FALSE;
static TPCANBaudrate sg_Baud = PCAN_BAUD_500K;
static BOOL sg_abCanFd[MAX_PEAK_CHANNELS] = {0};
static unsigned int sg_aunDataBitRate[MAX_PEAK_CHANNELS] = {0};
static STCAN_MSG sg_asLastTx[MAX_PEAK_CHANNELS];
static BOOL sg_abLastTxValid[MAX_PEAK_CHANNELS] = {0};
static LARGE_INTEGER sg_alnLastTxQpc[MAX_PEAK_CHANNELS] = {0};

static void RememberTx(int nChannelIndex, const STCAN_MSG& sMsg)
{
    if (nChannelIndex < 0 || nChannelIndex >= MAX_PEAK_CHANNELS)
    {
        return;
    }
    sg_asLastTx[nChannelIndex] = sMsg;
    sg_abLastTxValid[nChannelIndex] = TRUE;
    QueryPerformanceCounter(&sg_alnLastTxQpc[nChannelIndex]);
}

static BOOL IsTxEchoFrame(int nChannelIndex, UINT unId, const BYTE* pucData, BYTE ucLen, BYTE ucMsgType)
{
    if ((ucMsgType & PCAN_MESSAGE_ECHO) != 0)
    {
        if (nChannelIndex >= 0 && nChannelIndex < MAX_PEAK_CHANNELS)
        {
            sg_abLastTxValid[nChannelIndex] = FALSE;
        }
        return TRUE;
    }
    if (nChannelIndex < 0 || nChannelIndex >= MAX_PEAK_CHANNELS || !sg_abLastTxValid[nChannelIndex])
    {
        return FALSE;
    }
    const STCAN_MSG& tx = sg_asLastTx[nChannelIndex];
    if (tx.m_unMsgID != unId || tx.m_ucDataLen != ucLen)
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

static HMODULE sg_hPcan = nullptr;
static PFN_CAN_Initialize sg_CAN_Initialize = nullptr;
static PFN_CAN_InitializeFD sg_CAN_InitializeFD = nullptr;
static PFN_CAN_Uninitialize sg_CAN_Uninitialize = nullptr;
static PFN_CAN_Read sg_CAN_Read = nullptr;
static PFN_CAN_ReadFD sg_CAN_ReadFD = nullptr;
static PFN_CAN_Write sg_CAN_Write = nullptr;
static PFN_CAN_WriteFD sg_CAN_WriteFD = nullptr;
static PFN_CAN_GetValue sg_CAN_GetValue = nullptr;
static PFN_CAN_SetValue sg_CAN_SetValue = nullptr;
static PFN_CAN_GetErrorText sg_CAN_GetErrorText = nullptr;

static const TPCANHandle sg_UsbHandles[] =
{
    PCAN_USBBUS1, PCAN_USBBUS2, PCAN_USBBUS3, PCAN_USBBUS4,
    PCAN_USBBUS5, PCAN_USBBUS6, PCAN_USBBUS7, PCAN_USBBUS8,
    PCAN_USBBUS9, PCAN_USBBUS10, PCAN_USBBUS11, PCAN_USBBUS12,
    PCAN_USBBUS13, PCAN_USBBUS14, PCAN_USBBUS15, PCAN_USBBUS16
};

class CDIL_CAN_PEAK_USB : public CBaseDIL_CAN_Controller
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

static CDIL_CAN_PEAK_USB* g_pouDIL_CAN_PEAK_USB = nullptr;

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

static BOOL bClientExist(std::string pcClientName, INT& Index)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (!_tcscmp(pcClientName.c_str(), sg_asClientToBufMap[i].m_acClientName))
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
    memset(sg_asClientToBufMap[unClientIndex].m_acClientName, 0, sizeof(char) * MAX_PATH);
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

static TPCANBaudrate BaudFromController(PSCONTROLLER_DETAILS pDetails)
{
    if (pDetails == nullptr || pDetails->m_omStrBaudrate.empty())
    {
        return PCAN_BAUD_500K;
    }
    int nBaud = atoi(pDetails->m_omStrBaudrate.c_str());
    if (nBaud >= 1000000) { return PCAN_BAUD_1M; }
    if (nBaud >= 800000) { return PCAN_BAUD_800K; }
    if (nBaud >= 500000) { return PCAN_BAUD_500K; }
    if (nBaud >= 250000) { return PCAN_BAUD_250K; }
    if (nBaud >= 125000) { return PCAN_BAUD_125K; }
    if (nBaud >= 100000) { return PCAN_BAUD_100K; }
    if (nBaud >= 50000) { return PCAN_BAUD_50K; }
    return PCAN_BAUD_500K;
}

static void ApplyControllerConfig(PSCONTROLLER_DETAILS pDetails, int nCount)
{
    if (pDetails == nullptr)
    {
        return;
    }
    sg_Baud = BaudFromController(pDetails);
    INT nLim = nCount;
    if (nLim <= 0)
    {
        nLim = 1;
    }
    if (nLim > MAX_PEAK_CHANNELS)
    {
        nLim = MAX_PEAK_CHANNELS;
    }
    for (INT i = 0; i < nLim; i++)
    {
        sg_abCanFd[i] = pDetails[i].m_bcanFDEnabled ? TRUE : FALSE;
        sg_aunDataBitRate[i] = pDetails[i].m_unDataBitRate ? pDetails[i].m_unDataBitRate : 2000000;
    }
}

static unsigned int NominalFromController(PSCONTROLLER_DETAILS pDetails)
{
    if (pDetails == nullptr || pDetails->m_omStrBaudrate.empty())
    {
        return 500000;
    }
    unsigned int unBaud = (unsigned int)atoi(pDetails->m_omStrBaudrate.c_str());
    return unBaud ? unBaud : 500000;
}

static void BuildFdBitrate(unsigned int unNom, unsigned int unData, char* acOut, size_t nOut)
{
    struct sRate
    {
        unsigned int unRate;
        int nBrp;
        int nTseg1;
        int nTseg2;
    };
    const sRate asNom[] =
    {
        {1000000, 10, 5, 2},
        {800000, 10, 7, 2},
        {500000, 10, 12, 3},
        {250000, 10, 25, 6},
        {125000, 20, 25, 6},
        {100000, 40, 16, 3},
        {50000, 40, 32, 7}
    };
    const sRate asData[] =
    {
        {8000000, 2, 3, 1},
        {5000000, 2, 5, 2},
        {4000000, 2, 7, 2},
        {2000000, 4, 7, 2},
        {1000000, 4, 15, 4}
    };
    sRate nom = asNom[2];
    sRate data = asData[3];
    for (int i = 0; i < (int)(sizeof(asNom) / sizeof(asNom[0])); i++)
    {
        if (unNom >= asNom[i].unRate)
        {
            nom = asNom[i];
            break;
        }
    }
    for (int i = 0; i < (int)(sizeof(asData) / sizeof(asData[0])); i++)
    {
        if (unData >= asData[i].unRate)
        {
            data = asData[i];
            break;
        }
    }
    sprintf_s(acOut, nOut,
              "f_clock_mhz=80, nom_brp=%d, nom_tseg1=%d, nom_tseg2=%d, nom_sjw=1, data_brp=%d, data_tseg1=%d, data_tseg2=%d, data_sjw=1",
              nom.nBrp, nom.nTseg1, nom.nTseg2, data.nBrp, data.nTseg1, data.nTseg2);
}

static INT EnumeratePeakChannels(INTERFACE_HW_LIST& asList)
{
    INT nFound = 0;
    if (sg_CAN_GetValue == nullptr)
    {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(sg_UsbHandles) / sizeof(sg_UsbHandles[0])); i++)
    {
        DWORD dwCond = 0;
        TPCANStatus st = sg_CAN_GetValue(sg_UsbHandles[i], PCAN_CHANNEL_CONDITION, &dwCond, sizeof(dwCond));
        if (st != PCAN_ERROR_OK)
        {
            continue;
        }
        if (dwCond != PCAN_CHANNEL_AVAILABLE && dwCond != PCAN_CHANNEL_PCANVIEW)
        {
            continue;
        }
        char acName[64] = {0};
        sg_CAN_GetValue(sg_UsbHandles[i], PCAN_HARDWARE_NAME, acName, sizeof(acName));
        DWORD dwDevId = 0;
        if (sg_CAN_GetValue(sg_UsbHandles[i], PCAN_DEVICE_ID, &dwDevId, sizeof(dwDevId)) != PCAN_ERROR_OK || dwDevId == 0)
        {
            sg_CAN_GetValue(sg_UsbHandles[i], PCAN_DEVICE_NUMBER, &dwDevId, sizeof(dwDevId));
        }
        char acDesc[64] = {0};
        if (dwDevId != 0)
        {
            sprintf_s(acDesc, "PCAN-USB Driver Id %u", dwDevId);
        }
        else
        {
            sprintf_s(acDesc, "PCAN USB 0x%X", sg_UsbHandles[i]);
        }
        asList[nFound].m_dwIdInterface = (dwDevId != 0) ? dwDevId : sg_UsbHandles[i];
        asList[nFound].m_dwVendor = sg_UsbHandles[i];
        asList[nFound].m_bytNetworkID = (unsigned char)nFound;
        asList[nFound].m_acNameInterface = acDesc;
        asList[nFound].m_acDescription = acDesc;
        char acFw[64] = {0};
        sg_CAN_GetValue(sg_UsbHandles[i], PCAN_CHANNEL_VERSION, acFw, sizeof(acFw));
        asList[nFound].m_acDeviceName = acFw[0] ? acFw : (acName[0] ? acName : "PCAN-USB");
        nFound++;
        if (nFound >= (INT)defCHANNEL_CAN_MAX)
        {
            break;
        }
    }
    return nFound;
}

static BYTE LenFromDlc(BYTE dlc)
{
    static const BYTE s_aucLen[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return (dlc < 16) ? s_aucLen[dlc] : 64;
}

static BYTE DlcFromLen(BYTE len)
{
    if (len <= 8) { return len; }
    if (len <= 12) { return 9; }
    if (len <= 16) { return 10; }
    if (len <= 20) { return 11; }
    if (len <= 24) { return 12; }
    if (len <= 32) { return 13; }
    if (len <= 48) { return 14; }
    return 15;
}

static void DispatchPcanMessageFD(int nChannelIndex, const TPCANMsgFD& msg, TPCANTimestampFD ts)
{
    if (msg.MSGTYPE & (PCAN_MESSAGE_STATUS | PCAN_MESSAGE_ERRFRAME))
    {
        return;
    }
    BYTE ucLen = LenFromDlc(msg.DLC);
    if (IsTxEchoFrame(nChannelIndex, msg.ID, msg.DATA, ucLen, msg.MSGTYPE))
    {
        return;
    }
    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = RX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_unMsgID = msg.ID;
    can_data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucRTR = (msg.MSGTYPE & PCAN_MESSAGE_RTR) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen = ucLen;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nChannelIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = (msg.MSGTYPE & PCAN_MESSAGE_FD) ? true : false;
    memcpy(can_data.m_uDataInfo.m_sCANMsg.m_ucData, msg.DATA, ucLen);
    can_data.m_lTickCount.QuadPart = (LONGLONG)(ts / 100ULL);
    vWriteIntoClientsBuffer(can_data);
}

static void DispatchPcanMessage(int nChannelIndex, const TPCANMsg& msg, const TPCANTimestamp& ts)
{
    if (msg.MSGTYPE & (PCAN_MESSAGE_STATUS | PCAN_MESSAGE_ERRFRAME))
    {
        return;
    }
    BYTE ucLen = msg.LEN > 8 ? 8 : msg.LEN;
    if (IsTxEchoFrame(nChannelIndex, msg.ID, msg.DATA, ucLen, msg.MSGTYPE))
    {
        return;
    }
    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = RX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_unMsgID = msg.ID;
    can_data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucRTR = (msg.MSGTYPE & PCAN_MESSAGE_RTR) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen = ucLen;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nChannelIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = false;
    memcpy(can_data.m_uDataInfo.m_sCANMsg.m_ucData, msg.DATA, can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen);
    can_data.m_lTickCount.QuadPart = ((UINT64)ts.millis * 10ULL) + (ts.micros / 100ULL);
    vWriteIntoClientsBuffer(can_data);
}

static DWORD WINAPI PeakRxThread(LPVOID)
{
    while (sg_bRxRun)
    {
        HANDLE ahWait[MAX_PEAK_CHANNELS];
        INT nWait = 0;
        INT anMap[MAX_PEAK_CHANNELS];
        for (INT i = 0; i < sg_nNoOfChannels; i++)
        {
            if (sg_ahRxEvent[i] != nullptr)
            {
                ahWait[nWait] = sg_ahRxEvent[i];
                anMap[nWait] = i;
                nWait++;
            }
        }
        if (nWait == 0)
        {
            Sleep(10);
            continue;
        }
        DWORD dw = WaitForMultipleObjects((DWORD)nWait, ahWait, FALSE, 50);
        if (dw < WAIT_OBJECT_0 || dw >= WAIT_OBJECT_0 + (DWORD)nWait)
        {
            continue;
        }
        INT nIndex = anMap[dw - WAIT_OBJECT_0];
        for (;;)
        {
            if (sg_abCanFd[nIndex] && sg_CAN_ReadFD != nullptr)
            {
                TPCANMsgFD msgFd;
                TPCANTimestampFD tsFd = 0;
                memset(&msgFd, 0, sizeof(msgFd));
                TPCANStatus st = sg_CAN_ReadFD(sg_anHandles[nIndex], &msgFd, &tsFd);
                if (st == PCAN_ERROR_QRCVEMPTY || st != PCAN_ERROR_OK)
                {
                    break;
                }
                EnterCriticalSection(&sg_DIL_CriticalSection);
                DispatchPcanMessageFD(nIndex, msgFd, tsFd);
                LeaveCriticalSection(&sg_DIL_CriticalSection);
            }
            else if (sg_CAN_Read != nullptr)
            {
                TPCANMsg msg;
                TPCANTimestamp ts;
                memset(&msg, 0, sizeof(msg));
                memset(&ts, 0, sizeof(ts));
                TPCANStatus st = sg_CAN_Read(sg_anHandles[nIndex], &msg, &ts);
                if (st == PCAN_ERROR_QRCVEMPTY || st != PCAN_ERROR_OK)
                {
                    break;
                }
                EnterCriticalSection(&sg_DIL_CriticalSection);
                DispatchPcanMessage(nIndex, msg, ts);
                LeaveCriticalSection(&sg_DIL_CriticalSection);
            }
            else
            {
                break;
            }
        }
    }
    return 0;
}

USAGEMODE HRESULT __cdecl GetIDIL_CAN_Controller(void** ppvInterface)
{
    HRESULT hResult = S_OK;
    if (g_pouDIL_CAN_PEAK_USB == nullptr)
    {
        g_pouDIL_CAN_PEAK_USB = new CDIL_CAN_PEAK_USB;
        if (g_pouDIL_CAN_PEAK_USB == nullptr)
        {
            hResult = S_FALSE;
        }
    }
    *ppvInterface = (void*)g_pouDIL_CAN_PEAK_USB;
    return hResult;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0;
    sg_QueryTickCount.QuadPart = 0;
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_LoadDriverLibrary(void)
{
    if (sg_hPcan != nullptr)
    {
        return DLL_ALREADY_LOADED;
    }
    sg_hPcan = LoadLibraryA("PCANBasic.dll");
    if (sg_hPcan == nullptr)
    {
        sg_acErrStr = "PCANBasic.dll not found. Install PEAK drivers.";
        return ERR_LOAD_DRIVER;
    }
    sg_CAN_Initialize = (PFN_CAN_Initialize)GetProcAddress(sg_hPcan, "CAN_Initialize");
    sg_CAN_InitializeFD = (PFN_CAN_InitializeFD)GetProcAddress(sg_hPcan, "CAN_InitializeFD");
    sg_CAN_Uninitialize = (PFN_CAN_Uninitialize)GetProcAddress(sg_hPcan, "CAN_Uninitialize");
    sg_CAN_Read = (PFN_CAN_Read)GetProcAddress(sg_hPcan, "CAN_Read");
    sg_CAN_ReadFD = (PFN_CAN_ReadFD)GetProcAddress(sg_hPcan, "CAN_ReadFD");
    sg_CAN_Write = (PFN_CAN_Write)GetProcAddress(sg_hPcan, "CAN_Write");
    sg_CAN_WriteFD = (PFN_CAN_WriteFD)GetProcAddress(sg_hPcan, "CAN_WriteFD");
    sg_CAN_GetValue = (PFN_CAN_GetValue)GetProcAddress(sg_hPcan, "CAN_GetValue");
    sg_CAN_SetValue = (PFN_CAN_SetValue)GetProcAddress(sg_hPcan, "CAN_SetValue");
    sg_CAN_GetErrorText = (PFN_CAN_GetErrorText)GetProcAddress(sg_hPcan, "CAN_GetErrorText");
    if (sg_CAN_Initialize == nullptr || sg_CAN_Uninitialize == nullptr ||
            sg_CAN_Read == nullptr || sg_CAN_Write == nullptr || sg_CAN_GetValue == nullptr)
    {
        FreeLibrary(sg_hPcan);
        sg_hPcan = nullptr;
        sg_acErrStr = "PCANBasic.dll is missing required exports.";
        return ERR_LOAD_DRIVER;
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_UnloadDriverLibrary(void)
{
    CAN_StopHardware();
    if (sg_hPcan != nullptr)
    {
        FreeLibrary(sg_hPcan);
        sg_hPcan = nullptr;
    }
    sg_CAN_Initialize = nullptr;
    sg_CAN_InitializeFD = nullptr;
    sg_CAN_Uninitialize = nullptr;
    sg_CAN_Read = nullptr;
    sg_CAN_ReadFD = nullptr;
    sg_CAN_Write = nullptr;
    sg_CAN_WriteFD = nullptr;
    sg_CAN_GetValue = nullptr;
    sg_CAN_SetValue = nullptr;
    sg_CAN_GetErrorText = nullptr;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_PerformInitOperations(void)
{
    if (!sg_bCsInit)
    {
        InitializeCriticalSection(&sg_DIL_CriticalSection);
        sg_bCsInit = TRUE;
    }
    DWORD dwClientID = 0;
    CAN_RegisterClient(TRUE, dwClientID, CAN_MONITOR_NODE);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_PerformClosureOperations(void)
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

HRESULT CDIL_CAN_PEAK_USB::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    INTERFACE_HW_LIST asFound = {};
    INT nFound = EnumeratePeakChannels(asFound);
    if (nFound <= 0)
    {
        nCount = 0;
        sg_acErrStr = "No PCAN-USB channel available.";
        return S_FALSE;
    }

    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    int anSelList[CHANNEL_ALLOWED] = {0};
    CWnd objMainWnd;
    objMainWnd.Attach(sg_hOwnerWnd);
    IChangeRegisters* pAdvancedSettings = new CChangeRegisters(nullptr, InitData, nFound);
    CHardwareListingCAN HwList(asFound, nFound, anSelList, CAN, CHANNEL_ALLOWED, &objMainWnd, InitData, pAdvancedSettings);
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

    sg_nNoOfChannels = nCount;
    for (INT i = 0; i < nCount; i++)
    {
        INT nSrc = anSelList[i];
        if (nSrc < 0 || nSrc >= nFound)
        {
            nSrc = i;
        }
        sSelHwInterface[i] = asFound[nSrc];
        TPCANHandle hCh = (TPCANHandle)sSelHwInterface[i].m_dwVendor;
        if (hCh == 0)
        {
            hCh = (TPCANHandle)sSelHwInterface[i].m_dwIdInterface;
        }
        sg_anHandles[i] = hCh;
    }
    if (InitData != nullptr)
    {
        ApplyControllerConfig(InitData, nCount);
    }
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount)
{
    if (nCount <= 0)
    {
        return S_FALSE;
    }
    sg_nNoOfChannels = nCount > MAX_PEAK_CHANNELS ? MAX_PEAK_CHANNELS : nCount;
    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        TPCANHandle hCh = (TPCANHandle)sSelHwInterface[i].m_dwVendor;
        if (hCh == 0)
        {
            hCh = (TPCANHandle)sSelHwInterface[i].m_dwIdInterface;
        }
        sg_anHandles[i] = hCh;
    }
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_DeselectHwInterface(void)
{
    CAN_StopHardware();
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    ApplyControllerConfig(InitData, Length > 0 ? Length : 1);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS pDetails, DWORD /*dwDriverId*/, bool /*bIsHardwareListed*/, unsigned int unChannelCount)
{
    if (pDetails != nullptr)
    {
        ApplyControllerConfig(pDetails, (int)unChannelCount);
    }
    if (unChannelCount > 0 && unChannelCount <= (unsigned int)MAX_PEAK_CHANNELS)
    {
        sg_nNoOfChannels = (INT)unChannelCount;
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_StartHardware(void)
{
    if (sg_CAN_Initialize == nullptr || sg_nNoOfChannels <= 0)
    {
        sg_acErrStr = "No channel selected.";
        return S_FALSE;
    }

    CAN_StopHardware();

    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        TPCANStatus st;
        if (sg_abCanFd[i] && sg_CAN_InitializeFD != nullptr)
        {
            char acFd[256] = {0};
            unsigned int unNom = 500000;
            if (sg_Baud == PCAN_BAUD_1M) { unNom = 1000000; }
            else if (sg_Baud == PCAN_BAUD_800K) { unNom = 800000; }
            else if (sg_Baud == PCAN_BAUD_500K) { unNom = 500000; }
            else if (sg_Baud == PCAN_BAUD_250K) { unNom = 250000; }
            else if (sg_Baud == PCAN_BAUD_125K) { unNom = 125000; }
            else if (sg_Baud == PCAN_BAUD_100K) { unNom = 100000; }
            else if (sg_Baud == PCAN_BAUD_50K) { unNom = 50000; }
            BuildFdBitrate(unNom, sg_aunDataBitRate[i] ? sg_aunDataBitRate[i] : 2000000, acFd, sizeof(acFd));
            st = sg_CAN_InitializeFD(sg_anHandles[i], acFd);
        }
        else
        {
            st = sg_CAN_Initialize(sg_anHandles[i], sg_Baud, 0, 0, 0);
            if (st == PCAN_ERROR_ILLOPERATION && sg_CAN_InitializeFD != nullptr)
            {
                char acFd[256] = {0};
                BuildFdBitrate(500000, 2000000, acFd, sizeof(acFd));
                st = sg_CAN_InitializeFD(sg_anHandles[i], acFd);
                sg_abCanFd[i] = TRUE;
            }
        }
        if (st != PCAN_ERROR_OK)
        {
            char acText[256] = {0};
            if (sg_CAN_GetErrorText != nullptr)
            {
                sg_CAN_GetErrorText(st, 0, acText);
            }
            sg_acErrStr = acText[0] ? acText : "CAN_Initialize failed";
            for (INT j = 0; j < i; j++)
            {
                sg_CAN_Uninitialize(sg_anHandles[j]);
            }
            return S_FALSE;
        }
        sg_ahRxEvent[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (sg_CAN_SetValue != nullptr && sg_ahRxEvent[i] != nullptr)
        {
            sg_CAN_SetValue(sg_anHandles[i], PCAN_RECEIVE_EVENT, &sg_ahRxEvent[i], sizeof(sg_ahRxEvent[i]));
        }
        if (sg_CAN_SetValue != nullptr)
        {
            DWORD dwEchoOff = PCAN_PARAMETER_OFF;
            sg_CAN_SetValue(sg_anHandles[i], PCAN_ALLOW_ECHO_FRAMES, &dwEchoOff, sizeof(dwEchoOff));
        }
    }

    GetLocalTime(&sg_CurrSysTime);
    QueryPerformanceFrequency(&sg_lnFrequency);
    QueryPerformanceCounter(&sg_QueryTickCount);
    sg_TimeStamp = 0;
    sg_bRxRun = TRUE;
    sg_hRxThread = CreateThread(nullptr, 0, PeakRxThread, nullptr, 0, nullptr);
    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_StopHardware(void)
{
    sg_bRxRun = FALSE;
    if (sg_hRxThread != nullptr)
    {
        WaitForSingleObject(sg_hRxThread, 1000);
        CloseHandle(sg_hRxThread);
        sg_hRxThread = nullptr;
    }
    for (INT i = 0; i < MAX_PEAK_CHANNELS; i++)
    {
        if (sg_CAN_Uninitialize != nullptr && sg_anHandles[i] != 0)
        {
            sg_CAN_Uninitialize(sg_anHandles[i]);
        }
        if (sg_ahRxEvent[i] != nullptr)
        {
            CloseHandle(sg_ahRxEvent[i]);
            sg_ahRxEvent[i] = nullptr;
        }
    }
    if (sg_bCurrState == STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    if (sg_bCurrState != STATE_CONNECTED || sg_CAN_Write == nullptr)
    {
        return S_FALSE;
    }
    if (!bClientIdExist(dwClientID))
    {
        return ERR_NO_CLIENT_EXIST;
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

    TPCANStatus st;
    if (sg_abCanFd[nIndex] && sg_CAN_WriteFD != nullptr)
    {
        TPCANMsgFD msgFd;
        memset(&msgFd, 0, sizeof(msgFd));
        msgFd.ID = sCanTxMsg.m_unMsgID;
        msgFd.DLC = DlcFromLen(sCanTxMsg.m_ucDataLen);
        if (sCanTxMsg.m_ucEXTENDED) { msgFd.MSGTYPE |= PCAN_MESSAGE_EXTENDED; }
        if (sCanTxMsg.m_ucRTR) { msgFd.MSGTYPE |= PCAN_MESSAGE_RTR; }
        if (sCanTxMsg.m_bCANFD) { msgFd.MSGTYPE |= PCAN_MESSAGE_FD | PCAN_MESSAGE_BRS; }
        BYTE ucCopy = sCanTxMsg.m_ucDataLen > 64 ? 64 : sCanTxMsg.m_ucDataLen;
        memcpy(msgFd.DATA, sCanTxMsg.m_ucData, ucCopy);
        st = sg_CAN_WriteFD(sg_anHandles[nIndex], &msgFd);
    }
    else
    {
        TPCANMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.ID = sCanTxMsg.m_unMsgID;
        msg.LEN = sCanTxMsg.m_ucDataLen > 8 ? 8 : sCanTxMsg.m_ucDataLen;
        msg.MSGTYPE = 0;
        if (sCanTxMsg.m_ucEXTENDED) { msg.MSGTYPE |= PCAN_MESSAGE_EXTENDED; }
        if (sCanTxMsg.m_ucRTR) { msg.MSGTYPE |= PCAN_MESSAGE_RTR; }
        memcpy(msg.DATA, sCanTxMsg.m_ucData, msg.LEN);
        st = sg_CAN_Write(sg_anHandles[nIndex], &msg);
    }
    if (st != PCAN_ERROR_OK)
    {
        return S_FALSE;
    }

    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = TX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nIndex + 1);
    QueryPerformanceCounter(&can_data.m_lTickCount);
    RememberTx(nIndex, can_data.m_uDataInfo.m_sCANMsg);
    EnterCriticalSection(&sg_DIL_CriticalSection);
    vWriteIntoClientsBuffer(can_data);
    LeaveCriticalSection(&sg_DIL_CriticalSection);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_acErrStr;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
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

HRESULT CDIL_CAN_PEAK_USB::CAN_SetControllerParams(int /*nValue*/, ECONTR_PARAM /*eContrparam*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT /*nChannel*/, ECONTR_PARAM /*eContrParam*/)
{
    sErrorCnt.m_ucTxErrCount = 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
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

HRESULT CDIL_CAN_PEAK_USB::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
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
