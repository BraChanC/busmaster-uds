#include "CAN_ZLG_USB_stdafx.h"
#include "CAN_ZLG_USB.h"
#include "ZlgCanLite.h"

#include <string>
#include <vector>
#include <algorithm>
#include <math.h>

#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "Error.h"
#include "DIL_Interface/HardwareListingCAN.h"
#include "DIL_Interface/IChangeRegisters.h"
#include "resource.h"

#define USAGE_EXPORT
#include "CAN_ZLG_USB_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_ZLG_USBApp, CWinApp)
END_MESSAGE_MAP()

CCAN_ZLG_USBApp::CCAN_ZLG_USBApp()
{
}

CCAN_ZLG_USBApp theApp;

BOOL CCAN_ZLG_USBApp::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

#define MAX_CLIENT_ALLOWED  16
#define MAX_BUFF_ALLOWED    16
#define MAX_ZLG_CHANNELS    16

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

struct SZLG_USB_TYPE
{
    UINT unType;
    UINT unDefCh;
    const char* pcName;
};

static const SZLG_USB_TYPE sg_asUsbTypes[] =
{
    {ZCAN_USBCANFD_200U, 2, "USBCANFD-200U"}
};

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
static UINT sg_aunType[MAX_ZLG_CHANNELS] = {0};
static UINT sg_aunDevIndex[MAX_ZLG_CHANNELS] = {0};
static UINT sg_aunCh[MAX_ZLG_CHANNELS] = {0};
static BOOL sg_abCanFd[MAX_ZLG_CHANNELS] = {0};
static unsigned int sg_aunNomBitRate[MAX_ZLG_CHANNELS] = {0};
static unsigned int sg_aunDataBitRate[MAX_ZLG_CHANNELS] = {0};
static BOOL sg_abTerm[MAX_ZLG_CHANNELS];
static BOOL sg_abIso[MAX_ZLG_CHANNELS];
static BOOL sg_abExp[MAX_ZLG_CHANNELS];
static unsigned int sg_aunArbSp[MAX_ZLG_CHANNELS];
static unsigned int sg_aunDataSp[MAX_ZLG_CHANNELS];
static char sg_acCustomBaud[MAX_ZLG_CHANNELS][200];
static DEVICE_HANDLE sg_ahDevice[MAX_ZLG_CHANNELS] = {0};
static CHANNEL_HANDLE sg_ahChannel[MAX_ZLG_CHANNELS] = {0};
static HANDLE sg_hRxThread = nullptr;
static volatile BOOL sg_bRxRun = FALSE;

static HMODULE sg_hZlg = nullptr;
static char sg_acZlgDir[MAX_PATH] = {0};
static PFN_ZCAN_OpenDevice sg_OpenDevice = nullptr;
static PFN_ZCAN_CloseDevice sg_CloseDevice = nullptr;
static PFN_ZCAN_GetDeviceInf sg_GetDeviceInf = nullptr;
static PFN_ZCAN_InitCAN sg_InitCAN = nullptr;
static PFN_ZCAN_StartCAN sg_StartCAN = nullptr;
static PFN_ZCAN_ResetCAN sg_ResetCAN = nullptr;
static PFN_ZCAN_GetReceiveNum sg_GetReceiveNum = nullptr;
static PFN_ZCAN_Transmit sg_Transmit = nullptr;
static PFN_ZCAN_Receive sg_Receive = nullptr;
static PFN_ZCAN_TransmitFD sg_TransmitFD = nullptr;
static PFN_ZCAN_ReceiveFD sg_ReceiveFD = nullptr;
static PFN_ZCAN_SetValue sg_SetValue = nullptr;

class CDIL_CAN_ZLG_USB : public CBaseDIL_CAN_Controller
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

static CDIL_CAN_ZLG_USB* g_pouDIL_CAN_ZLG_USB = nullptr;

static BOOL IsUsbCanFdType(UINT unType)
{
    return unType == ZCAN_USBCANFD_200U || unType == ZCAN_USBCANFD_100U ||
           unType == ZCAN_USBCANFD_MINI || unType == ZCAN_USBCANFD_800U ||
           unType == ZCAN_USBCANFD_400U;
}

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

static unsigned int NominalFromController(PSCONTROLLER_DETAILS pDetails)
{
    if (pDetails == nullptr || pDetails->m_omStrBaudrate.empty())
    {
        return 500000;
    }
    unsigned int unBaud = (unsigned int)atoi(pDetails->m_omStrBaudrate.c_str());
    return unBaud ? unBaud : 500000;
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
    if (nLim > MAX_ZLG_CHANNELS)
    {
        nLim = MAX_ZLG_CHANNELS;
    }
    for (INT i = 0; i < nLim; i++)
    {
        sg_aunNomBitRate[i] = NominalFromController(&pDetails[i]);
        sg_abCanFd[i] = pDetails[i].m_bcanFDEnabled ? TRUE : FALSE;
        sg_aunDataBitRate[i] = pDetails[i].m_unDataBitRate ? pDetails[i].m_unDataBitRate : 2000000;
        sg_abTerm[i] = (pDetails[i].m_omStrLocation != "0") ? TRUE : FALSE;
        sg_abIso[i] = (pDetails[i].m_omStrCNF1 != "1") ? TRUE : FALSE;
        sg_abExp[i] = (pDetails[i].m_omStrPropagationDelay != "0") ? TRUE : FALSE;
        sg_aunArbSp[i] = (unsigned int)atoi(pDetails[i].m_omStrSamplePercentage.c_str());
        if (sg_aunArbSp[i] < 50 || sg_aunArbSp[i] > 95)
        {
            sg_aunArbSp[i] = 80;
        }
        sg_aunDataSp[i] = pDetails[i].m_unDataSamplePoint;
        if (sg_aunDataSp[i] < 50 || sg_aunDataSp[i] > 95)
        {
            sg_aunDataSp[i] = (sg_aunDataBitRate[i] == 5000000) ? 75 : 80;
        }
        memset(sg_acCustomBaud[i], 0, sizeof(sg_acCustomBaud[i]));
        if (!pDetails[i].m_omStrCNF2.empty())
        {
            strncpy_s(sg_acCustomBaud[i], pDetails[i].m_omStrCNF2.c_str(), _TRUNCATE);
        }
    }
}

static void DecodeHw(const INTERFACE_HW& hw, UINT& unType, UINT& unIndex, UINT& unCh)
{
    unType = (UINT)hw.m_dwVendor;
    unIndex = (UINT)((hw.m_dwIdInterface >> 8) & 0xFF);
    unCh = (UINT)(hw.m_dwIdInterface & 0xFF);
}

static void FillFromSelected(const INTERFACE_HW_LIST& sSel, INT nCount)
{
    sg_nNoOfChannels = nCount > MAX_ZLG_CHANNELS ? MAX_ZLG_CHANNELS : nCount;
    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        DecodeHw(sSel[i], sg_aunType[i], sg_aunDevIndex[i], sg_aunCh[i]);
        if (sg_aunNomBitRate[i] == 0)
        {
            sg_aunNomBitRate[i] = 500000;
        }
        if (sg_aunDataBitRate[i] == 0)
        {
            sg_aunDataBitRate[i] = 2000000;
        }
    }
}

static void vFillZlgDirFromModule(HMODULE hMod)
{
    memset(sg_acZlgDir, 0, sizeof(sg_acZlgDir));
    if (hMod == nullptr)
    {
        hMod = GetModuleHandleA(nullptr);
    }
    if (GetModuleFileNameA(hMod, sg_acZlgDir, MAX_PATH) == 0)
    {
        sg_acZlgDir[0] = 0;
        return;
    }
    char* pcSlash = strrchr(sg_acZlgDir, '\\');
    if (pcSlash != nullptr)
    {
        *pcSlash = 0;
    }
}

class CZlgDirGuard
{
    char m_acOld[MAX_PATH];
public:
    CZlgDirGuard()
    {
        m_acOld[0] = 0;
        GetCurrentDirectoryA(MAX_PATH, m_acOld);
        if (sg_acZlgDir[0] != 0)
        {
            SetDllDirectoryA(sg_acZlgDir);
            SetCurrentDirectoryA(sg_acZlgDir);
        }
    }
    ~CZlgDirGuard()
    {
        if (m_acOld[0] != 0)
        {
            SetCurrentDirectoryA(m_acOld);
        }
    }
};

static DEVICE_HANDLE ZlgOpenDeviceSafe(UINT unType, UINT unIndex)
{
    DEVICE_HANDLE hDev = nullptr;
    if (sg_OpenDevice == nullptr)
    {
        return nullptr;
    }
    __try
    {
        hDev = sg_OpenDevice(unType, unIndex, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        hDev = nullptr;
    }
    if (hDev == (DEVICE_HANDLE)(INT_PTR)-1)
    {
        hDev = nullptr;
    }
    return hDev;
}

static void ZlgCloseDeviceSafe(DEVICE_HANDLE hDev)
{
    if (hDev == nullptr || sg_CloseDevice == nullptr)
    {
        return;
    }
    __try
    {
        sg_CloseDevice(hDev);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static BOOL ZlgGetDeviceInfSafe(DEVICE_HANDLE hDev, ZCAN_DEVICE_INFO* pInfo)
{
    UINT unRet = 0;
    if (hDev == nullptr || sg_GetDeviceInf == nullptr || pInfo == nullptr)
    {
        return FALSE;
    }
    __try
    {
        unRet = sg_GetDeviceInf(hDev, pInfo);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        unRet = 0;
    }
    return unRet == STATUS_OK;
}

static INT EnumerateZlgChannels(INTERFACE_HW_LIST& asList)
{
    /* Do not call ZCAN_OpenDevice while listing. Opening absent types
       raises 0xC000041D inside zlgcan callbacks and cannot be caught. */
    INT nFound = 0;
    for (int nType = 0; nType < (int)(sizeof(sg_asUsbTypes) / sizeof(sg_asUsbTypes[0])); nType++)
    {
        UINT unChCount = sg_asUsbTypes[nType].unDefCh;
        for (UINT unCh = 0; unCh < unChCount && nFound < (INT)defCHANNEL_CAN_MAX; unCh++)
        {
            char acName[80] = {0};
            sprintf_s(acName, "%s CH%u", sg_asUsbTypes[nType].pcName, unCh);
            asList[nFound].m_dwVendor = sg_asUsbTypes[nType].unType;
            asList[nFound].m_dwIdInterface = unCh;
            asList[nFound].m_bytNetworkID = (unsigned char)unCh;
            asList[nFound].m_acNameInterface = acName;
            asList[nFound].m_acDescription = acName;
            asList[nFound].m_acDeviceName = sg_asUsbTypes[nType].pcName;
            nFound++;
        }
    }
    return nFound;
}

static UINT MakeCanId(unsigned int unId, unsigned char ucExt, unsigned char ucRtr)
{
    UINT unCanId = unId & CAN_ID_FLAG;
    if (ucExt)
    {
        unCanId |= CAN_EFF_FLAG;
    }
    if (ucRtr)
    {
        unCanId |= CAN_RTR_FLAG;
    }
    return unCanId;
}

static void DispatchCan(int nChannelIndex, const zlg_can_frame& frame, UINT64 ts)
{
    if (frame.can_id & CAN_ERR_FLAG)
    {
        return;
    }
    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = RX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_unMsgID = frame.can_id & CAN_ID_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = (frame.can_id & CAN_EFF_FLAG) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucRTR = (frame.can_id & CAN_RTR_FLAG) ? 1 : 0;
    BYTE ucLen = frame.can_dlc > 8 ? 8 : frame.can_dlc;
    can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen = ucLen;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nChannelIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = false;
    memcpy(can_data.m_uDataInfo.m_sCANMsg.m_ucData, frame.data, ucLen);
    can_data.m_lTickCount.QuadPart = (LONGLONG)(ts / 100ULL);
    vWriteIntoClientsBuffer(can_data);
}

static void DispatchFd(int nChannelIndex, const zlg_canfd_frame& frame, UINT64 ts)
{
    if (frame.can_id & CAN_ERR_FLAG)
    {
        return;
    }
    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = RX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_unMsgID = frame.can_id & CAN_ID_FLAG;
    can_data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = (frame.can_id & CAN_EFF_FLAG) ? 1 : 0;
    can_data.m_uDataInfo.m_sCANMsg.m_ucRTR = (frame.can_id & CAN_RTR_FLAG) ? 1 : 0;
    BYTE ucLen = frame.len > 64 ? 64 : frame.len;
    can_data.m_uDataInfo.m_sCANMsg.m_ucDataLen = ucLen;
    can_data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(nChannelIndex + 1);
    can_data.m_uDataInfo.m_sCANMsg.m_bCANFD = true;
    memcpy(can_data.m_uDataInfo.m_sCANMsg.m_ucData, frame.data, ucLen);
    can_data.m_lTickCount.QuadPart = (LONGLONG)(ts / 100ULL);
    vWriteIntoClientsBuffer(can_data);
}

static DWORD WINAPI ZlgRxThread(LPVOID)
{
    while (sg_bRxRun)
    {
        BOOL bGot = FALSE;
        for (INT i = 0; i < sg_nNoOfChannels && sg_bRxRun; i++)
        {
            if (sg_ahChannel[i] == nullptr)
            {
                continue;
            }
            if (sg_GetReceiveNum != nullptr && sg_Receive != nullptr)
            {
                UINT unNum = sg_GetReceiveNum(sg_ahChannel[i], TYPE_CAN);
                if (unNum > 0)
                {
                    ZCAN_Receive_Data asRx[64];
                    UINT unGot = sg_Receive(sg_ahChannel[i], asRx, unNum > 64 ? 64 : unNum, 0);
                    EnterCriticalSection(&sg_DIL_CriticalSection);
                    for (UINT n = 0; n < unGot; n++)
                    {
                        DispatchCan(i, asRx[n].frame, asRx[n].timestamp);
                    }
                    LeaveCriticalSection(&sg_DIL_CriticalSection);
                    bGot = TRUE;
                }
            }
            if (sg_abCanFd[i] && sg_GetReceiveNum != nullptr && sg_ReceiveFD != nullptr)
            {
                UINT unNum = sg_GetReceiveNum(sg_ahChannel[i], TYPE_CANFD);
                if (unNum > 0)
                {
                    ZCAN_ReceiveFD_Data asRx[64];
                    UINT unGot = sg_ReceiveFD(sg_ahChannel[i], asRx, unNum > 64 ? 64 : unNum, 0);
                    EnterCriticalSection(&sg_DIL_CriticalSection);
                    for (UINT n = 0; n < unGot; n++)
                    {
                        DispatchFd(i, asRx[n].frame, asRx[n].timestamp);
                    }
                    LeaveCriticalSection(&sg_DIL_CriticalSection);
                    bGot = TRUE;
                }
            }
        }
        if (!bGot)
        {
            Sleep(1);
        }
    }
    return 0;
}

static BOOL SetAsciiValue(DEVICE_HANDLE hDev, UINT unCh, const char* pcKey, const char* pcVal)
{
    if (sg_SetValue == nullptr || hDev == nullptr)
    {
        return FALSE;
    }
    char acPath[80] = {0};
    sprintf_s(acPath, "%u/%s", unCh, pcKey);
    return sg_SetValue(hDev, acPath, pcVal) == STATUS_OK;
}

static DWORD MakeZlgTimingValue(unsigned int unBrp, unsigned int unTseg1, unsigned int unTseg2, unsigned int unSjw)
{
    return (unBrp << 22) | ((unSjw & 0x3F) << 16) | ((unTseg2 & 0xFF) << 8) | (unTseg1 & 0xFF);
}

static void FormatZlgRatePart(char* ac, size_t nLen, unsigned int unBaud, unsigned int unSp)
{
    if (unBaud >= 1000000)
    {
        sprintf_s(ac, nLen, "%u.%uMbps(%u%%)", unBaud / 1000000, (unBaud / 100000) % 10, unSp);
    }
    else
    {
        sprintf_s(ac, nLen, "%uKbps(%u%%)", unBaud / 1000, unSp);
    }
}

static void InitTimingList(CListCtrl* pList)
{
    if (pList == nullptr)
    {
        return;
    }
    if (pList->GetHeaderCtrl() != nullptr && pList->GetHeaderCtrl()->GetItemCount() > 0)
    {
        return;
    }
    pList->SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    pList->InsertColumn(0, "Value", LVCFMT_LEFT, 78);
    pList->InsertColumn(1, "BRP", LVCFMT_RIGHT, 42);
    pList->InsertColumn(2, "TSEG1", LVCFMT_RIGHT, 48);
    pList->InsertColumn(3, "TSEG2", LVCFMT_RIGHT, 48);
    pList->InsertColumn(4, "SMP", LVCFMT_RIGHT, 58);
    pList->InsertColumn(5, "Baud", LVCFMT_RIGHT, 90);
    pList->InsertColumn(6, "Diff", LVCFMT_RIGHT, 52);
}

static void FillComboKbps(CComboBox* pBox, const unsigned int* punKbps, int nCount, unsigned int unCurKbps)
{
    if (pBox == nullptr)
    {
        return;
    }
    pBox->ResetContent();
    int nSel = 0;
    for (int i = 0; i < nCount; i++)
    {
        CString om;
        om.Format("%u", punKbps[i]);
        int nAt = pBox->AddString(om);
        pBox->SetItemData(nAt, punKbps[i]);
        if (punKbps[i] == unCurKbps)
        {
            nSel = nAt;
        }
    }
    pBox->SetCurSel(nSel);
}

static unsigned int ReadKbps(CComboBox* pBox, unsigned int unDefKbps)
{
    if (pBox == nullptr)
    {
        return unDefKbps;
    }
    int nSel = pBox->GetCurSel();
    if (nSel >= 0)
    {
        unsigned int unData = (unsigned int)pBox->GetItemData(nSel);
        if (unData != 0)
        {
            return unData;
        }
        CString omSel;
        pBox->GetLBText(nSel, omSel);
        unsigned int unFromList = (unsigned int)_tstol(omSel);
        if (unFromList != 0)
        {
            return unFromList;
        }
    }
    CString om;
    pBox->GetWindowText(om);
    unsigned int unVal = (unsigned int)_tstol(om);
    return unVal ? unVal : unDefKbps;
}

static int ReadOptionalInt(CWnd* pDlg, UINT nId)
{
    if (pDlg == nullptr)
    {
        return -1;
    }
    CString om;
    pDlg->GetDlgItemText(nId, om);
    om.Trim();
    if (om.IsEmpty())
    {
        return -1;
    }
    return (int)_tstol(om);
}

static unsigned int ReadComboUInt(CComboBox* pBox, unsigned int unDef)
{
    if (pBox == nullptr)
    {
        return unDef;
    }
    int nSel = pBox->GetCurSel();
    if (nSel >= 0)
    {
        return (unsigned int)pBox->GetItemData(nSel);
    }
    CString om;
    pBox->GetWindowText(om);
    unsigned int unVal = (unsigned int)_tstol(om);
    return unVal ? unVal : unDef;
}

class CZlgAdvanceDlg : public CDialog, public IChangeRegisters
{
public:
    CZlgAdvanceDlg()
        : CDialog(IDD_ZLG_ADVANCE, nullptr)
        , m_p(nullptr)
        , m_nCount(0)
        , m_nSel(0)
        , m_bUpdating(FALSE)
        , m_bApplySavedPrefer(FALSE)
    {
    }
    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSel)
    {
        m_p = pControllerDetails;
        m_nCount = nCount;
        m_nSel = nSel;
        return (DoModal() == IDOK) ? 1 : 0;
    }
    double vValidateBaudRate(double dBaud, int, UINT)
    {
        return dBaud;
    }
protected:
    void FillTimingTable(CListCtrl* pList, unsigned int unClkMhz, unsigned int unBaudBps,
        double dMaxErrPct, unsigned int unSjw, BOOL bTseg2GeSjw, DWORD dwPrefer,
        int nFilterBrp, int nFilterTseg1, int nFilterTseg2)
    {
        if (pList == nullptr || unClkMhz == 0 || unBaudBps == 0)
        {
            return;
        }
        pList->SetRedraw(FALSE);
        pList->DeleteAllItems();
        unsigned int unClk = unClkMhz * 1000000u;
        struct SRow
        {
            unsigned int unBrp;
            unsigned int unTseg1;
            unsigned int unTseg2;
            unsigned int unActual;
            double dSmp;
            double dDiff;
            DWORD dwVal;
        };
        std::vector<SRow> asRows;
        asRows.reserve(512);
        for (unsigned int unBrp = 0; unBrp <= 1023; unBrp++)
        {
            unsigned int unDiv = unBrp + 1;
            unsigned long long ullLo = (unsigned long long)unDiv * unBaudBps;
            if (ullLo == 0)
            {
                continue;
            }
            double dNbtIdeal = (double)unClk / (double)ullLo;
            unsigned int unNbtMin = 3;
            unsigned int unNbtMax = 513;
            if (dMaxErrPct < 99.0)
            {
                double dScale = dMaxErrPct / 100.0 + 1e-12;
                unsigned int unA = (unsigned int)(dNbtIdeal / (1.0 + dScale));
                unsigned int unB = (unsigned int)(dNbtIdeal / (1.0 - dScale) + 1.0);
                if (unA > unNbtMin)
                {
                    unNbtMin = unA;
                }
                if (unB < unNbtMax)
                {
                    unNbtMax = unB;
                }
            }
            for (unsigned int unNbt = unNbtMin; unNbt <= unNbtMax; unNbt++)
            {
                unsigned long long ullDen = (unsigned long long)unDiv * unNbt;
                if (ullDen == 0)
                {
                    continue;
                }
                double dActual = (double)unClk / (double)ullDen;
                unsigned int unActual = (unsigned int)(dActual + 0.5);
                double dDiff = fabs(dActual - (double)unBaudBps) * 100.0 / (double)unBaudBps;
                if (dDiff > dMaxErrPct + 1e-9)
                {
                    continue;
                }
                if (unNbt < 3)
                {
                    continue;
                }
                for (unsigned int unTseg1 = 0; unTseg1 <= 255 && unTseg1 + 3 <= unNbt; unTseg1++)
                {
                    unsigned int unTseg2 = unNbt - unTseg1 - 3;
                    if (unTseg2 > 255)
                    {
                        continue;
                    }
                    if (bTseg2GeSjw && unTseg2 < unSjw)
                    {
                        continue;
                    }
                    if (nFilterBrp >= 0 && (int)unBrp != nFilterBrp)
                    {
                        continue;
                    }
                    if (nFilterTseg1 >= 0 && (int)unTseg1 != nFilterTseg1)
                    {
                        continue;
                    }
                    if (nFilterTseg2 >= 0 && (int)unTseg2 != nFilterTseg2)
                    {
                        continue;
                    }
                    SRow s;
                    s.unBrp = unBrp;
                    s.unTseg1 = unTseg1;
                    s.unTseg2 = unTseg2;
                    s.unActual = unActual;
                    s.dSmp = (unTseg1 + 2) * 100.0 / (double)unNbt;
                    s.dDiff = dDiff;
                    s.dwVal = MakeZlgTimingValue(unBrp, unTseg1, unTseg2, unSjw);
                    asRows.push_back(s);
                }
            }
        }
        std::sort(asRows.begin(), asRows.end(),
            [](const SRow& a, const SRow& b)
            {
                if (a.unTseg2 != b.unTseg2)
                {
                    return a.unTseg2 < b.unTseg2;
                }
                if (a.unBrp != b.unBrp)
                {
                    return a.unBrp > b.unBrp;
                }
                return a.unTseg1 < b.unTseg1;
            });
        int nPrefer = -1;
        int nSp80 = -1;
        int nLimit = (int)asRows.size();
        if (nLimit > 4000)
        {
            nLimit = 4000;
        }
        for (int i = 0; i < nLimit; i++)
        {
            const SRow& s = asRows[(size_t)i];
            CString om;
            om.Format("%08X", s.dwVal);
            int nItem = pList->InsertItem(i, om);
            om.Format("%u", s.unBrp);
            pList->SetItemText(nItem, 1, om);
            om.Format("%u", s.unTseg1);
            pList->SetItemText(nItem, 2, om);
            om.Format("%u", s.unTseg2);
            pList->SetItemText(nItem, 3, om);
            om.Format("%.2f %%", s.dSmp);
            pList->SetItemText(nItem, 4, om);
            om.Format("%u bps", s.unActual);
            pList->SetItemText(nItem, 5, om);
            om.Format("%.2f %%", s.dDiff);
            pList->SetItemText(nItem, 6, om);
            pList->SetItemData(nItem, s.dwVal);
            if (dwPrefer != 0 && s.dwVal == dwPrefer)
            {
                nPrefer = nItem;
            }
            if (nSp80 < 0 && fabs(s.dSmp - 80.0) < 0.01)
            {
                nSp80 = nItem;
            }
        }
        pList->SetRedraw(TRUE);
        if (nLimit > 0)
        {
            int nSel = 0;
            if (nPrefer >= 0)
            {
                nSel = nPrefer;
            }
            else if (nSp80 >= 0)
            {
                nSel = nSp80;
            }
            pList->SetItemState(nSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            pList->EnsureVisible(nSel, FALSE);
        }
        pList->Invalidate();
    }

    unsigned int SelectedSp(CListCtrl* pList)
    {
        if (pList == nullptr)
        {
            return 80;
        }
        POSITION pos = pList->GetFirstSelectedItemPosition();
        if (pos == nullptr)
        {
            return 80;
        }
        int nItem = pList->GetNextSelectedItem(pos);
        CString om = pList->GetItemText(nItem, 4);
        return (unsigned int)_tstol(om);
    }

    DWORD SelectedValue(CListCtrl* pList)
    {
        if (pList == nullptr)
        {
            return 0;
        }
        POSITION pos = pList->GetFirstSelectedItemPosition();
        if (pos == nullptr)
        {
            if (pList->GetItemCount() <= 0)
            {
                return 0;
            }
            return (DWORD)pList->GetItemData(0);
        }
        int nItem = pList->GetNextSelectedItem(pos);
        return (DWORD)pList->GetItemData(nItem);
    }

    void UpdateResult()
    {
        unsigned int unClk = ReadComboUInt((CComboBox*)GetDlgItem(IDC_ZLG_CLOCK), 80);
        unsigned int unArbKbps = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_NOM), 500);
        unsigned int unDataKbps = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_DATA), 4000);
        CListCtrl* pArb = (CListCtrl*)GetDlgItem(IDC_ZLG_ARB_LIST);
        CListCtrl* pData = (CListCtrl*)GetDlgItem(IDC_ZLG_DATA_LIST);
        unsigned int unAsp = SelectedSp(pArb);
        unsigned int unDsp = SelectedSp(pData);
        DWORD dwA = SelectedValue(pArb);
        DWORD dwD = SelectedValue(pData);
        char acA[40] = {0};
        char acD[40] = {0};
        FormatZlgRatePart(acA, sizeof(acA), unArbKbps * 1000, unAsp);
        FormatZlgRatePart(acD, sizeof(acD), unDataKbps * 1000, unDsp);
        CString om;
        om.Format("%s,%s,(%u,%08X,%08X)", acA, acD, unClk, dwA, dwD);
        CWnd* pRes = GetDlgItem(IDC_ZLG_RESULT);
        if (pRes != nullptr)
        {
            pRes->SetWindowText(om);
        }
    }

    void RecalcTables()
    {
        if (m_bUpdating)
        {
            return;
        }
        m_bUpdating = TRUE;
        unsigned int unClk = ReadComboUInt((CComboBox*)GetDlgItem(IDC_ZLG_CLOCK), 80);
        unsigned int unArb = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_NOM), 500) * 1000;
        unsigned int unData = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_DATA), 4000) * 1000;
        CString omErr;
        GetDlgItemText(IDC_ZLG_NOM_ERR, omErr);
        double dArbErr = atof(omErr);
        if (dArbErr <= 0)
        {
            dArbErr = 0.05;
        }
        GetDlgItemText(IDC_ZLG_DATA_ERR, omErr);
        double dDataErr = atof(omErr);
        if (dDataErr <= 0)
        {
            dDataErr = 0.05;
        }
        unsigned int unSjwA = ReadComboUInt((CComboBox*)GetDlgItem(IDC_ZLG_SJW), 0);
        unsigned int unSjwD = ReadComboUInt((CComboBox*)GetDlgItem(IDC_ZLG_DATA_SJW), 0);
        CButton* pGeA = (CButton*)GetDlgItem(IDC_ZLG_TSEG2_SJW);
        CButton* pGeD = (CButton*)GetDlgItem(IDC_ZLG_DATA_TSEG2_SJW);
        BOOL bGeA = (pGeA != nullptr && pGeA->GetCheck() == BST_CHECKED);
        BOOL bGeD = (pGeD != nullptr && pGeD->GetCheck() == BST_CHECKED);
        DWORD dwPreferA = 0;
        DWORD dwPreferD = 0;
        UINT nUse = (m_p != nullptr && m_nCount > 0 && m_nSel < m_nCount) ? m_nSel : 0;
        if (m_p != nullptr && !m_p[nUse].m_omStrCNF2.empty())
        {
            const char* pc = strrchr(m_p[nUse].m_omStrCNF2.c_str(), ',');
            if (pc != nullptr)
            {
                unsigned int unTmp = 0;
                sscanf_s(m_p[nUse].m_omStrCNF2.c_str(), "%*[^,],%*[^,],(%u,%x,%x)", &unTmp, &dwPreferA, &dwPreferD);
            }
        }
        if (!m_bApplySavedPrefer)
        {
            dwPreferA = 0;
            dwPreferD = 0;
        }
        m_bApplySavedPrefer = FALSE;
        FillTimingTable((CListCtrl*)GetDlgItem(IDC_ZLG_ARB_LIST), unClk, unArb, dArbErr, unSjwA, bGeA, dwPreferA,
            ReadOptionalInt(this, IDC_ZLG_ARB_BRP), ReadOptionalInt(this, IDC_ZLG_ARB_TSEG1), ReadOptionalInt(this, IDC_ZLG_ARB_TSEG2));
        FillTimingTable((CListCtrl*)GetDlgItem(IDC_ZLG_DATA_LIST), unClk, unData, dDataErr, unSjwD, bGeD, dwPreferD,
            ReadOptionalInt(this, IDC_ZLG_DATA_BRP), ReadOptionalInt(this, IDC_ZLG_DATA_TSEG1), ReadOptionalInt(this, IDC_ZLG_DATA_TSEG2));
        m_bUpdating = FALSE;
        UpdateResult();
    }

    virtual BOOL OnInitDialog()
    {
        CDialog::OnInitDialog();
        static const unsigned int sunArb[] = {50, 100, 125, 250, 500, 800, 1000};
        static const unsigned int sunData[] = {125, 250, 500, 800, 1000, 2000, 4000, 5000};
        UINT nUse = 0;
        if (m_p != nullptr && m_nCount > 0 && m_nSel < m_nCount)
        {
            nUse = m_nSel;
        }
        unsigned int unNom = 500000;
        unsigned int unData = 4000000;
        unsigned int unClk = 80;
        BOOL bFd = TRUE;
        BOOL bIso = TRUE;
        BOOL bExp = TRUE;
        BOOL bTerm = TRUE;
        if (m_p != nullptr)
        {
            unNom = NominalFromController(&m_p[nUse]);
            unData = m_p[nUse].m_unDataBitRate ? m_p[nUse].m_unDataBitRate : 4000000;
            bFd = m_p[nUse].m_bcanFDEnabled ? TRUE : FALSE;
            bTerm = (m_p[nUse].m_omStrLocation != "0") ? TRUE : FALSE;
            bIso = (m_p[nUse].m_omStrCNF1 != "1") ? TRUE : FALSE;
            bExp = (m_p[nUse].m_omStrPropagationDelay != "0") ? TRUE : FALSE;
            unsigned int unC = (unsigned int)atoi(m_p[nUse].m_omStrClock.c_str());
            if (unC == 60 || unC == 80)
            {
                unClk = unC;
            }
        }
        CComboBox* pClk = (CComboBox*)GetDlgItem(IDC_ZLG_CLOCK);
        if (pClk != nullptr)
        {
            pClk->ResetContent();
            int n60 = pClk->AddString("60");
            pClk->SetItemData(n60, 60);
            int n80 = pClk->AddString("80");
            pClk->SetItemData(n80, 80);
            pClk->SetCurSel(unClk == 60 ? n60 : n80);
        }
        FillComboKbps((CComboBox*)GetDlgItem(IDC_ZLG_NOM), sunArb, (int)(sizeof(sunArb) / sizeof(sunArb[0])), unNom / 1000);
        FillComboKbps((CComboBox*)GetDlgItem(IDC_ZLG_DATA), sunData, (int)(sizeof(sunData) / sizeof(sunData[0])), unData / 1000);
        CComboBox* pSjwA = (CComboBox*)GetDlgItem(IDC_ZLG_SJW);
        CComboBox* pSjwD = (CComboBox*)GetDlgItem(IDC_ZLG_DATA_SJW);
        if (pSjwA != nullptr)
        {
            pSjwA->ResetContent();
            for (int i = 0; i <= 3; i++)
            {
                CString om;
                om.Format("%d", i);
                int nAt = pSjwA->AddString(om);
                pSjwA->SetItemData(nAt, i);
            }
            pSjwA->SetCurSel(0);
        }
        if (pSjwD != nullptr)
        {
            pSjwD->ResetContent();
            for (int i = 0; i <= 3; i++)
            {
                CString om;
                om.Format("%d", i);
                int nAt = pSjwD->AddString(om);
                pSjwD->SetItemData(nAt, i);
            }
            pSjwD->SetCurSel(0);
        }
        SetDlgItemText(IDC_ZLG_NOM_ERR, "0.05");
        SetDlgItemText(IDC_ZLG_DATA_ERR, "0.05");
        CButton* pGeA = (CButton*)GetDlgItem(IDC_ZLG_TSEG2_SJW);
        CButton* pGeD = (CButton*)GetDlgItem(IDC_ZLG_DATA_TSEG2_SJW);
        if (pGeA != nullptr)
        {
            pGeA->SetCheck(BST_CHECKED);
        }
        if (pGeD != nullptr)
        {
            pGeD->SetCheck(BST_CHECKED);
        }
        CButton* pFd = (CButton*)GetDlgItem(IDC_ZLG_FD);
        if (pFd != nullptr)
        {
            pFd->SetCheck(bFd ? BST_CHECKED : BST_UNCHECKED);
        }
        CButton* pIso = (CButton*)GetDlgItem(IDC_ZLG_ISO);
        CButton* pNon = (CButton*)GetDlgItem(IDC_ZLG_NONISO);
        if (pIso != nullptr)
        {
            pIso->SetCheck(bIso ? BST_CHECKED : BST_UNCHECKED);
        }
        if (pNon != nullptr)
        {
            pNon->SetCheck(bIso ? BST_UNCHECKED : BST_CHECKED);
        }
        CButton* pExp = (CButton*)GetDlgItem(IDC_ZLG_EXP);
        if (pExp != nullptr)
        {
            pExp->SetCheck(bExp ? BST_CHECKED : BST_UNCHECKED);
        }
        CButton* pTerm = (CButton*)GetDlgItem(IDC_ZLG_TERM);
        if (pTerm != nullptr)
        {
            pTerm->SetCheck(bTerm ? BST_CHECKED : BST_UNCHECKED);
        }
        InitTimingList((CListCtrl*)GetDlgItem(IDC_ZLG_ARB_LIST));
        InitTimingList((CListCtrl*)GetDlgItem(IDC_ZLG_DATA_LIST));
        RecalcTables();
        return TRUE;
    }
    virtual void OnOK()
    {
        if (m_p != nullptr && m_nCount > 0)
        {
            UINT nUse = (m_nSel < m_nCount) ? m_nSel : 0;
            unsigned int unNom = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_NOM), 500) * 1000;
            unsigned int unData = ReadKbps((CComboBox*)GetDlgItem(IDC_ZLG_DATA), 4000) * 1000;
            unsigned int unClk = ReadComboUInt((CComboBox*)GetDlgItem(IDC_ZLG_CLOCK), 80);
            CListCtrl* pArb = (CListCtrl*)GetDlgItem(IDC_ZLG_ARB_LIST);
            CListCtrl* pData = (CListCtrl*)GetDlgItem(IDC_ZLG_DATA_LIST);
            unsigned int unAsp = SelectedSp(pArb);
            unsigned int unDsp = SelectedSp(pData);
            CButton* pFd = (CButton*)GetDlgItem(IDC_ZLG_FD);
            CButton* pIso = (CButton*)GetDlgItem(IDC_ZLG_ISO);
            CButton* pExp = (CButton*)GetDlgItem(IDC_ZLG_EXP);
            CButton* pTerm = (CButton*)GetDlgItem(IDC_ZLG_TERM);
            BOOL bFd = (pFd != nullptr && pFd->GetCheck() == BST_CHECKED);
            BOOL bIso = (pIso != nullptr && pIso->GetCheck() == BST_CHECKED);
            BOOL bExp = (pExp != nullptr && pExp->GetCheck() == BST_CHECKED);
            BOOL bTerm = (pTerm == nullptr || pTerm->GetCheck() == BST_CHECKED);
            char acNom[16] = {0};
            char acSp[8] = {0};
            char acClk[8] = {0};
            sprintf_s(acNom, "%u", unNom);
            sprintf_s(acSp, "%u", unAsp);
            sprintf_s(acClk, "%u", unClk);
            CString omRes;
            GetDlgItemText(IDC_ZLG_RESULT, omRes);
            m_p[nUse].m_omStrBaudrate = acNom;
            m_p[nUse].m_unDataBitRate = unData;
            m_p[nUse].m_bcanFDEnabled = bFd ? true : false;
            m_p[nUse].m_bSupportCANFD = true;
            m_p[nUse].m_bISO = bIso ? true : false;
            m_p[nUse].m_omStrCNF1 = bIso ? "0" : "1";
            m_p[nUse].m_omStrCNF2 = omRes;
            m_p[nUse].m_omStrClock = acClk;
            m_p[nUse].m_omStrLocation = bTerm ? "1" : "0";
            m_p[nUse].m_omStrPropagationDelay = bExp ? "1" : "0";
            m_p[nUse].m_omStrSamplePercentage = acSp;
            m_p[nUse].m_unDataSamplePoint = unDsp;
        }
        CDialog::OnOK();
    }
    afx_msg void OnParamChange()
    {
        RecalcTables();
    }
    afx_msg void OnCopyResult()
    {
        CString om;
        GetDlgItemText(IDC_ZLG_RESULT, om);
        if (OpenClipboard())
        {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)om.GetLength() + 1);
            if (hMem != nullptr)
            {
                memcpy(GlobalLock(hMem), (LPCSTR)om, (SIZE_T)om.GetLength() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
        }
    }
    afx_msg void OnListChanged(NMHDR* pNMHDR, LRESULT* pResult)
    {
        if (!m_bUpdating)
        {
            LPNMLISTVIEW pNm = (LPNMLISTVIEW)pNMHDR;
            if (pNm != nullptr && (pNm->uChanged & LVIF_STATE) && (pNm->uNewState & LVIS_SELECTED))
            {
                UpdateResult();
            }
        }
        *pResult = 0;
    }
    DECLARE_MESSAGE_MAP()
    PSCONTROLLER_DETAILS m_p;
    UINT m_nCount;
    UINT m_nSel;
    BOOL m_bUpdating;
    BOOL m_bApplySavedPrefer;
};

BEGIN_MESSAGE_MAP(CZlgAdvanceDlg, CDialog)
    ON_CBN_SELCHANGE(IDC_ZLG_CLOCK, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELCHANGE(IDC_ZLG_NOM, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELCHANGE(IDC_ZLG_DATA, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELENDOK(IDC_ZLG_NOM, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELENDOK(IDC_ZLG_DATA, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_CLOSEUP(IDC_ZLG_NOM, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_CLOSEUP(IDC_ZLG_DATA, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_KILLFOCUS(IDC_ZLG_NOM, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_KILLFOCUS(IDC_ZLG_DATA, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELCHANGE(IDC_ZLG_SJW, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_SELCHANGE(IDC_ZLG_DATA_SJW, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_EDITCHANGE(IDC_ZLG_NOM, &CZlgAdvanceDlg::OnParamChange)
    ON_CBN_EDITCHANGE(IDC_ZLG_DATA, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_KILLFOCUS(IDC_ZLG_NOM_ERR, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_KILLFOCUS(IDC_ZLG_DATA_ERR, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_ARB_BRP, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_ARB_TSEG1, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_ARB_TSEG2, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_DATA_BRP, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_DATA_TSEG1, &CZlgAdvanceDlg::OnParamChange)
    ON_EN_CHANGE(IDC_ZLG_DATA_TSEG2, &CZlgAdvanceDlg::OnParamChange)
    ON_BN_CLICKED(IDC_ZLG_TSEG2_SJW, &CZlgAdvanceDlg::OnParamChange)
    ON_BN_CLICKED(IDC_ZLG_DATA_TSEG2_SJW, &CZlgAdvanceDlg::OnParamChange)
    ON_BN_CLICKED(IDC_ZLG_COPY, &CZlgAdvanceDlg::OnCopyResult)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_ZLG_ARB_LIST, &CZlgAdvanceDlg::OnListChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_ZLG_DATA_LIST, &CZlgAdvanceDlg::OnListChanged)
END_MESSAGE_MAP()

USAGEMODE HRESULT __cdecl GetIDIL_CAN_Controller(void** ppvInterface)
{
    HRESULT hResult = S_OK;
    if (g_pouDIL_CAN_ZLG_USB == nullptr)
    {
        g_pouDIL_CAN_ZLG_USB = new CDIL_CAN_ZLG_USB;
        if (g_pouDIL_CAN_ZLG_USB == nullptr)
        {
            hResult = S_FALSE;
        }
    }
    *ppvInterface = (void*)g_pouDIL_CAN_ZLG_USB;
    return hResult;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0;
    sg_QueryTickCount.QuadPart = 0;
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_LoadDriverLibrary(void)
{
    if (sg_hZlg != nullptr)
    {
        return DLL_ALREADY_LOADED;
    }
    vFillZlgDirFromModule(GetModuleHandleA(nullptr));
    if (sg_acZlgDir[0] != 0)
    {
        SetDllDirectoryA(sg_acZlgDir);
        char acDll[MAX_PATH] = {0};
        sprintf_s(acDll, "%s\\zlgcan.dll", sg_acZlgDir);
        sg_hZlg = LoadLibraryA(acDll);
    }
    if (sg_hZlg == nullptr)
    {
        sg_hZlg = LoadLibraryA("zlgcan.dll");
    }
    if (sg_hZlg == nullptr)
    {
        sg_acErrStr = "zlgcan.dll not found. Copy official x86 zlgcan.dll and kerneldlls next to BUSMASTER.exe.";
        return ERR_LOAD_DRIVER;
    }
    vFillZlgDirFromModule(sg_hZlg);
    if (sg_acZlgDir[0] != 0)
    {
        char acKernel[MAX_PATH] = {0};
        sprintf_s(acKernel, "%s\\kerneldlls\\USBCANFD.dll", sg_acZlgDir);
        LoadLibraryA(acKernel);
    }
    sg_OpenDevice = (PFN_ZCAN_OpenDevice)GetProcAddress(sg_hZlg, "ZCAN_OpenDevice");
    sg_CloseDevice = (PFN_ZCAN_CloseDevice)GetProcAddress(sg_hZlg, "ZCAN_CloseDevice");
    sg_GetDeviceInf = (PFN_ZCAN_GetDeviceInf)GetProcAddress(sg_hZlg, "ZCAN_GetDeviceInf");
    sg_InitCAN = (PFN_ZCAN_InitCAN)GetProcAddress(sg_hZlg, "ZCAN_InitCAN");
    sg_StartCAN = (PFN_ZCAN_StartCAN)GetProcAddress(sg_hZlg, "ZCAN_StartCAN");
    sg_ResetCAN = (PFN_ZCAN_ResetCAN)GetProcAddress(sg_hZlg, "ZCAN_ResetCAN");
    sg_GetReceiveNum = (PFN_ZCAN_GetReceiveNum)GetProcAddress(sg_hZlg, "ZCAN_GetReceiveNum");
    sg_Transmit = (PFN_ZCAN_Transmit)GetProcAddress(sg_hZlg, "ZCAN_Transmit");
    sg_Receive = (PFN_ZCAN_Receive)GetProcAddress(sg_hZlg, "ZCAN_Receive");
    sg_TransmitFD = (PFN_ZCAN_TransmitFD)GetProcAddress(sg_hZlg, "ZCAN_TransmitFD");
    sg_ReceiveFD = (PFN_ZCAN_ReceiveFD)GetProcAddress(sg_hZlg, "ZCAN_ReceiveFD");
    sg_SetValue = (PFN_ZCAN_SetValue)GetProcAddress(sg_hZlg, "ZCAN_SetValue");
    if (sg_OpenDevice == nullptr || sg_CloseDevice == nullptr || sg_InitCAN == nullptr ||
            sg_StartCAN == nullptr || sg_Transmit == nullptr || sg_Receive == nullptr)
    {
        FreeLibrary(sg_hZlg);
        sg_hZlg = nullptr;
        sg_acErrStr = "zlgcan.dll is missing required exports.";
        return ERR_LOAD_DRIVER;
    }
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_UnloadDriverLibrary(void)
{
    CAN_StopHardware();
    if (sg_hZlg != nullptr)
    {
        FreeLibrary(sg_hZlg);
        sg_hZlg = nullptr;
    }
    sg_OpenDevice = nullptr;
    sg_CloseDevice = nullptr;
    sg_GetDeviceInf = nullptr;
    sg_InitCAN = nullptr;
    sg_StartCAN = nullptr;
    sg_ResetCAN = nullptr;
    sg_GetReceiveNum = nullptr;
    sg_Transmit = nullptr;
    sg_Receive = nullptr;
    sg_TransmitFD = nullptr;
    sg_ReceiveFD = nullptr;
    sg_SetValue = nullptr;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_PerformInitOperations(void)
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

HRESULT CDIL_CAN_ZLG_USB::CAN_PerformClosureOperations(void)
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

HRESULT CDIL_CAN_ZLG_USB::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    INTERFACE_HW_LIST asFound = {};
    INT nFound = EnumerateZlgChannels(asFound);
    if (nFound <= 0)
    {
        nCount = 0;
        sg_acErrStr = "No ZLG USB CAN/CANFD device available.";
        return S_FALSE;
    }

    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    int anSelList[CHANNEL_ALLOWED];
    for (int i = 0; i < CHANNEL_ALLOWED; i++)
    {
        anSelList[i] = -1;
    }
    if (nFound >= 1)
    {
        anSelList[0] = 0;
    }
    if (nFound >= 2)
    {
        anSelList[1] = 1;
    }
    if (InitData != nullptr)
    {
        for (INT i = 0; i < nFound; i++)
        {
            InitData[i].m_bcanFDEnabled = true;
            InitData[i].m_bSupportCANFD = true;
            InitData[i].m_bISO = true;
            if (InitData[i].m_unDataBitRate == 0)
            {
                InitData[i].m_unDataBitRate = 2000000;
            }
            if (InitData[i].m_omStrLocation.empty())
            {
                InitData[i].m_omStrLocation = "1";
            }
            if (InitData[i].m_omStrCNF1.empty())
            {
                InitData[i].m_omStrCNF1 = "0";
            }
            if (InitData[i].m_omStrPropagationDelay.empty())
            {
                InitData[i].m_omStrPropagationDelay = "1";
            }
            if (InitData[i].m_omStrSamplePercentage.empty())
            {
                InitData[i].m_omStrSamplePercentage = "80";
            }
            if (InitData[i].m_unDataSamplePoint == 0)
            {
                InitData[i].m_unDataSamplePoint = 80;
            }
        }
    }
    CWnd objMainWnd;
    objMainWnd.Attach(sg_hOwnerWnd);
    CZlgAdvanceDlg ouAdvance;
    CHardwareListingCAN HwList(asFound, nFound, anSelList, CAN, nFound, &objMainWnd, InitData, &ouAdvance);
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

HRESULT CDIL_CAN_ZLG_USB::CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount)
{
    if (nCount <= 0)
    {
        return S_FALSE;
    }
    FillFromSelected(sSelHwInterface, nCount);
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_DeselectHwInterface(void)
{
    CAN_StopHardware();
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    ApplyControllerConfig(InitData, Length > 0 ? Length : 1);
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS pDetails, DWORD /*dwDriverId*/, bool /*bIsHardwareListed*/, unsigned int unChannelCount)
{
    if (pDetails != nullptr)
    {
        ApplyControllerConfig(pDetails, (int)unChannelCount);
    }
    if (unChannelCount > 0 && unChannelCount <= (unsigned int)MAX_ZLG_CHANNELS)
    {
        sg_nNoOfChannels = (INT)unChannelCount;
    }
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_StartHardware(void)
{
    if (sg_OpenDevice == nullptr || sg_nNoOfChannels <= 0)
    {
        sg_acErrStr = "No channel selected.";
        return S_FALSE;
    }

    CAN_StopHardware();
    CZlgDirGuard ouDir;

    for (INT i = 0; i < sg_nNoOfChannels; i++)
    {
        DEVICE_HANDLE hDev = nullptr;
        for (INT j = 0; j < i; j++)
        {
            if (sg_aunType[j] == sg_aunType[i] && sg_aunDevIndex[j] == sg_aunDevIndex[i] && sg_ahDevice[j] != nullptr)
            {
                hDev = sg_ahDevice[j];
                break;
            }
        }
        if (hDev == nullptr)
        {
            hDev = ZlgOpenDeviceSafe(sg_aunType[i], sg_aunDevIndex[i]);
            if (hDev == nullptr)
            {
                sg_acErrStr = "ZCAN_OpenDevice failed. Check USB cable and that kerneldlls sits next to BUSMASTER.exe.";
                CAN_StopHardware();
                return S_FALSE;
            }
        }
        sg_ahDevice[i] = hDev;

        char acNom[16] = {0};
        char acData[16] = {0};
        sprintf_s(acNom, "%u", sg_aunNomBitRate[i] ? sg_aunNomBitRate[i] : 500000);
        sprintf_s(acData, "%u", sg_aunDataBitRate[i] ? sg_aunDataBitRate[i] : 2000000);

        if (IsUsbCanFdType(sg_aunType[i]))
        {
            SetAsciiValue(hDev, sg_aunCh[i], "protocol", sg_abCanFd[i] ? "1" : "0");
            SetAsciiValue(hDev, sg_aunCh[i], "canfd_standard", sg_abIso[i] ? "0" : "1");
            SetAsciiValue(hDev, sg_aunCh[i], "canfd_exp", sg_abExp[i] ? "1" : "0");
            if (sg_acCustomBaud[i][0] != 0)
            {
                SetAsciiValue(hDev, sg_aunCh[i], "canfd_abit_baud_rate", "0");
                SetAsciiValue(hDev, sg_aunCh[i], "baud_rate_custom", sg_acCustomBaud[i]);
            }
            else
            {
                SetAsciiValue(hDev, sg_aunCh[i], "canfd_abit_baud_rate", acNom);
                if (sg_abCanFd[i] && sg_abExp[i])
                {
                    SetAsciiValue(hDev, sg_aunCh[i], "canfd_dbit_baud_rate", acData);
                }
            }
        }
        else
        {
            SetAsciiValue(hDev, sg_aunCh[i], "baud_rate", acNom);
        }

        ZCAN_CHANNEL_INIT_CONFIG cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (IsUsbCanFdType(sg_aunType[i]) || sg_abCanFd[i])
        {
            cfg.can_type = TYPE_CANFD;
            cfg.canfd.acc_code = 0;
            cfg.canfd.acc_mask = 0xFFFFFFFF;
            cfg.canfd.mode = 0;
        }
        else
        {
            cfg.can_type = TYPE_CAN;
            cfg.can.acc_code = 0;
            cfg.can.acc_mask = 0xFFFFFFFF;
            cfg.can.mode = 0;
        }
        sg_ahChannel[i] = sg_InitCAN(hDev, sg_aunCh[i], &cfg);
        if (sg_ahChannel[i] == nullptr)
        {
            sg_acErrStr = "ZCAN_InitCAN failed.";
            CAN_StopHardware();
            return S_FALSE;
        }
        SetAsciiValue(hDev, sg_aunCh[i], "initenal_resistance", sg_abTerm[i] ? "1" : "0");
        if (sg_StartCAN(sg_ahChannel[i]) != STATUS_OK)
        {
            sg_acErrStr = "ZCAN_StartCAN failed.";
            CAN_StopHardware();
            return S_FALSE;
        }
    }

    GetLocalTime(&sg_CurrSysTime);
    QueryPerformanceFrequency(&sg_lnFrequency);
    QueryPerformanceCounter(&sg_QueryTickCount);
    sg_TimeStamp = 0;
    sg_bRxRun = TRUE;
    sg_hRxThread = CreateThread(nullptr, 0, ZlgRxThread, nullptr, 0, nullptr);
    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_StopHardware(void)
{
    CZlgDirGuard ouDir;
    sg_bRxRun = FALSE;
    if (sg_hRxThread != nullptr)
    {
        WaitForSingleObject(sg_hRxThread, 1000);
        CloseHandle(sg_hRxThread);
        sg_hRxThread = nullptr;
    }
    for (INT i = 0; i < MAX_ZLG_CHANNELS; i++)
    {
        if (sg_ResetCAN != nullptr && sg_ahChannel[i] != nullptr)
        {
            sg_ResetCAN(sg_ahChannel[i]);
        }
        sg_ahChannel[i] = nullptr;
    }
    for (INT i = 0; i < MAX_ZLG_CHANNELS; i++)
    {
        if (sg_ahDevice[i] == nullptr)
        {
            continue;
        }
        BOOL bClose = TRUE;
        for (INT j = 0; j < i; j++)
        {
            if (sg_ahDevice[j] == sg_ahDevice[i])
            {
                bClose = FALSE;
                break;
            }
        }
        if (bClose)
        {
            ZlgCloseDeviceSafe(sg_ahDevice[i]);
        }
        sg_ahDevice[i] = nullptr;
    }
    if (sg_bCurrState == STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    if (sg_bCurrState != STATE_CONNECTED)
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
    if (nIndex >= sg_nNoOfChannels || sg_ahChannel[nIndex] == nullptr)
    {
        return ERR_INVALID_CHANNEL;
    }

    UINT unSent = 0;
    if (sg_abCanFd[nIndex] && sg_TransmitFD != nullptr)
    {
        ZCAN_TransmitFD_Data tx;
        memset(&tx, 0, sizeof(tx));
        tx.transmit_type = 0;
        tx.frame.can_id = MakeCanId(sCanTxMsg.m_unMsgID, sCanTxMsg.m_ucEXTENDED, sCanTxMsg.m_ucRTR);
        tx.frame.len = sCanTxMsg.m_ucDataLen > 64 ? 64 : sCanTxMsg.m_ucDataLen;
        if (sCanTxMsg.m_bCANFD && sg_abExp[nIndex])
        {
            tx.frame.flags = CANFD_BRS;
        }
        memcpy(tx.frame.data, sCanTxMsg.m_ucData, tx.frame.len);
        unSent = sg_TransmitFD(sg_ahChannel[nIndex], &tx, 1);
    }
    else if (sg_Transmit != nullptr)
    {
        ZCAN_Transmit_Data tx;
        memset(&tx, 0, sizeof(tx));
        tx.transmit_type = 0;
        tx.frame.can_id = MakeCanId(sCanTxMsg.m_unMsgID, sCanTxMsg.m_ucEXTENDED, sCanTxMsg.m_ucRTR);
        tx.frame.can_dlc = sCanTxMsg.m_ucDataLen > 8 ? 8 : sCanTxMsg.m_ucDataLen;
        memcpy(tx.frame.data, sCanTxMsg.m_ucData, tx.frame.can_dlc);
        unSent = sg_Transmit(sg_ahChannel[nIndex], &tx, 1);
    }
    if (unSent == 0)
    {
        return S_FALSE;
    }

    STCANDATA can_data;
    memset(&can_data, 0, sizeof(can_data));
    can_data.m_ucDataType = TX_FLAG;
    can_data.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    QueryPerformanceCounter(&can_data.m_lTickCount);
    EnterCriticalSection(&sg_DIL_CriticalSection);
    vWriteIntoClientsBuffer(can_data);
    LeaveCriticalSection(&sg_DIL_CriticalSection);
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_acErrStr;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
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

HRESULT CDIL_CAN_ZLG_USB::CAN_SetControllerParams(int /*nValue*/, ECONTR_PARAM /*eContrparam*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT /*nChannel*/, ECONTR_PARAM /*eContrParam*/)
{
    sErrorCnt.m_ucTxErrCount = 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_ZLG_USB::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
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

HRESULT CDIL_CAN_ZLG_USB::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
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
