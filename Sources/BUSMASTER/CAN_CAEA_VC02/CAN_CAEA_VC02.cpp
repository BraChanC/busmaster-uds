#include "CAN_CAEA_VC02_stdafx.h"
#include "CAN_CAEA_VC02.h"
#include "resource.h"
#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "Error.h"
#include "DIL_Interface/HardwareListingCAN.h"
#include "DIL_Interface/IChangeRegisters.h"
#include "..\CAEA_VC02\VC02Protocol.h"

#define USAGE_EXPORT
#include "CAN_CAEA_VC02_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_CAEA_VC02App, CWinApp)
END_MESSAGE_MAP()

CCAN_CAEA_VC02App::CCAN_CAEA_VC02App()
{
}

CCAN_CAEA_VC02App theApp;

BOOL CCAN_CAEA_VC02App::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

#define MAX_CLIENT_ALLOWED 16
#define MAX_BUFF_ALLOWED 16

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

struct CChannel
{
    int com;
    int wire;
    void* session;
    unsigned baud;
    unsigned fdBaud;
    int fd;
    int term;
    int listen;
    int resend;
};

static HMODULE sg_hProto = nullptr;
static HWND sg_hOwnerWnd = nullptr;
static std::string sg_acErrStr;
static CRITICAL_SECTION sg_cs;
static BOOL sg_bCritInit = FALSE;
static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;
static UINT sg_unClientCnt = 0;
static SCLIENTBUFMAP sg_asClientToBufMap[MAX_CLIENT_ALLOWED];
static INT sg_nNoOfChannels = 0;
static int sg_family = VC02_FAMILY_VC02;
static CChannel sg_channels[defCHANNEL_CAN_MAX];
static HANDLE sg_hRxThread = nullptr;
static volatile LONG sg_bRxRun = 0;

typedef int (*VC02_FIND)(VC02_DEVICE*, int);
typedef void* (*VC02_OPEN)(int);
typedef void (*VC02_CLOSE)(void*);
typedef int (*VC02_SET_CAN)(void*, int, unsigned, unsigned, int, int, int, int);
typedef int (*VC02_TX_CAN)(void*, int, unsigned, int, int, int, const unsigned char*, int);
typedef int (*VC02_READ)(void*, VC02_MSG*, int, int);
typedef const char* (*VC02_LAST_ERROR)(void);

static VC02_FIND pFind = nullptr;
static VC02_OPEN pOpen = nullptr;
static VC02_CLOSE pClose = nullptr;
static VC02_SET_CAN pSetCan = nullptr;
static VC02_TX_CAN pTxCan = nullptr;
static VC02_READ pRead = nullptr;
static VC02_LAST_ERROR pLastError = nullptr;

class CDIL_CAN_CAEA_VC02;
static CDIL_CAN_CAEA_VC02* g_pouDIL_CAN = nullptr;

class CDIL_CAN_CAEA_VC02 : public CBaseDIL_CAN_Controller
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
    HRESULT CAN_SetHardwareChannel(PSCONTROLLER_DETAILS, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCnt);
};

extern "C" __declspec(dllexport) void SetCaeaFamily(int family)
{
    int next = (family == VC02_FAMILY_VC01) ? VC02_FAMILY_VC01 : VC02_FAMILY_VC02;
    if (sg_family != next)
    {
        if (g_pouDIL_CAN != nullptr && sg_bCurrState == STATE_CONNECTED)
        {
            g_pouDIL_CAN->CAN_StopHardware();
        }
        memset(sg_channels, 0, sizeof(sg_channels));
        sg_nNoOfChannels = 0;
    }
    sg_family = next;
}

USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface)
{
    if (g_pouDIL_CAN == nullptr)
    {
        g_pouDIL_CAN = new CDIL_CAN_CAEA_VC02;
    }
    *ppvInterface = (void*)g_pouDIL_CAN;
    return g_pouDIL_CAN != nullptr ? S_OK : S_FALSE;
}

static BOOL bClientExist(const char* name, INT& index)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (strcmp(name, sg_asClientToBufMap[i].m_acClientName) == 0)
        {
            index = (INT)i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bGetClientObj(DWORD id, UINT& index)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].m_dwClientID == id)
        {
            index = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bClientIdExist(DWORD id)
{
    UINT index = 0;
    return bGetClientObj(id, index);
}

static DWORD dwGetAvailableClientSlot(void)
{
    DWORD id = 2;
    for (INT i = 0; i < MAX_CLIENT_ALLOWED; i++)
    {
        if (bClientIdExist(id))
        {
            id++;
        }
        else
        {
            break;
        }
    }
    return id;
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

static unsigned ParseBaud(const std::string& text, unsigned fallback)
{
    if (text.empty())
    {
        return fallback;
    }
    unsigned value = (unsigned)atoi(text.c_str());
    if (value == 0)
    {
        return fallback;
    }
    if (value < 20000)
    {
        return value * 1000;
    }
    return value;
}

static void ApplyControllerConfig(PSCONTROLLER_DETAILS details, int count)
{
    if (details == nullptr)
    {
        return;
    }
    if (count > sg_nNoOfChannels)
    {
        count = sg_nNoOfChannels;
    }
    for (int i = 0; i < count; i++)
    {
        sg_channels[i].baud = ParseBaud(details[i].m_omStrBaudrate, 500000);
        int allowFd = (sg_family != VC02_FAMILY_VC01 && sg_channels[i].wire <= 4);
        sg_channels[i].fd = (allowFd && details[i].m_bcanFDEnabled) ? 1 : 0;
        sg_channels[i].fdBaud = details[i].m_unDataBitRate ? details[i].m_unDataBitRate : 2000000;
        sg_channels[i].term = (details[i].m_omStrLocation == "0") ? 0 : 1;
        sg_channels[i].listen = (details[i].m_ucControllerMode == 2) ? 1 : 0;
        sg_channels[i].resend = (details[i].m_omStrPropagationDelay == "1") ? 1
            : (details[i].m_omStrPropagationDelay == "2") ? 2 : 0;
    }
}

static LONGLONG TickFromSeconds(double seconds)
{
    return (LONGLONG)(seconds * 10000.0);
}

static void Dispatch(int channelIndex, const VC02_MSG& msg)
{
    if (msg.isTx)
    {
        return;
    }
    if (msg.isErr && msg.dlc <= 0)
    {
        return;
    }
    STCANDATA data;
    memset(&data, 0, sizeof(data));
    data.m_ucDataType = RX_FLAG;
    data.m_uDataInfo.m_sCANMsg.m_unMsgID = msg.id;
    data.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = msg.isExt ? 1 : 0;
    data.m_uDataInfo.m_sCANMsg.m_ucRTR = 0;
    data.m_uDataInfo.m_sCANMsg.m_ucDataLen = (unsigned char)((msg.dlc > 64) ? 64 : msg.dlc);
    data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(channelIndex + 1);
    data.m_uDataInfo.m_sCANMsg.m_bCANFD = msg.isFd ? true : false;
    if (data.m_uDataInfo.m_sCANMsg.m_ucDataLen > 0)
    {
        memcpy(data.m_uDataInfo.m_sCANMsg.m_ucData, msg.data, data.m_uDataInfo.m_sCANMsg.m_ucDataLen);
    }
    data.m_lTickCount.QuadPart = TickFromSeconds(msg.timeSec);
    vWriteIntoClientsBuffer(data);
}

static int ChannelIndex(void* session, int wire)
{
    for (int i = 0; i < sg_nNoOfChannels; i++)
    {
        if (sg_channels[i].session == session && sg_channels[i].wire == wire)
        {
            return i;
        }
    }
    return -1;
}

static DWORD WINAPI RxThread(LPVOID)
{
    VC02_MSG batch[64];
    while (sg_bRxRun)
    {
        int gotAny = 0;
        void* seen[defCHANNEL_CAN_MAX];
        int seenCount = 0;
        for (int i = 0; i < sg_nNoOfChannels && sg_bRxRun; i++)
        {
            void* session = sg_channels[i].session;
            if (session == nullptr || pRead == nullptr)
            {
                continue;
            }
            int already = 0;
            for (int n = 0; n < seenCount; n++)
            {
                if (seen[n] == session)
                {
                    already = 1;
                    break;
                }
            }
            if (already)
            {
                continue;
            }
            seen[seenCount++] = session;
            int count = pRead(session, batch, 64, VC02_KIND_CAN);
            if (count > 0)
            {
                gotAny = 1;
                EnterCriticalSection(&sg_cs);
                for (int n = 0; n < count; n++)
                {
                    if (batch[n].kind != VC02_KIND_CAN)
                    {
                        continue;
                    }
                    int index = ChannelIndex(session, batch[n].channel);
                    if (index >= 0)
                    {
                        Dispatch(index, batch[n]);
                    }
                }
                LeaveCriticalSection(&sg_cs);
            }
        }
        if (!gotAny)
        {
            Sleep(1);
        }
    }
    return 0;
}

static void CloseSessions(void)
{
    void* seen[defCHANNEL_CAN_MAX];
    int seenCount = 0;
    for (int i = 0; i < sg_nNoOfChannels; i++)
    {
        void* session = sg_channels[i].session;
        sg_channels[i].session = nullptr;
        if (session == nullptr || pClose == nullptr)
        {
            continue;
        }
        int already = 0;
        for (int n = 0; n < seenCount; n++)
        {
            if (seen[n] == session)
            {
                already = 1;
                break;
            }
        }
        if (!already)
        {
            seen[seenCount++] = session;
            pClose(session);
        }
    }
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_PerformInitOperations(void)
{
    if (!sg_bCritInit)
    {
        InitializeCriticalSection(&sg_cs);
        sg_bCritInit = TRUE;
    }
    memset(sg_channels, 0, sizeof(sg_channels));
    sg_nNoOfChannels = 0;
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_PerformClosureOperations(void)
{
    CAN_StopHardware();
    if (sg_bCritInit)
    {
        DeleteCriticalSection(&sg_cs);
        sg_bCritInit = FALSE;
    }
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    GetLocalTime(&CurrSysTime);
    TimeStamp = 0;
    QueryPerformanceCounter(&QueryTickCount);
    return S_OK;
}

static const unsigned kArbBauds[] =
{
    10000, 20000, 33333, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000
};
static const unsigned kDataBauds[] =
{
    500000, 1000000, 2000000, 4000000, 5000000, 8000000
};

static void FillChoiceCombo(CComboBox& box, const char* const* labels, const int* values, int count, int current)
{
    int select = 0;
    for (int i = 0; i < count; i++)
    {
        int item = box.AddString(labels[i]);
        box.SetItemData(item, (DWORD)values[i]);
        if (values[i] == current)
        {
            select = item;
        }
    }
    box.SetCurSel(select);
}

static int ChoiceValue(CComboBox& box, int fallback)
{
    int item = box.GetCurSel();
    if (item < 0)
    {
        return fallback;
    }
    return (int)box.GetItemData(item);
}

static void FillBaudCombo(CComboBox& box, const unsigned* values, int count, unsigned current)
{
    int select = -1;
    for (int i = 0; i < count; i++)
    {
        CString text;
        text.Format("%u", values[i]);
        int item = box.AddString(text);
        if (values[i] == current)
        {
            select = item;
        }
    }
    if (select < 0)
    {
        CString text;
        text.Format("%u", current);
        select = box.AddString(text);
    }
    box.SetCurSel(select);
}

class CVc02BaudDlg : public CDialog, public IChangeRegisters
{
public:
    CVc02BaudDlg()
        : CDialog(IDD_VC02_BAUD, nullptr)
        , m_p(nullptr)
        , m_nCount(0)
        , m_nSel(0)
    {
    }
    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSel)
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_p = pControllerDetails;
        m_nCount = nCount;
        m_nSel = nSel;
        if (m_p == nullptr || m_nCount == 0)
        {
            return 0;
        }
        if (m_nSel >= m_nCount)
        {
            m_nSel = 0;
        }
        return (DoModal() == IDOK) ? 1 : 0;
    }
    double vValidateBaudRate(double dBaud, int, UINT)
    {
        return dBaud;
    }
protected:
    BOOL OnInitDialog()
    {
        CDialog::OnInitDialog();
        SetWindowText(sg_family == VC02_FAMILY_VC01 ? "VC01" : "VC02 Baud");
        unsigned arb = ParseBaud(m_p[m_nSel].m_omStrBaudrate, 500000);
        unsigned data = m_p[m_nSel].m_unDataBitRate ? m_p[m_nSel].m_unDataBitRate : 2000000;
        CComboBox* arbBox = (CComboBox*)GetDlgItem(IDC_VC02_ARB_BAUD);
        CComboBox* dataBox = (CComboBox*)GetDlgItem(IDC_VC02_DATA_BAUD);
        if (arbBox != nullptr)
        {
            FillBaudCombo(*arbBox, kArbBauds, _countof(kArbBauds), arb);
        }
        if (dataBox != nullptr)
        {
            FillBaudCombo(*dataBox, kDataBauds, _countof(kDataBauds), data);
            dataBox->EnableWindow(m_p[m_nSel].m_bcanFDEnabled ? TRUE : FALSE);
        }
        static const char* modeLabels[] = { "Normal", "Listen only" };
        static const int modeValues[] = { 0, 1 };
        static const char* resendLabels[] = { "300 ms", "Unlimited", "No resend" };
        static const int resendValues[] = { 0, 1, 2 };
        int listen = (m_p[m_nSel].m_ucControllerMode == 2) ? 1 : 0;
        int resend = (m_p[m_nSel].m_omStrPropagationDelay == "1") ? 1
            : (m_p[m_nSel].m_omStrPropagationDelay == "2") ? 2 : 0;
        CComboBox* modeBox = (CComboBox*)GetDlgItem(IDC_VC02_MODE);
        CComboBox* resendBox = (CComboBox*)GetDlgItem(IDC_VC02_RESEND);
        if (modeBox != nullptr)
        {
            FillChoiceCombo(*modeBox, modeLabels, modeValues, _countof(modeValues), listen);
        }
        if (resendBox != nullptr)
        {
            FillChoiceCombo(*resendBox, resendLabels, resendValues, _countof(resendValues), resend);
        }
        return TRUE;
    }
    void OnOK()
    {
        CString arbText;
        CString dataText;
        GetDlgItemText(IDC_VC02_ARB_BAUD, arbText);
        GetDlgItemText(IDC_VC02_DATA_BAUD, dataText);
        unsigned arb = ParseBaud(std::string((LPCSTR)arbText), 0);
        unsigned data = ParseBaud(std::string((LPCSTR)dataText), 0);
        if (arb < 5000 || arb > 1000000)
        {
            AfxMessageBox("Arbitration baud must be from 5000 to 1000000 bps.");
            return;
        }
        if (m_p[m_nSel].m_bcanFDEnabled && (data < 100000 || data > 8000000))
        {
            AfxMessageBox("Data baud must be from 100000 to 8000000 bps.");
            return;
        }
        char text[32];
        sprintf_s(text, "%u", arb);
        m_p[m_nSel].m_omStrBaudrate = text;
        if (data != 0)
        {
            m_p[m_nSel].m_unDataBitRate = data;
        }
        CComboBox* modeBox = (CComboBox*)GetDlgItem(IDC_VC02_MODE);
        CComboBox* resendBox = (CComboBox*)GetDlgItem(IDC_VC02_RESEND);
        int listen = (modeBox != nullptr) ? ChoiceValue(*modeBox, 0) : 0;
        int resend = (resendBox != nullptr) ? ChoiceValue(*resendBox, 0) : 0;
        m_p[m_nSel].m_ucControllerMode = (listen == 1) ? 2 : 1;
        char resendText[8];
        sprintf_s(resendText, "%d", (resend < 0 || resend > 2) ? 0 : resend);
        m_p[m_nSel].m_omStrPropagationDelay = resendText;
        CDialog::OnOK();
    }
private:
    PSCONTROLLER_DETAILS m_p;
    UINT m_nCount;
    UINT m_nSel;
};

HRESULT CDIL_CAN_CAEA_VC02::CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (pFind == nullptr)
    {
        sg_acErrStr = "CAEA_VC02.dll is not loaded";
        return ERR_LOAD_DRIVER;
    }
    VC02_DEVICE devices[8];
    int deviceCount = pFind(devices, 8);
    VC02_DEVICE matched[8];
    int matchedCount = 0;
    for (int d = 0; d < deviceCount && matchedCount < 8; d++)
    {
        if (devices[d].family == sg_family)
        {
            matched[matchedCount++] = devices[d];
        }
    }
    const char* title = (sg_family == VC02_FAMILY_VC01) ? "CAEA VC01" : "CAEA VC02";
    if (matchedCount <= 0)
    {
        nCount = 0;
        sg_acErrStr = (sg_family == VC02_FAMILY_VC01) ? "No VC01 serial device answered" : "No VC02 serial device answered";
        MessageBox(sg_hOwnerWnd, sg_acErrStr.c_str(), title, MB_OK | MB_ICONERROR);
        return NO_HW_INTERFACE;
    }

    INTERFACE_HW asFound[defCHANNEL_CAN_MAX];
    INT nFound = 0;
    for (int d = 0; d < matchedCount && nFound < (INT)defCHANNEL_CAN_MAX; d++)
    {
        int canCount = matched[d].canCount;
        if (canCount > 5)
        {
            canCount = 5;
        }
        for (int ch = 1; ch <= canCount && nFound < (INT)defCHANNEL_CAN_MAX; ch++)
        {
            char desc[160];
            sprintf_s(desc, "CAEA %s COM%d CAN%d", matched[d].version, matched[d].com, ch);
            asFound[nFound].m_dwIdInterface = (unsigned long)ch;
            asFound[nFound].m_dwVendor = (unsigned long)matched[d].com;
            asFound[nFound].m_bytNetworkID = (unsigned char)ch;
            asFound[nFound].m_acNameInterface = title;
            asFound[nFound].m_acDeviceName = matched[d].version;
            asFound[nFound].m_acDescription = desc;
            int allowFd = (sg_family != VC02_FAMILY_VC01 && ch <= 4);
            if (InitData != nullptr && !allowFd)
            {
                InitData[nFound].m_bSupportCANFD = false;
                InitData[nFound].m_bcanFDEnabled = false;
            }
            if (InitData != nullptr && sg_nNoOfChannels == 0)
            {
                InitData[nFound].m_bSupportCANFD = allowFd ? true : false;
                InitData[nFound].m_bcanFDEnabled = allowFd ? true : false;
                if (InitData[nFound].m_omStrBaudrate.empty())
                {
                    InitData[nFound].m_omStrBaudrate = "500000";
                }
                if (InitData[nFound].m_unDataBitRate == 0)
                {
                    InitData[nFound].m_unDataBitRate = 2000000;
                }
                if (InitData[nFound].m_omStrLocation.empty())
                {
                    InitData[nFound].m_omStrLocation = "1";
                }
            }
            nFound++;
        }
    }
    if (nFound <= 0)
    {
        return NO_HW_INTERFACE;
    }

    int anSel[CHANNEL_ALLOWED];
    for (int i = 0; i < CHANNEL_ALLOWED; i++)
    {
        anSel[i] = -1;
    }
    int restored = 0;
    for (int i = 0; i < sg_nNoOfChannels && restored < CHANNEL_ALLOWED; i++)
    {
        for (int j = 0; j < nFound; j++)
        {
            if ((int)asFound[j].m_dwVendor == sg_channels[i].com
                && (int)asFound[j].m_bytNetworkID == sg_channels[i].wire)
            {
                anSel[restored++] = j;
                break;
            }
        }
    }
    CWnd owner;
    CWnd* parent = nullptr;
    if (sg_hOwnerWnd != nullptr)
    {
        owner.Attach(sg_hOwnerWnd);
        parent = &owner;
    }
    CVc02BaudDlg advance;
    CHardwareListingCAN dialog(asFound, nFound, anSel, CAN, nFound, parent, InitData, &advance);
    INT result = dialog.DoModal();
    if (parent != nullptr)
    {
        owner.Detach();
    }
    if (result != IDOK)
    {
        return HW_INTERFACE_NO_SEL;
    }
    nCount = dialog.nGetSelectedList(anSel);
    if (nCount <= 0)
    {
        return HW_INTERFACE_NO_SEL;
    }
    if (nCount > nFound)
    {
        nCount = nFound;
    }
    sg_nNoOfChannels = nCount;
    for (int i = 0; i < nCount; i++)
    {
        int src = anSel[i];
        if (src < 0 || src >= nFound)
        {
            src = i;
        }
        sSelHwInterface[i] = asFound[src];
        sg_channels[i].com = (int)asFound[src].m_dwVendor;
        sg_channels[i].wire = (int)asFound[src].m_bytNetworkID;
        sg_channels[i].session = nullptr;
        sg_channels[i].baud = 500000;
        sg_channels[i].fdBaud = 2000000;
        sg_channels[i].fd = (sg_family != VC02_FAMILY_VC01 && sg_channels[i].wire <= 4) ? 1 : 0;
        sg_channels[i].term = 1;
        sg_channels[i].listen = 0;
        sg_channels[i].resend = 0;
    }
    ApplyControllerConfig(InitData, nCount);
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount)
{
    if (nCount <= 0)
    {
        return S_FALSE;
    }
    if (nCount > (INT)defCHANNEL_CAN_MAX)
    {
        nCount = defCHANNEL_CAN_MAX;
    }
    sg_nNoOfChannels = nCount;
    for (int i = 0; i < nCount; i++)
    {
        sg_channels[i].com = (int)sSelHwInterface[i].m_dwVendor;
        sg_channels[i].wire = (int)sSelHwInterface[i].m_bytNetworkID;
        sg_channels[i].session = nullptr;
    }
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_DeselectHwInterface(void)
{
    CAN_StopHardware();
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    ApplyControllerConfig(InitData, Length > 0 ? Length : 1);
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS details, DWORD, bool, unsigned int unChannelCount)
{
    if (unChannelCount > 0 && unChannelCount <= defCHANNEL_CAN_MAX)
    {
        sg_nNoOfChannels = (INT)unChannelCount;
    }
    if (details != nullptr)
    {
        ApplyControllerConfig(details, (int)unChannelCount);
    }
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_StartHardware(void)
{
    if (pOpen == nullptr || sg_nNoOfChannels <= 0)
    {
        sg_acErrStr = "No VC02 channel selected";
        return S_FALSE;
    }
    CAN_StopHardware();
    for (int i = 0; i < sg_nNoOfChannels; i++)
    {
        void* session = nullptr;
        for (int j = 0; j < i; j++)
        {
            if (sg_channels[j].com == sg_channels[i].com && sg_channels[j].session != nullptr)
            {
                session = sg_channels[j].session;
                break;
            }
        }
        if (session == nullptr)
        {
            session = pOpen(sg_channels[i].com);
            if (session == nullptr)
            {
                sg_acErrStr = pLastError != nullptr ? pLastError() : "VC02 open failed";
                CloseSessions();
                return ERR_LOAD_HW_INTERFACE;
            }
        }
        sg_channels[i].session = session;
    }
    for (int i = 0; i < sg_nNoOfChannels; i++)
    {
        if (pSetCan != nullptr)
        {
            pSetCan(sg_channels[i].session, sg_channels[i].wire, sg_channels[i].baud,
                    sg_channels[i].fdBaud, sg_channels[i].fd, sg_channels[i].term,
                    sg_channels[i].listen, sg_channels[i].resend);
        }
    }
    sg_bRxRun = 1;
    sg_hRxThread = CreateThread(nullptr, 0, RxThread, nullptr, 0, nullptr);
    if (sg_hRxThread == nullptr)
    {
        sg_bRxRun = 0;
        CloseSessions();
        return ERR_LOAD_HW_INTERFACE;
    }
    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_StopHardware(void)
{
    sg_bRxRun = 0;
    if (sg_hRxThread != nullptr)
    {
        WaitForSingleObject(sg_hRxThread, 1000);
        CloseHandle(sg_hRxThread);
        sg_hRxThread = nullptr;
    }
    CloseSessions();
    if (sg_bCurrState == STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    if (sg_bCurrState != STATE_CONNECTED)
    {
        return ERR_IMPROPER_STATE;
    }
    if (!bClientIdExist(dwClientID))
    {
        return ERR_NO_CLIENT_EXIST;
    }
    int index = (int)sCanTxMsg.m_ucChannel - 1;
    if (index < 0 || index >= sg_nNoOfChannels || sg_channels[index].session == nullptr || pTxCan == nullptr)
    {
        return ERR_INVALID_CHANNEL;
    }
    int dlc = sCanTxMsg.m_ucDataLen;
    int fd = (sCanTxMsg.m_bCANFD && sg_channels[index].fd && sg_channels[index].wire <= 4) ? 1 : 0;
    if (!fd && dlc > 8)
    {
        dlc = 8;
    }
    if (dlc < 0)
    {
        dlc = 0;
    }
    if (fd && dlc > 64)
    {
        dlc = 64;
    }
    if (!pTxCan(sg_channels[index].session, sg_channels[index].wire, sCanTxMsg.m_unMsgID,
                sCanTxMsg.m_ucEXTENDED ? 1 : 0, fd, fd, sCanTxMsg.m_ucData, dlc))
    {
        return S_FALSE;
    }
    STCANDATA data;
    memset(&data, 0, sizeof(data));
    data.m_ucDataType = TX_FLAG;
    data.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    data.m_uDataInfo.m_sCANMsg.m_ucChannel = (unsigned char)(index + 1);
    data.m_uDataInfo.m_sCANMsg.m_bCANFD = fd ? true : false;
    LARGE_INTEGER tick;
    QueryPerformanceCounter(&tick);
    data.m_lTickCount = tick;
    EnterCriticalSection(&sg_cs);
    vWriteIntoClientsBuffer(data);
    LeaveCriticalSection(&sg_cs);
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_acErrStr;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = sg_nNoOfChannels;
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

HRESULT CDIL_CAN_CAEA_VC02::CAN_SetControllerParams(int, ECONTR_PARAM)
{
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT, ECONTR_PARAM)
{
    sErrorCnt.m_ucTxErrCount = 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    return S_OK;
}

static BOOL bRemoveClientBuffer(CBaseCANBufFSE* buffers[], UINT& count, CBaseCANBufFSE* target)
{
    for (UINT i = 0; i < count; i++)
    {
        if (buffers[i] == target)
        {
            for (UINT j = i; j + 1 < count; j++)
            {
                buffers[j] = buffers[j + 1];
            }
            count--;
            buffers[count] = nullptr;
            return TRUE;
        }
    }
    return FALSE;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    UINT index = 0;
    if (ClientID != 0)
    {
        if (!bGetClientObj(ClientID, index))
        {
            return ERR_NO_CLIENT_EXIST;
        }
        SCLIENTBUFMAP& client = sg_asClientToBufMap[index];
        if (bAction == MSGBUF_ADD)
        {
            if (pBufObj == nullptr || client.m_unBufCount >= MAX_BUFF_ALLOWED)
            {
                return ERR_BUFFER_EXISTS;
            }
            for (UINT i = 0; i < client.m_unBufCount; i++)
            {
                if (client.m_pClientBuf[i] == pBufObj)
                {
                    return ERR_BUFFER_EXISTS;
                }
            }
            client.m_pClientBuf[client.m_unBufCount++] = pBufObj;
            return S_OK;
        }
        if (bAction == MSGBUF_CLEAR)
        {
            if (pBufObj != nullptr)
            {
                bRemoveClientBuffer(client.m_pClientBuf, client.m_unBufCount, pBufObj);
            }
            else
            {
                client.m_unBufCount = 0;
            }
            return S_OK;
        }
    }
    else if (bAction == MSGBUF_CLEAR)
    {
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            sg_asClientToBufMap[i].m_unBufCount = 0;
        }
        return S_OK;
    }
    return S_FALSE;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    if (bRegister)
    {
        if (sg_unClientCnt >= MAX_CLIENT_ALLOWED)
        {
            return ERR_NO_MORE_CLIENT_ALLOWED;
        }
        INT index = 0;
        if (pacClientName != nullptr && bClientExist(pacClientName, index))
        {
            ClientID = sg_asClientToBufMap[index].m_dwClientID;
            return ERR_CLIENT_EXISTS;
        }
        if (pacClientName != nullptr && strcmp(pacClientName, CAN_MONITOR_NODE) == 0)
        {
            ClientID = 1;
            strcpy_s(sg_asClientToBufMap[sg_unClientCnt].m_acClientName, pacClientName);
            sg_asClientToBufMap[sg_unClientCnt].m_dwClientID = ClientID;
            sg_asClientToBufMap[sg_unClientCnt].m_unBufCount = 0;
        }
        else
        {
            ClientID = dwGetAvailableClientSlot();
            if (pacClientName != nullptr)
            {
                strcpy_s(sg_asClientToBufMap[sg_unClientCnt].m_acClientName, pacClientName);
            }
            sg_asClientToBufMap[sg_unClientCnt].m_dwClientID = ClientID;
            sg_asClientToBufMap[sg_unClientCnt].m_unBufCount = 0;
        }
        sg_unClientCnt++;
        return S_OK;
    }
    UINT index = 0;
    if (!bGetClientObj(ClientID, index))
    {
        return ERR_NO_CLIENT_EXIST;
    }
    sg_asClientToBufMap[index] = sg_asClientToBufMap[sg_unClientCnt - 1];
    sg_unClientCnt--;
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_GetCntrlStatus(const HANDLE&, UINT&)
{
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_LoadDriverLibrary(void)
{
    if (sg_hProto != nullptr)
    {
        return S_OK;
    }
    sg_hProto = LoadLibrary("CAEA_VC02.dll");
    if (sg_hProto == nullptr)
    {
        sg_acErrStr = "CAEA_VC02.dll was not found next to BUSMASTER.exe";
        return ERR_LOAD_DRIVER;
    }
    pFind = (VC02_FIND)GetProcAddress(sg_hProto, "vc02_find");
    pOpen = (VC02_OPEN)GetProcAddress(sg_hProto, "vc02_open");
    pClose = (VC02_CLOSE)GetProcAddress(sg_hProto, "vc02_close");
    pSetCan = (VC02_SET_CAN)GetProcAddress(sg_hProto, "vc02_set_can");
    pTxCan = (VC02_TX_CAN)GetProcAddress(sg_hProto, "vc02_tx_can");
    pRead = (VC02_READ)GetProcAddress(sg_hProto, "vc02_read");
    pLastError = (VC02_LAST_ERROR)GetProcAddress(sg_hProto, "vc02_last_error");
    if (pFind == nullptr || pOpen == nullptr || pClose == nullptr || pSetCan == nullptr || pTxCan == nullptr || pRead == nullptr)
    {
        FreeLibrary(sg_hProto);
        sg_hProto = nullptr;
        sg_acErrStr = "CAEA_VC02.dll is missing the serial exports";
        return ERR_LOAD_DRIVER;
    }
    return S_OK;
}

HRESULT CDIL_CAN_CAEA_VC02::CAN_UnloadDriverLibrary(void)
{
    CAN_StopHardware();
    if (sg_hProto != nullptr)
    {
        FreeLibrary(sg_hProto);
        sg_hProto = nullptr;
    }
    pFind = nullptr;
    pOpen = nullptr;
    pClose = nullptr;
    pSetCan = nullptr;
    pTxCan = nullptr;
    pRead = nullptr;
    pLastError = nullptr;
    return S_OK;
}
