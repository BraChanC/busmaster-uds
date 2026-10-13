#include "LIN_CAEA_VC02_stdafx.h"
#include "LIN_CAEA_VC02.h"
#include "resource.h"
#include "DIL_Interface\HardwareListing.h"
#include "DIL_Interface\CommanDIL_LIN.h"
#include "Error.h"
#include "..\CAEA_VC02\VC02Protocol.h"

#define USAGE_EXPORT
#include "LIN_CAEA_VC02_Extern.h"

const unsigned int DEFAULT_LIN_BAUD = 19200;

enum
{
    STATE_DRIVER_SELECTED = 0x0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};

static HMODULE sg_hProto = nullptr;
static HWND sg_hOwnerWnd = nullptr;
static std::string sg_acErrStr;
static CRITICAL_SECTION sg_cs;
static BOOL sg_bCritInit = FALSE;
static BOOL sg_bIsConnected = FALSE;
static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;
static UINT sg_nNoOfChannels = 0;
static int sg_family = VC02_FAMILY_VC02;
static HANDLE sg_hRxThread = nullptr;
static volatile LONG sg_bRxRun = 0;
static STLINDATA sg_asLINMsg;
static unsigned char sg_aucSlaveData[CHANNEL_ALLOWED][64][8];
static unsigned char sg_aucSlaveLen[CHANNEL_ALLOWED][64];
static unsigned char sg_aucSlaveValid[CHANNEL_ALLOWED][64];

struct CChannel
{
    int com;
    int wire;
    void* session;
    unsigned int baud;
    int master;
    char name[256];
};

static CChannel sg_channels[defNO_OF_CHANNELS];

typedef int (*VC02_FIND)(VC02_DEVICE*, int);
typedef void* (*VC02_OPEN)(int);
typedef void (*VC02_CLOSE)(void*);
typedef int (*VC02_SET_LIN)(void*, int, unsigned, int);
typedef int (*VC02_TX_LIN)(void*, int, unsigned, const unsigned char*, int);
typedef int (*VC02_READ)(void*, VC02_MSG*, int, int);
typedef const char* (*VC02_LAST_ERROR)(void);

static VC02_FIND pFind = nullptr;
static VC02_OPEN pOpen = nullptr;
static VC02_CLOSE pClose = nullptr;
static VC02_SET_LIN pSetLin = nullptr;
static VC02_TX_LIN pTxLin = nullptr;
static VC02_READ pRead = nullptr;
static VC02_LAST_ERROR pLastError = nullptr;

class CDIL_LIN_CAEA_VC02;
static CDIL_LIN_CAEA_VC02* g_pouDIL_LIN = nullptr;

class CDIL_LIN_CAEA_VC02 : public CCommanDIL_LIN
{
private:
    ECONTR_STATUS m_Ctrl_Status;
public:
    CDIL_LIN_CAEA_VC02()
    {
        m_Ctrl_Status = RESET_STATE;
    }
    HRESULT LIN_PerformInitOperations(void);
    HRESULT PerformClosureOperations(void);
    HRESULT LIN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT LIN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount);
    HRESULT LIN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT LIN_DeselectHwInterface(void);
    HRESULT SetConfigData(IBMNetWorkGetService* ouConfig);
    HRESULT StartHardware(void);
    HRESULT PreStartHardware(void);
    HRESULT StopHardware(void);
    HRESULT LIN_ResetHardware(void);
    HRESULT LIN_GetCurrStatus(STATUSMSG& StatusData);
    HRESULT LIN_GetTxMsgBuffer(BYTE*& pouFlxTxMsgBuffer);
    HRESULT LIN_Send(STLIN_MSG& sLinTxMsg);
    HRESULT LIN_SetSlaveRespData(const STLIN_MSG stRespMsg);
    HRESULT LIN_ResetSlaveRespData(void);
    HRESULT LIN_DisableSlave(STLIN_MSG& sMessage);
    HRESULT LIN_GetBusConfigInfo(BYTE* BusInfo);
    HRESULT LIN_GetLastErrorString(std::string& acErrorStr);
    HRESULT LIN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT LIN_GetConfiguration(SCONTROLLER_DETAILS_LIN[], INT& nSize);
    HRESULT LIN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam);
    HRESULT LIN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT LIN_SetAppParams(HWND hWndOwner);
    HRESULT LIN_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus);
    HRESULT LIN_LoadDriverLibrary(void);
    HRESULT LIN_UnloadDriverLibrary(void);
    void ProcessLINMsg(const VC02_MSG& msg, int nChannel);
};

extern "C" __declspec(dllexport) void SetCaeaFamily(int family)
{
    int next = (family == VC02_FAMILY_VC01) ? VC02_FAMILY_VC01 : VC02_FAMILY_VC02;
    if (sg_family != next)
    {
        if (g_pouDIL_LIN != nullptr && sg_bIsConnected)
        {
            g_pouDIL_LIN->StopHardware();
        }
        memset(sg_channels, 0, sizeof(sg_channels));
        sg_nNoOfChannels = 0;
    }
    sg_family = next;
}

BEGIN_MESSAGE_MAP(CLIN_CAEA_VC02, CWinApp)
END_MESSAGE_MAP()

CLIN_CAEA_VC02::CLIN_CAEA_VC02()
{
}

CLIN_CAEA_VC02 theApp;

BOOL CLIN_CAEA_VC02::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

USAGEMODE HRESULT GetIDIL_LIN_Controller(void** ppvInterface)
{
    if (g_pouDIL_LIN == nullptr)
    {
        g_pouDIL_LIN = new CDIL_LIN_CAEA_VC02;
    }
    *ppvInterface = (void*)g_pouDIL_LIN;
    return g_pouDIL_LIN != nullptr ? S_OK : S_FALSE;
}

static void vClearSlaveTable(void)
{
    memset(sg_aucSlaveValid, 0, sizeof(sg_aucSlaveValid));
    memset(sg_aucSlaveLen, 0, sizeof(sg_aucSlaveLen));
    memset(sg_aucSlaveData, 0, sizeof(sg_aucSlaveData));
}

static void CloseSessions(void)
{
    void* seen[defNO_OF_CHANNELS];
    int seenCount = 0;
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
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

class CVc02LinListing : public CHardwareListing
{
public:
    CVc02LinListing(INTERFACE_HW* interfaces, int count, int* selected, int* master, CWnd* parent)
        : CHardwareListing(interfaces, count, selected, LIN, CHANNEL_ALLOWED, parent)
        , m_master(master)
        , m_count(count)
        , m_lastIndex(-1)
    {
    }
protected:
    BOOL OnInitDialog()
    {
        BOOL result = CHardwareListing::OnInitDialog();
        SyncSlaveCheck();
        return result;
    }
    void OnOK()
    {
        SaveSlaveCheck();
        CHardwareListing::OnOK();
    }
    void OnItemchangedLstcSelectedHwList(NMHDR* pNMHDR, LRESULT* pResult)
    {
        SaveSlaveCheck();
        CHardwareListing::OnItemchangedLstcSelectedHwList(pNMHDR, pResult);
        SyncSlaveCheck();
    }
    DECLARE_MESSAGE_MAP()
private:
    void SaveSlaveCheck()
    {
        if (m_master == nullptr || m_lastIndex < 0 || m_lastIndex >= m_count)
        {
            return;
        }
        CButton* box = (CButton*)GetDlgItem(IDC_VC02_LIN_SLAVE);
        if (box == nullptr || box->IsWindowEnabled() == FALSE)
        {
            return;
        }
        m_master[m_lastIndex] = (box->GetCheck() == BST_CHECKED) ? 0 : 1;
    }
    void SyncSlaveCheck()
    {
        CButton* box = (CButton*)GetDlgItem(IDC_VC02_LIN_SLAVE);
        if (box == nullptr)
        {
            return;
        }
        POSITION pos = m_omSelectedHwList.GetFirstSelectedItemPosition();
        if (pos == nullptr)
        {
            m_lastIndex = -1;
            box->SetCheck(BST_UNCHECKED);
            box->EnableWindow(FALSE);
            return;
        }
        int item = m_omSelectedHwList.GetNextSelectedItem(pos);
        int index = (int)m_omSelectedHwList.GetItemData(item);
        if (index < 0 || index >= m_count)
        {
            return;
        }
        m_lastIndex = index;
        box->EnableWindow(TRUE);
        box->SetCheck((m_master[index] == 0) ? BST_CHECKED : BST_UNCHECKED);
    }
    int* m_master;
    int m_count;
    int m_lastIndex;
};

BEGIN_MESSAGE_MAP(CVc02LinListing, CHardwareListing)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LSTC_SELECTED_HW_LIST, OnItemchangedLstcSelectedHwList)
END_MESSAGE_MAP()

static int ListHardwareInterfaces(HWND parentHwnd, INTERFACE_HW* interfaces, int* selected, int* master, int& count)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd owner;
    CWnd* parent = nullptr;
    if (parentHwnd != nullptr)
    {
        owner.Attach(parentHwnd);
        parent = &owner;
    }
    CVc02LinListing dialog(interfaces, count, selected, master, parent);
    INT result = dialog.DoModal();
    if (parent != nullptr)
    {
        owner.Detach();
    }
    if (result == IDOK)
    {
        count = dialog.nGetSelectedList(selected);
        return 0;
    }
    return -1;
}

void CDIL_LIN_CAEA_VC02::ProcessLINMsg(const VC02_MSG& msg, int nChannel)
{
    if (nChannel < 0 || nChannel >= (int)sg_nNoOfChannels)
    {
        return;
    }
    memset(&sg_asLINMsg, 0, sizeof(sg_asLINMsg));
    sg_asLINMsg.m_lTickCount.QuadPart = (LONGLONG)(msg.timeSec * 10000.0);
    unsigned char id = (unsigned char)(msg.id & 0x3F);
    unsigned char channel = (unsigned char)(nChannel + 1);
    sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ucChannel = channel;
    sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_ucChannel = channel;
    sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ulTimeStamp = (unsigned long)sg_asLINMsg.m_lTickCount.QuadPart;
    sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_ulTimeStamp = sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ulTimeStamp;
    sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ucMsgID = id;
    sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_ucId = id;
    sg_asLINMsg.m_ucDataType = msg.isTx ? (UCHAR)TX_FLAG : (UCHAR)RX_FLAG;

    if (msg.errCode == 1)
    {
        sg_asLINMsg.m_eLinMsgType = LIN_EVENT;
        sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_eEventType = EVENT_LIN_ERRCRC;
    }
    else if (msg.errCode == 2)
    {
        sg_asLINMsg.m_eLinMsgType = LIN_EVENT;
        sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_eEventType = EVENT_LIN_ERRBIT;
    }
    else if (msg.errCode == 3 || (msg.dlc == 0 && !msg.isTx))
    {
        if (!msg.isTx && nChannel < CHANNEL_ALLOWED && id < 64 && sg_aucSlaveValid[nChannel][id] && pTxLin != nullptr)
        {
            EnterCriticalSection(&sg_cs);
            pTxLin(sg_channels[nChannel].session, sg_channels[nChannel].wire, id,
                   sg_aucSlaveData[nChannel][id], sg_aucSlaveLen[nChannel][id]);
            LeaveCriticalSection(&sg_cs);
        }
        sg_asLINMsg.m_eLinMsgType = LIN_EVENT;
        sg_asLINMsg.m_uDataInfo.m_sErrInfo.m_eEventType = EVENT_LIN_ERRNOANS;
    }
    else
    {
        unsigned char len = (unsigned char)((msg.dlc > 8) ? 8 : msg.dlc);
        sg_asLINMsg.m_eLinMsgType = LIN_MSG;
        sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ucDataLen = len;
        if (len > 0)
        {
            memcpy(sg_asLINMsg.m_uDataInfo.m_sLINMsg.m_ucData, msg.data, len);
        }
    }
    vWriteIntoClientsBuffer(sg_asLINMsg);
}

static DWORD WINAPI RxThread(LPVOID param)
{
    CDIL_LIN_CAEA_VC02* self = (CDIL_LIN_CAEA_VC02*)param;
    VC02_MSG batch[64];
    while (sg_bRxRun)
    {
        int gotAny = 0;
        void* seen[defNO_OF_CHANNELS];
        int seenCount = 0;
        for (UINT i = 0; i < sg_nNoOfChannels && sg_bRxRun; i++)
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
            int count = pRead(session, batch, 64, VC02_KIND_LIN);
            if (count > 0)
            {
                gotAny = 1;
                for (int n = 0; n < count; n++)
                {
                    for (UINT ch = 0; ch < sg_nNoOfChannels; ch++)
                    {
                        if (sg_channels[ch].session == session && sg_channels[ch].wire == batch[n].channel)
                        {
                            self->ProcessLINMsg(batch[n], (int)ch);
                            break;
                        }
                    }
                }
            }
        }
        if (!gotAny)
        {
            Sleep(1);
        }
    }
    return 0;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_PerformInitOperations(void)
{
    if (!sg_bCritInit)
    {
        InitializeCriticalSection(&sg_cs);
        sg_bCritInit = TRUE;
    }
    vClearSlaveTable();
    m_Ctrl_Status = INITIALISED;
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::PerformClosureOperations(void)
{
    StopHardware();
    if (sg_bCritInit)
    {
        DeleteCriticalSection(&sg_cs);
        sg_bCritInit = FALSE;
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    GetLocalTime(&CurrSysTime);
    TimeStamp = 0;
    QueryPerformanceCounter(&QueryTickCount);
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_ListHwInterfaces(INTERFACE_HW_LIST&, INT& nCount)
{
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
        sg_acErrStr = (sg_family == VC02_FAMILY_VC01) ? "No VC01 serial device answered" : "No VC02 serial device answered";
        MessageBox(sg_hOwnerWnd, sg_acErrStr.c_str(), title, MB_OK | MB_ICONERROR);
        return NO_HW_INTERFACE;
    }
    INTERFACE_HW found[defNO_OF_CHANNELS];
    int foundCount = 0;
    for (int d = 0; d < matchedCount && foundCount < defNO_OF_CHANNELS; d++)
    {
        int linCount = matched[d].linCount;
        if (linCount > 4)
        {
            linCount = 4;
        }
        for (int ch = 1; ch <= linCount && foundCount < defNO_OF_CHANNELS; ch++)
        {
            char desc[160];
            sprintf_s(desc, "CAEA %s COM%d LIN%d", matched[d].version, matched[d].com, ch);
            found[foundCount].m_dwIdInterface = (unsigned long)ch;
            found[foundCount].m_dwVendor = (unsigned long)matched[d].com;
            found[foundCount].m_bytNetworkID = (unsigned char)ch;
            found[foundCount].m_acNameInterface = title;
            found[foundCount].m_acDeviceName = matched[d].version;
            found[foundCount].m_acDescription = desc;
            foundCount++;
        }
    }
    if (foundCount <= 0)
    {
        return NO_HW_INTERFACE;
    }
    int selected[CHANNEL_ALLOWED];
    int masterOf[CHANNEL_ALLOWED];
    for (int i = 0; i < CHANNEL_ALLOWED; i++)
    {
        selected[i] = -1;
        masterOf[i] = 1;
    }
    int restored = 0;
    for (UINT i = 0; i < sg_nNoOfChannels && restored < CHANNEL_ALLOWED; i++)
    {
        for (int j = 0; j < foundCount; j++)
        {
            if ((int)found[j].m_dwVendor == sg_channels[i].com
                && (int)found[j].m_bytNetworkID == sg_channels[i].wire)
            {
                selected[restored++] = j;
                masterOf[j] = sg_channels[i].master ? 1 : 0;
                break;
            }
        }
    }
    int shown = foundCount;
    if (ListHardwareInterfaces(sg_hOwnerWnd, found, selected, masterOf, shown) != 0)
    {
        return HW_INTERFACE_NO_SEL;
    }
    sg_nNoOfChannels = (UINT)shown;
    for (int i = 0; i < shown; i++)
    {
        int src = selected[i];
        if (src < 0 || src >= foundCount)
        {
            continue;
        }
        sg_channels[i].com = (int)found[src].m_dwVendor;
        sg_channels[i].wire = (int)found[src].m_bytNetworkID;
        sg_channels[i].session = nullptr;
        sg_channels[i].baud = DEFAULT_LIN_BAUD;
        sg_channels[i].master = masterOf[src] ? 1 : 0;
        strncpy_s(sg_channels[i].name, found[src].m_acDescription.c_str(), _TRUNCATE);
    }
    nCount = shown;
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_SelectHwInterface(const INTERFACE_HW_LIST&, INT nCount)
{
    if (nCount == 0)
    {
        return ERR_LOAD_DRIVER;
    }
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_DeselectHwInterface(void)
{
    if (sg_bIsConnected)
    {
        StopHardware();
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::SetConfigData(IBMNetWorkGetService* ouConfig)
{
    if (ouConfig != nullptr)
    {
        int channels = 0;
        ouConfig->GetChannelCount(LIN, channels);
        for (int i = 0; i < channels && i < defNO_OF_CHANNELS; i++)
        {
            ChannelSettings settings;
            ouConfig->GetChannelSettings(LIN, i, &settings);
            if (settings.m_ouLINSettings.m_nBaudRate > 0)
            {
                sg_channels[i].baud = (unsigned int)settings.m_ouLINSettings.m_nBaudRate;
            }
        }
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::StartHardware(void)
{
    if (pOpen == nullptr || sg_nNoOfChannels == 0)
    {
        return ERR_LOAD_HW_INTERFACE;
    }
    StopHardware();
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        void* session = nullptr;
        for (UINT j = 0; j < i; j++)
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
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        if (pSetLin != nullptr)
        {
            pSetLin(sg_channels[i].session, sg_channels[i].wire, sg_channels[i].baud, sg_channels[i].master ? 1 : 0);
        }
    }
    sg_bRxRun = 1;
    sg_bIsConnected = TRUE;
    sg_hRxThread = CreateThread(nullptr, 0, RxThread, this, 0, nullptr);
    if (sg_hRxThread == nullptr)
    {
        sg_bRxRun = 0;
        sg_bIsConnected = FALSE;
        CloseSessions();
        return ERR_LOAD_HW_INTERFACE;
    }
    sg_bCurrState = STATE_CONNECTED;
    m_Ctrl_Status = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::PreStartHardware(void)
{
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::StopHardware(void)
{
    sg_bRxRun = 0;
    if (sg_hRxThread != nullptr)
    {
        WaitForSingleObject(sg_hRxThread, 1000);
        CloseHandle(sg_hRxThread);
        sg_hRxThread = nullptr;
    }
    CloseSessions();
    sg_bIsConnected = FALSE;
    if (sg_bCurrState == STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    m_Ctrl_Status = INITIALISED;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_ResetHardware(void)
{
    m_Ctrl_Status = INITIALISED;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = m_Ctrl_Status;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetTxMsgBuffer(BYTE*&)
{
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_Send(STLIN_MSG& sMessage)
{
    if (sg_bCurrState != STATE_CONNECTED)
    {
        return ERR_IMPROPER_STATE;
    }
    if (sMessage.m_ucChannel == 0 || sMessage.m_ucChannel > sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }
    int channel = sMessage.m_ucChannel - 1;
    unsigned char id = (unsigned char)(sMessage.m_ucMsgID & 0x3F);
    if (sMessage.m_ucMsgTyp == LIN_SLAVE_RESPONSE)
    {
        if (channel < CHANNEL_ALLOWED && id < 64)
        {
            unsigned char len = sMessage.m_ucDataLen > 8 ? 8 : sMessage.m_ucDataLen;
            sg_aucSlaveLen[channel][id] = len;
            memcpy(sg_aucSlaveData[channel][id], sMessage.m_ucData, len);
            sg_aucSlaveValid[channel][id] = 1;
        }
        return S_OK;
    }
    if (pTxLin == nullptr || sg_channels[channel].session == nullptr)
    {
        return S_FALSE;
    }
    EnterCriticalSection(&sg_cs);
    int ok = pTxLin(sg_channels[channel].session, sg_channels[channel].wire, id, sMessage.m_ucData, sMessage.m_ucDataLen);
    LeaveCriticalSection(&sg_cs);
    return ok ? S_OK : S_FALSE;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_SetSlaveRespData(const STLIN_MSG stRespMsg)
{
    STLIN_MSG msg = stRespMsg;
    msg.m_ucMsgTyp = LIN_SLAVE_RESPONSE;
    return LIN_Send(msg);
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_ResetSlaveRespData(void)
{
    vClearSlaveTable();
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_DisableSlave(STLIN_MSG& sMessage)
{
    int channel = sMessage.m_ucChannel;
    if (channel > 0)
    {
        channel--;
    }
    unsigned char id = (unsigned char)(sMessage.m_ucMsgID & 0x3F);
    if (channel >= 0 && channel < CHANNEL_ALLOWED && id < 64)
    {
        sg_aucSlaveValid[channel][id] = 0;
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetBusConfigInfo(BYTE*)
{
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_acErrStr;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = (LONG)sg_nNoOfChannels;
            break;
        case DRIVER_STATUS:
            lParam = sg_bIsConnected ? 1 : 0;
            break;
        case HW_MODE:
            lParam = (nChannel < sg_nNoOfChannels) ? defMODE_ACTIVE : (defCONTROLLER_BUSOFF + 1);
            break;
        case CON_TEST:
            break;
        default:
            return S_FALSE;
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetConfiguration(SCONTROLLER_DETAILS_LIN config[], INT& nSize)
{
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        config[i].m_strHwUri = sg_channels[i].name;
        config[i].m_BaudRate = sg_channels[i].baud;
    }
    nSize = (INT)sg_nNoOfChannels;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_SetControllerParams(int, ECONTR_PARAM)
{
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT, ECONTR_PARAM)
{
    sErrorCnt.m_ucTxErrCount = 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_GetCntrlStatus(const HANDLE&, UINT&)
{
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_LoadDriverLibrary(void)
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
    pSetLin = (VC02_SET_LIN)GetProcAddress(sg_hProto, "vc02_set_lin");
    pTxLin = (VC02_TX_LIN)GetProcAddress(sg_hProto, "vc02_tx_lin");
    pRead = (VC02_READ)GetProcAddress(sg_hProto, "vc02_read");
    pLastError = (VC02_LAST_ERROR)GetProcAddress(sg_hProto, "vc02_last_error");
    if (pFind == nullptr || pOpen == nullptr || pClose == nullptr || pSetLin == nullptr || pTxLin == nullptr || pRead == nullptr)
    {
        FreeLibrary(sg_hProto);
        sg_hProto = nullptr;
        return ERR_LOAD_DRIVER;
    }
    return S_OK;
}

HRESULT CDIL_LIN_CAEA_VC02::LIN_UnloadDriverLibrary(void)
{
    StopHardware();
    if (sg_hProto != nullptr)
    {
        FreeLibrary(sg_hProto);
        sg_hProto = nullptr;
    }
    pFind = nullptr;
    pOpen = nullptr;
    pClose = nullptr;
    pSetLin = nullptr;
    pTxLin = nullptr;
    pRead = nullptr;
    pLastError = nullptr;
    return S_OK;
}
