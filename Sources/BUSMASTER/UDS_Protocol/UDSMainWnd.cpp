/**
 * \file      UDSMainWnd.cpp
 * \brief     Definition file for CUDSMainWnd class
 * \author    Sanchez Marin Maria Alejandra
 * Designer:    Bentea Radu Mihai

 *  Manager of the Main Window of the UDS tool
 */

#include "stdafx.h"
#include "UDSMainWnd.h"
#include "UDSSettingsWnd.h"
#include "UDS_Resource.h"
#include "UDS_Extern.h"
#include "BaseDIL_CAN.h"
#include "ProtocolsDefinitions.h"
#include "UDSIso14229.h"
#include "IBusMasterKernel.h"
#include <afxdlgs.h>
#include <process.h>
CUDSMainWnd* CUDSMainWnd::m_spodInstance = NULL;
extern CUDSMainWnd* omMainWnd;
extern int P2_Time;
extern "C" HRESULT EvaluateMessage(STCAN_MSG Mensaje);

BOOL FWait_SendingFrame=FALSE;
/** This variable is used to indicate that the system should wait for a flow control */
bool FWaitFlow = FALSE;
bool FlagTP = FALSE;
int Counter_BSize;
BOOL g_bStopSelectedMsgTx = TRUE;
static CBaseDIL_CAN* g_pouDIL_CAN_Interface;
static DWORD g_dwClientID = 0;
int c_unPreviousTime = -1;
double c_dDiffTime = 0;
int S3_Client = 2000;
int S3_Server = 5000;
long Font_Color = RGB(69, 96,200) ;

/* Consecutive frames of a long request are sent by a worker thread so the UI
   thread never spins on STmin. The read thread starts one worker per Flow
   Control (CTS); the UI only gets WM_UDS_CLI_TX_DONE at the end. */
#define CLI_TX_RETRY_MS     1000
/* STmin=0: CF submit period = worst-case (max stuffing) bus time of one CF
   at the channel bit rate, plus CLI_TX_MARGIN_PCT, so the adapter queue does
   not fill up. 500 kbit/s classic 11-bit 8-byte: 270 us -> 351 us.
   10% (297 us) lost a CF block on VC02C -> PCAN at block 26 of 1 MB. */
#define CLI_TX_MARGIN_PCT   30
#define CLI_TX_MIN_US       50
#define CLI_TX_DEF_BPS      500000u
#define CLI_TX_MAX_CH       32
#define UDS_LOG_LIMIT       (1024u * 1024u)
#define UDS_LOG_KEEP        (256 * 1024)
#define UDS_LOG_DATA_CHARS  64
#define UDS_WAIT_RSP_TEXT   "     Waiting for response..."
#define UDS_NO_RSP_TEXT     "     No Response Received"

extern BOOL g_udsCanFd;

struct SCliTx
{
    CRITICAL_SECTION cs;
    BOOL bInit;
    BYTE* pData;
    int nLen;
    int nOff;
    int nSn;
    STCAN_MSG sTmpl;
    int nPci;
    int nDlc;
    int nCfBytes;
    BYTE ucLastSt;
    HWND hNotify;
    volatile LONG lGen;
    volatile LONG lBusy;
    volatile LONG lRunning;
};
static SCliTx g_sCliTx;

struct SCliTxJob
{
    LONG lGen;
    int nBs;
    int nStUs;
};

static void CliTxInit(void)
{
    if (!g_sCliTx.bInit)
    {
        InitializeCriticalSection(&g_sCliTx.cs);
        g_sCliTx.bInit = TRUE;
    }
}

static volatile UINT g_aunCliNomBps[CLI_TX_MAX_CH];
static volatile UINT g_aunCliDataBps[CLI_TX_MAX_CH];
static volatile LONG g_alCliFd[CLI_TX_MAX_CH];

void UdsClientSetChannelBitrate(int channel, UINT nominalBps, UINT dataBps, BOOL canFd)
{
    if (channel < 1 || channel > CLI_TX_MAX_CH)
    {
        return;
    }
    /* Some drivers keep the rate in kbit/s. */
    if (nominalBps > 0 && nominalBps < 10000)
    {
        nominalBps *= 1000;
    }
    if (dataBps > 0 && dataBps < 10000)
    {
        dataBps *= 1000;
    }
    g_aunCliNomBps[channel - 1] = nominalBps;
    g_aunCliDataBps[channel - 1] = dataBps;
    g_alCliFd[channel - 1] = canFd ? 1 : 0;
}

/* Worst-case bus time of one frame with nData payload bytes. Bit counts
   include the maximum number of dynamic stuff bits and the 3-bit IFS. */
static int CliTxFrameUs(const STCAN_MSG& sTmpl, int nData)
{
    int ch = (int)sTmpl.m_ucChannel;
    UINT nom = 0;
    UINT data = 0;
    BOOL bFdCh = FALSE;
    if (ch >= 1 && ch <= CLI_TX_MAX_CH)
    {
        nom = g_aunCliNomBps[ch - 1];
        data = g_aunCliDataBps[ch - 1];
        bFdCh = g_alCliFd[ch - 1] != 0;
    }
    if (nom == 0)
    {
        nom = CLI_TX_DEF_BPS;
    }
    BOOL bExt = sTmpl.m_ucEXTENDED ? TRUE : FALSE;
    double us;
    if (!sTmpl.m_bCANFD)
    {
        int nStuffed = (bExt ? 54 : 34) + 8 * nData;
        int nBits = (bExt ? 67 : 47) + 8 * nData + (nStuffed - 1) / 4;
        us = (double)nBits * 1000000.0 / (double)nom;
    }
    else
    {
        /* Arbitration and ACK/EOF/IFS at the nominal rate, the rest at the
           data rate (BRS). No data rate configured -> nominal rate. */
        if (!bFdCh || data == 0)
        {
            data = nom;
        }
        int nArb = bExt ? 36 : 17;
        int nNomBits = nArb + (nArb - 1) / 4 + 13;
        int nCrc = (nData <= 16) ? 17 : 21;
        int nDataBits = 5 + 8 * nData + (5 + 8 * nData) / 4 + 4 + nCrc + (4 + nCrc + 3) / 4;
        us = (double)nNomBits * 1000000.0 / (double)nom + (double)nDataBits * 1000000.0 / (double)data;
    }
    int nUs = (int)(us * (100 + CLI_TX_MARGIN_PCT) / 100.0 + 0.999);
    return nUs < CLI_TX_MIN_US ? CLI_TX_MIN_US : nUs;
}

static int CliTxDecodeStUs(BYTE s)
{
    if (s <= 0x7F)
    {
        return (int)s * 1000;
    }
    if (s >= 0xF1 && s <= 0xF9)
    {
        return (int)(s - 0xF0) * 100;
    }
    return 127000;
}

BOOL UdsClientTxBusy()
{
    return g_sCliTx.lBusy != 0;
}

void UdsClientTxCancel()
{
    CliTxInit();
    EnterCriticalSection(&g_sCliTx.cs);
    InterlockedIncrement(&g_sCliTx.lGen);
    InterlockedExchange(&g_sCliTx.lBusy, 0);
    delete[] g_sCliTx.pData;
    g_sCliTx.pData = NULL;
    g_sCliTx.nLen = 0;
    g_sCliTx.nOff = 0;
    LeaveCriticalSection(&g_sCliTx.cs);
}

static void CliTxArm(const CString& omHexRest, const STCAN_MSG& sFf, int nPci, int nDlc, int nCfBytes, HWND hNotify)
{
    CliTxInit();
    int nLen = omHexRest.GetLength() / 2;
    BYTE* pData = new BYTE[nLen > 0 ? nLen : 1];
    for (int i = 0; i < nLen; i++)
    {
        char ach[3] = { (char)omHexRest[i * 2], (char)omHexRest[i * 2 + 1], 0 };
        pData[i] = (BYTE)strtoul(ach, NULL, 16);
    }
    EnterCriticalSection(&g_sCliTx.cs);
    InterlockedIncrement(&g_sCliTx.lGen);
    delete[] g_sCliTx.pData;
    g_sCliTx.pData = pData;
    g_sCliTx.nLen = nLen;
    g_sCliTx.nOff = 0;
    g_sCliTx.nSn = 1;
    g_sCliTx.sTmpl = sFf;
    g_sCliTx.nPci = nPci;
    g_sCliTx.nDlc = nDlc;
    g_sCliTx.nCfBytes = nCfBytes;
    g_sCliTx.hNotify = hNotify;
    InterlockedExchange(&g_sCliTx.lBusy, nLen > 0 ? 1 : 0);
    LeaveCriticalSection(&g_sCliTx.cs);
}

static unsigned __stdcall CliTxThread(void* pParam)
{
    SCliTxJob* pJob = (SCliTxJob*)pParam;
    LARGE_INTEGER qpf, qLast, qNow;
    QueryPerformanceFrequency(&qpf);
    qLast.QuadPart = 0;
    int nStUs = pJob->nStUs;
    if (nStUs <= 0)
    {
        EnterCriticalSection(&g_sCliTx.cs);
        STCAN_MSG sTmpl = g_sCliTx.sTmpl;
        sTmpl.m_bCANFD = g_udsCanFd ? true : false;
        int nFrameBytes = g_sCliTx.nDlc;
        LeaveCriticalSection(&g_sCliTx.cs);
        nStUs = CliTxFrameUs(sTmpl, nFrameBytes);
    }
    LONGLONG llStTicks = (LONGLONG)nStUs * qpf.QuadPart / 1000000;
    /* STmin=0: fixed submit period (DILC_SendMsg time is inside it). A late
       frame restarts the grid instead of bursting to catch up. A real STmin
       from the server is still measured from the end of the previous submit. */
    BOOL bGrid = (pJob->nStUs <= 0);
    LONGLONG llDue = 0;
    int nSent = 0;
    BOOL bDone = FALSE;
    BOOL bFailed = FALSE;
    for (;;)
    {
        if (nSent > 0)
        {
            for (;;)
            {
                QueryPerformanceCounter(&qNow);
                LONGLONG llLeft = (bGrid ? llDue : qLast.QuadPart + llStTicks) - qNow.QuadPart;
                if (llLeft <= 0)
                {
                    break;
                }
                if (llLeft * 1000 / qpf.QuadPart >= 3)
                {
                    Sleep(1);
                }
                else
                {
                    YieldProcessor();
                }
            }
        }
        STCAN_MSG sMsg;
        BOOL bLast = FALSE;
        BOOL bBlockEnd = FALSE;
        EnterCriticalSection(&g_sCliTx.cs);
        if (g_sCliTx.lGen != pJob->lGen || g_sCliTx.pData == NULL)
        {
            LeaveCriticalSection(&g_sCliTx.cs);
            break;
        }
        int n = g_sCliTx.nLen - g_sCliTx.nOff;
        if (n <= 0)
        {
            LeaveCriticalSection(&g_sCliTx.cs);
            bDone = TRUE;
            break;
        }
        if (n > g_sCliTx.nCfBytes)
        {
            n = g_sCliTx.nCfBytes;
        }
        int nPci = g_sCliTx.nPci;
        int nDlc = g_sCliTx.nDlc;
        sMsg = g_sCliTx.sTmpl;
        memset(sMsg.m_ucData + nPci, 0, sizeof(sMsg.m_ucData) - nPci);
        sMsg.m_ucData[nPci] = (BYTE)(0x20 | (g_sCliTx.nSn & 0x0F));
        memcpy(sMsg.m_ucData + nPci + 1, g_sCliTx.pData + g_sCliTx.nOff, n);
        sMsg.m_ucDataLen = (unsigned char)nDlc;
        sMsg.m_bCANFD = g_udsCanFd ? true : false;
        g_sCliTx.nOff += n;
        g_sCliTx.nSn = (g_sCliTx.nSn + 1) & 0x0F;
        bLast = (g_sCliTx.nOff >= g_sCliTx.nLen);
        bBlockEnd = (!bLast && pJob->nBs > 0 && nSent + 1 >= pJob->nBs);
        if (bBlockEnd)
        {
            /* Armed before the frame goes out so an immediate FC is not missed. */
            FWaitFlow = TRUE;
        }
        LeaveCriticalSection(&g_sCliTx.cs);

        if (bGrid)
        {
            QueryPerformanceCounter(&qNow);
            if (llDue == 0 || qNow.QuadPart - llDue > llStTicks)
            {
                llDue = qNow.QuadPart;
            }
            llDue += llStTicks;
        }
        HRESULT hr = g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, sMsg);
        if (hr != S_OK)
        {
            DWORD t0 = GetTickCount();
            int nTries = 0;
            while (hr != S_OK && g_sCliTx.lGen == pJob->lGen && (GetTickCount() - t0) < CLI_TX_RETRY_MS)
            {
                Sleep(nTries < 16 ? 0 : 1);
                nTries++;
                hr = g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, sMsg);
            }
            if (hr != S_OK)
            {
                bFailed = TRUE;
                break;
            }
        }
        QueryPerformanceCounter(&qLast);
        nSent++;
        if (bLast)
        {
            bDone = TRUE;
            break;
        }
        if (bBlockEnd)
        {
            break;
        }
    }
    if ((bDone || bFailed) && g_sCliTx.lGen == pJob->lGen)
    {
        InterlockedExchange(&g_sCliTx.lBusy, 0);
        if (g_sCliTx.hNotify != NULL)
        {
            ::PostMessage(g_sCliTx.hNotify, WM_UDS_CLI_TX_DONE, bFailed ? 1 : 0, 0);
        }
    }
    InterlockedExchange(&g_sCliTx.lRunning, 0);
    delete pJob;
    return 0;
}

/* Called on the CAN read thread when a CTS Flow Control arrives. */
void UdsClientTxOnFc(int nBs, BYTE ucSt)
{
    CliTxInit();
    g_sCliTx.ucLastSt = ucSt;
    if (g_sCliTx.lBusy == 0)
    {
        return;
    }
    if (InterlockedCompareExchange(&g_sCliTx.lRunning, 1, 0) != 0)
    {
        return;
    }
    SCliTxJob* pJob = new SCliTxJob;
    pJob->lGen = g_sCliTx.lGen;
    pJob->nBs = nBs;
    pJob->nStUs = CliTxDecodeStUs(ucSt);
    uintptr_t h = _beginthreadex(NULL, 0, CliTxThread, pJob, 0, NULL);
    if (h == 0)
    {
        InterlockedExchange(&g_sCliTx.lRunning, 0);
        delete pJob;
        return;
    }
    CloseHandle((HANDLE)h);
}


// CUDSMainWnd dialog

IMPLEMENT_DYNAMIC(CUDSMainWnd, CDialog)

CUDSMainWnd::CUDSMainWnd(UINT SA, UINT TA , UDS_INTERFACE FInterface, UINT CanId,  CWnd* pParent /*=NULL*/)
    : CDialog(CUDSMainWnd::IDD, pParent)
{
    SourceAddress = SA;
    TargetAddress = TA;
    fInterface = FInterface;
    CanID  = CanId;
    P2_Time=250;
    BOOL FWaitFlow = FALSE;
    psTxCanMsgUds = NULL;
    m_stringEditDLC = 0;
    int ConsecutiveFrame = 0x21;
    m_nCyclicTimer = 0;
    m_unCyclicPeriodMs = 1000;
    m_bSendingSecurityKey = FALSE;
    m_bSyncingSecurityUi = FALSE;
}

CUDSMainWnd::~CUDSMainWnd()
{
    StopCyclicTimer();
}

void CUDSMainWnd::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_SA, m_omSourceAddress);
    DDX_Control(pDX, IDC_EDIT_TA, m_omTargetAddress);
    DDX_Control(pDX, IDC_COMBO_CHANNEL, m_omComboChannelUDS);
    DDX_Control(pDX, IDC_EDIT_CANID, m_omCanID);
    DDX_Control(pDX, IDC_EDIT_DLCC, m_omEditDLC);
    DDX_Text(pDX, IDC_EDIT_DLCC, m_stringEditDLC );
    DDX_Control(pDX, IDC_EDIT_DATA, m_omEditMsgData);
    DDX_Text(pDX, IDC_EDIT_DATA, m_omMsgDataEdit);
    DDX_Text (pDX, IDC_RESPONSE_DATA, m_abDatas);
    DDX_Control(pDX, IDC_CHECK_TP, m_omCheckTP);
    DDX_Text(pDX, IDC_DIAG_SERVICE, m_omDiagService);
    DDX_Control(pDX, IDC_RESPONSE_DATA, m_omDataResponse);
    DDX_Control(pDX, IDC_NUMBER_OF_BYTES, m_omBytes);
    DDX_Control(pDX, IDC_SEND_UDS, m_omSendButton);
    DDX_Control(pDX, IDC_TREE_SERVICES, m_omTreeServices);
    DDX_Control(pDX, IDC_LIST_ATTRIBUTE, m_omListAttribute);
    DDX_Control(pDX, IDC_CHECK_CYCLIC, m_omCheckCyclic);
    DDX_Control(pDX, IDC_CHECK_FUNCTIONAL, m_omCheckFunctional);
    DDX_Control(pDX, IDC_COMBO_SEC_TARGET, m_omComboSecTarget);
    DDX_Control(pDX, IDC_EDIT_CYCLIC_PERIOD, m_omEditCyclicPeriod);
    DDX_Control(pDX, IDC_EDIT_LOG, m_omEditLog);
    DDX_Control(pDX, IDC_EDIT_HEX_PATH, m_omEditHexPath);
    DDX_Control(pDX, IDC_EDIT_SEC_CURRENT, m_omEditSecCurrent);
    DDX_Control(pDX, IDC_EDIT_SEEDKEY_DLL, m_omEditSeedKeyDll);
    DDX_Control(pDX, IDC_EDIT_SEEDKEY_VARIANT, m_omEditSeedKeyVariant);
}

BEGIN_MESSAGE_MAP(CUDSMainWnd, CDialog)
    ON_BN_CLICKED ( IDC_SEND_UDS, OnBnClickedSendUD )
    ON_BN_CLICKED ( IDC_CHECK_TP, OnBnClickedTesterPresent )
    ON_BN_CLICKED ( IDC_CHECK_CYCLIC, OnBnClickedCyclic )
    ON_BN_CLICKED ( IDC_CHECK_FUNCTIONAL, OnBnClickedFunctional )
    ON_BN_CLICKED ( IDC_BTN_ADD, OnBnClickedAdd )
    ON_BN_CLICKED ( IDC_BTN_DELETE, OnBnClickedDelete )
    ON_BN_CLICKED ( IDC_BTN_IMPORT, OnBnClickedImport )
    ON_BN_CLICKED ( IDC_BTN_EXPORT, OnBnClickedExport )
    ON_BN_CLICKED ( IDC_BTN_DATA_BROWSE, OnBnClickedDataBrowse )
    ON_BN_CLICKED ( IDC_BTN_UNLOCK, OnBnClickedUnlock )
    ON_BN_CLICKED ( IDC_BTN_SEEDKEY_BROWSE, OnBnClickedSeedKeyBrowse )
    ON_BN_CLICKED ( IDC_BTN_HEX_BROWSE, OnBnClickedHexBrowse )
    ON_BN_CLICKED ( IDC_BTN_HEX_DOWNLOAD, OnBnClickedHexDownload )
    ON_BN_CLICKED ( IDC_BTN_LOG_SAVE, OnBnClickedLogSave )
    ON_BN_CLICKED ( IDC_BTN_LOG_CLEAR, OnBnClickedLogClear )
    ON_BN_CLICKED ( IDC_CHECK_UDS_CANFD, OnCanFdSwitch )
    ON_NOTIFY(TVN_SELCHANGED, IDC_TREE_SERVICES, OnTvnSelchangedServices)
    ON_CBN_SELCHANGE(IDC_COMBO_SEC_TARGET, OnCbnSelchangeSecTarget)
    ON_EN_UPDATE ( IDC_EDIT_DATA, OnEnChangeData )
    ON_EN_UPDATE ( IDC_EDIT_SA, OnEnChangeSA )
    ON_EN_UPDATE ( IDC_EDIT_TA, OnEnChangeTA )
    ON_EN_UPDATE ( IDC_EDIT_CANID, OnEnChangeCanID )
    ON_WM_TIMER ()
    ON_WM_CTLCOLOR ()
    ON_WM_KEYDOWN ()
    ON_WM_CHAR ()
    ON_MESSAGE (WM_COMMANDHELP, CUDSMainWnd::OnCommandHelp )
    ON_MESSAGE (WM_UDS_EVALUATE_MSG, OnEvaluateCanMsg )
    ON_MESSAGE (WM_UDS_CONTINUE_CF, OnContinueCf )
    ON_MESSAGE (WM_UDS_CLI_TX_DONE, OnClientTxDone )
    ON_MESSAGE (WM_UDS_LONG_RX_UI, OnLongRxUi )
    ON_MESSAGE (WM_UDS_SEND_FC, OnSendFlowControl )
    ON_WM_DESTROY()

END_MESSAGE_MAP()


// CUDSMainWnd message handlers

//const int NO_OF_CHAR_IN_BYTE = 2;

/**********************************************************************************************************
 Function Name  :   StartTimer
  Input(s)      :   -
  Output        :   -
 Description    :   This function is called to start the timer used to send the Tester Present
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

UINT CUDSMainWnd::StartTimer(/*LPVOID alo*/)
{
    if (m_omCheckTP.GetCheck())
    {
        m_nTimer = SetTimer(ID_TIMER_TP, S3_Client, NULL);
    }
    return 0;
}

/**********************************************************************************************************
 Function Name  :   SendFirstFrame
 Input(s)       :   omByteStr contains the bytes that has to be sent in the first frame of the long request
                    abByteArr[] is the array where the data should be put to be sent
                    psTxCanMsgUds is the general structure of the message
                    FInterface indicates the interface selected in the SettingsWnd
 Output         :   -
 Description    :   This function sends the first Message of a long request
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

extern BOOL g_udsCanFd;
extern BOOL g_udsCanFdHw;
extern int g_udsFdDlc;

static int UdsTxDlc()
{
    if (!g_udsCanFd)
    {
        return 8;
    }
    if (g_udsFdDlc < 8)
    {
        return 8;
    }
    if (g_udsFdDlc > 64)
    {
        return 64;
    }
    return g_udsFdDlc;
}

/* Hands the bytes after the First Frame to the CF worker. The FF message is
   the template (ID, channel, extended-address byte). */
static void ArmConsecutiveFrames(const CString& omHexRest, const STCAN_MSG& sFf, HWND hNotify)
{
    int nDlc = g_udsCanFd ? UdsTxDlc() : 8;
    int nPci = initialByte;
    if (nPci < 0)
    {
        nPci = 0;
    }
    int nCfBytes = nDlc - 1 - aux_Finterface;
    if (nPci + 1 + nCfBytes > nDlc)
    {
        nCfBytes = nDlc - nPci - 1;
    }
    if (nCfBytes < 1)
    {
        nCfBytes = 1;
    }
    CliTxArm(omHexRest, sFf, nPci, nDlc, nCfBytes, hNotify);
}

int CUDSMainWnd::SendFirstFrame(CString omByteStr, unsigned char abByteArr[], mPSTXSELMSGDATA psTxCanMsgUds, UDS_INTERFACE FInterface)
{
    if (g_udsCanFd)
    {
        int dlc = UdsTxDlc();
        int aux = aux_Finterface;
        int ffData;
        int pci;
        memset(abByteArr, 0, 64);
        if (aux > 0)
        {
            abByteArr[0] = (BYTE)TargetAddress;
        }
        if (TotalLength > 0xFFF)
        {
            abByteArr[aux] = 0x10;
            abByteArr[aux + 1] = 0x00;
            abByteArr[aux + 2] = (BYTE)((TotalLength >> 24) & 0xFF);
            abByteArr[aux + 3] = (BYTE)((TotalLength >> 16) & 0xFF);
            abByteArr[aux + 4] = (BYTE)((TotalLength >> 8) & 0xFF);
            abByteArr[aux + 5] = (BYTE)(TotalLength & 0xFF);
            pci = 6;
            ffData = dlc - 6 - aux;
        }
        else
        {
            abByteArr[aux] = (BYTE)(0x10 | ((TotalLength >> 8) & 0x0F));
            abByteArr[aux + 1] = (BYTE)(TotalLength & 0xFF);
            pci = 2;
            ffData = dlc - 2 - aux;
        }
        if (ffData < 1)
        {
            ffData = 1;
        }
        if (ffData > (int)TotalLength)
        {
            ffData = (int)TotalLength;
        }
        for (int n = 0; n < ffData; n++)
        {
            CString one = omByteStr.Mid(n * 2, 2);
            abByteArr[aux + pci + n] = (BYTE)_tcstol(one, NULL, 16);
        }
        psTxCanMsgUds->m_psTxMsg->m_ucDataLen = (unsigned char)dlc;
        psTxCanMsgUds->m_psTxMsg->m_bCANFD = true;
        int takenChars = ffData * 2;
        omByteStr = omByteStr.Right(((UINT)TotalLength * 2) - takenChars);
        TotalLength = (((UINT)TotalLength * 2) - takenChars) / 2;
        ConsecutiveFrame = 0x21;
        /* Armed before the FF goes out; the FC can arrive on the read thread at once. */
        ArmConsecutiveFrames(omByteStr, psTxCanMsgUds->m_psTxMsg[0], m_hWnd);
        FWaitFlow = TRUE;
        SendSimpleDiagnosticMessage();
        return 0;
    }
    int pci = initialByte;
    if (pci < 0)
    {
        pci = 0;
    }
    int ffData = 6 - aux_Finterface;
    if (ffData < 1)
    {
        ffData = 1;
    }
    if (ffData > (int)TotalLength)
    {
        ffData = (int)TotalLength;
    }
    if (pci + 2 + ffData > 8)
    {
        ffData = 8 - pci - 2;
    }
    if (ffData < 1)
    {
        ffData = 1;
    }
    for (int k = pci; k < 8; k++)
    {
        abByteArr[k] = 0;
    }
    abByteArr[pci] = (BYTE)(0x10 | ((TotalLength >> 8) & 0x0F));
    abByteArr[pci + 1] = (BYTE)(TotalLength & 0xFF);
    for (int n = 0; n < ffData; n++)
    {
        CString one = omByteStr.Mid(n * 2, 2);
        abByteArr[pci + 2 + n] = (BYTE)_tcstol(one, NULL, 16);
    }
    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
    psTxCanMsgUds->m_psTxMsg->m_bCANFD = false;

    int takenChars = ffData * 2;
    if (takenChars > omByteStr.GetLength())
    {
        takenChars = omByteStr.GetLength();
    }
    CString omRest = omByteStr.Mid(takenChars);
    TotalLength = (UINT)(omRest.GetLength() / 2);
    ConsecutiveFrame = 0x21;
    ArmConsecutiveFrames(omRest, psTxCanMsgUds->m_psTxMsg[0], m_hWnd);
    FWaitFlow = TRUE;
    SendSimpleDiagnosticMessage();
    return 0;
}

/**********************************************************************************************************
 Function Name  :   SendContinuosFrames
  Input(s)      :   abByteArr[] is the array where the data should be put to be sent
                    psTxCanMsgUds is the general structure of the message
                    FInterface indicates the interface selected in the SettingsWnd
 Output         :   -
 Description    :   This function sends the continuos frames of a long request. It sends several messages
                    until the BSize parameter has been reached.
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::SendContinuosFrames( unsigned char /*abByteArr*/[], mPSTXSELMSGDATA /*psTxCanMsgUds*/, UDS_INTERFACE /*FInterface*/)
{
    UdsClientTxOnFc(BSizE, g_sCliTx.ucLastSt);
}

LRESULT CUDSMainWnd::OnContinueCf(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (FWaitFlow || !UdsClientTxBusy())
    {
        return 0;
    }
    SendContinuosFrames(NULL, NULL, fInterface);
    return 0;
}

LRESULT CUDSMainWnd::OnClientTxDone(WPARAM wParam, LPARAM /*lParam*/)
{
    if (wParam != 0)
    {
        SetDlgItemText(IDC_RESPONSE_DATA, "\r\nISO-TP transmit aborted: CAN send kept failing");
    }
    else if (m_omDiagService == UDS_WAIT_RSP_TEXT)
    {
        SetTimer(ID_TIMER_SEND_BUTTON, P2_Time, NULL);
    }
    m_omSendButton.EnableWindow(TRUE);
    return 0;
}

/**********************************************************************************************************
 Function Name  :   SendSimpleDiagnosticMessage
 Input(s)       :   -
 Output         :   -
 Description    :   This function is used to send any simple message
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::SendSimpleDiagnosticMessage(void)
{

    g_bStopSelectedMsgTx = FALSE;
    // Get handle of thread and assign it to pulic data
    // member in app class. This will be used to terminate
    // the thread.
    if( psTxCanMsgUds->m_psTxMsg->m_unMsgID != 0 )
    {
        psTxCanMsgUds->m_psTxMsg->m_bCANFD = g_udsCanFd ? true : false;
        //CWinThread* pomThread = NULL ;
        //pomThread = AfxBeginThread( OnSendSelectedMsg, psTxCanMsgUds );
        //FWait_SendingFrame = TRUE;         // No one can send another message  until this procedure has ended
        int nReturn = g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, psTxCanMsgUds->m_psTxMsg[0]);
    }
    StartTimer();
}
/**********************************************************************************************************
 Function Name  :   SendSimpleDiagnosticMessage
 Input(s)       :   -
 Output         :   -
 Description    :   This function is used to send any simple message
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::SendSimpleDiagnosticMessagePanels(mPSTXSELMSGDATA pstx)
{

    g_bStopSelectedMsgTx = FALSE;
    // Get handle of thread and assign it to pulic data
    // member in app class. This will be used to terminate
    // the thread.

    //if(   psTxCanMsgUds->m_psTxMsg->m_unMsgID != 0 && (TargetAddress || SourceAddress)!=0 ) {
    //CWinThread* pomThread = NULL ;
    //pomThread = AfxBeginThread( OnSendSelectedMsg, psTxCanMsgUds );
    //FWait_SendingFrame = TRUE;         // No one can send another message  until this procedure has ended
    int nReturn = g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, pstx->m_psTxMsg[0]);
    //}
    //StartTimer();
}

/**********************************************************************************************************
 Function Name  :   OnSendSelectedMsg
 Input(s)       :   -
 Output         :   -
 Description    :   This function is called to transmit any simple message
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/
UINT CUDSMainWnd::OnSendSelectedMsg(LPVOID pParam)
{
    // s_omState.ResetEvent();
    mPSTXSELMSGDATA psTxCanMsgUds = static_cast <mPSTXSELMSGDATA> (pParam);
    int nReturn = g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, psTxCanMsgUds->m_psTxMsg[0]);
    if (nReturn != S_OK)
    {
        // Message not sent.
    }
    FWait_SendingFrame = FALSE;  // Now that the msg was sent its structure can be modified
    //g_pouDIL_CAN_Interface->DILC_ManageMsgBuf(MSGBUF_CLEAR, g_dwClientID, &m_ouMCCanBufFSE);
    return 0;
}
/**********************************************************************************************************
 Function Name  :   PrepareFlowControl
 Input(s)       :   -
 Output         :   -
 Description    :   This function is called when it has been received a long response to send the flow control
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::PrepareFlowControl()
{
    /* ISO-TP FC: 30 | BlockSize | STmin.
       Must run on the UI thread. Calling DILC_SendMsg from the PSDI read
       thread (inside m_sCritSecDataSync) stalls VC02C RX — FF is seen
       (6/4095) but CFs never reach EvaluateMessage. */
    if (psTxCanMsgUds == NULL || psTxCanMsgUds->m_psTxMsg == NULL)
    {
        return;
    }
    if (GetWindowThreadProcessId(GetSafeHwnd(), NULL) != GetCurrentThreadId())
    {
        PostMessage(WM_UDS_SEND_FC, 0, 0);
        return;
    }
    int minLen = initialByte + 3;
    if (minLen < 3)
    {
        minLen = 3;
    }
    int dlc = g_udsCanFd ? UdsTxDlc() : SizeFC;
    if (!g_udsCanFd && (dlc < minLen || dlc > 8))
    {
        dlc = (dlc > 8 || dlc <= 0) ? 8 : minLen;
    }
    if (g_udsCanFd)
    {
        memset(psTxCanMsgUds->m_psTxMsg->m_ucData, 0, 64);
    }
    unsigned char bs = (unsigned char)(BSize & 0xFF);
    unsigned char st = (unsigned char)(SSTMin & 0xFF);
    Counter_BSize = (int)bs;
    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = (unsigned char)dlc;
    memset(psTxCanMsgUds->m_psTxMsg->m_ucData, 0, g_udsCanFd ? 64 : 8);

    if (fInterface == INTERFACE_EXTENDED_11)
    {
        psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte - 1] = TargetAddress;
    }
    psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x30;
    psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte + 1] = bs;
    psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte + 2] = st;

    /* Do not StartTimer() here — FC must not restart Tester Present. */
    if (psTxCanMsgUds->m_psTxMsg->m_unMsgID != 0 && g_pouDIL_CAN_Interface != NULL)
    {
        psTxCanMsgUds->m_psTxMsg->m_bCANFD = g_udsCanFd ? true : false;
        g_pouDIL_CAN_Interface->DILC_SendMsg(g_dwClientID, psTxCanMsgUds->m_psTxMsg[0]);
    }
}

/**********************************************************************************************************
 Function Name  :   PrepareDiagnosticMessage
  Input(s)      :   omByteStr contains the bytes that has to be sent in the first frame of the long request
                    abByteArr[] is the array where the data should be put to be sent
                    psTxCanMsgUds is the general structure of the message
                    ByteArrLen is the length of the message to send
 Output         :   -
 Description    :   This function is called by OnBnClickedSendUDS when the user needs to transmit a message
                    It evaluates the message and divide it according to its lenght
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::PrepareDiagnosticMessage(CString omByteStr,mPSTXSELMSGDATA psTxCanMsgUds, unsigned char abByteArr[], UINT ByteArrLen)
{
    memset(abByteArr, 0, ByteArrLen);               // Initialize the array to zero
    omByteStr.Replace(" ","");
    int i_counter =0;
    UINT LengthStr = omByteStr.GetLength();
    UINT Result;
    if ( LengthStr%2 != 0)
    {
        CString LastString = "0" + omByteStr.Right(1);
        omByteStr= omByteStr.Left(omByteStr.GetLength() - 1);
        omByteStr = omByteStr+ LastString;

    }
    Result = LengthStr / 2 + LengthStr % 2;
    TotalLength = Result;
    m_omSendButton.EnableWindow(FALSE);  //I have to disable the sendButton everytime that I press SEND
    // If it's a long request I need it
    // If it's a long response I've to restart it  and receive all the bytes
    // If it's a simple response: the response could take more time or it could be a 0x78 resp

    m_nTimer = SetTimer(ID_TIMER_SEND_BUTTON,P2_Time , NULL);               // Time to wait to enable the send button again

    if (g_udsCanFd)
    {
        int dlc = UdsTxDlc();
        int aux = aux_Finterface;
        BOOL ext29 = (fInterface == INTERFACE_NORMAL_ISO_29 || fInterface == INTERFACE_NORMAL_J1939_29);
        psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED = ext29 ? TRUE : FALSE;
        memset(abByteArr, 0, 64);
        int sfMax = (dlc > 8) ? (dlc - 2 - aux) : (7 - aux);
        if ((int)Result <= sfMax)
        {
            int dataAt;
            if (aux > 0)
            {
                abByteArr[0] = (BYTE)TargetAddress;
            }
            if (dlc <= 8)
            {
                abByteArr[aux] = (BYTE)Result;
                dataAt = aux + 1;
            }
            else
            {
                abByteArr[aux] = 0x00;
                abByteArr[aux + 1] = (BYTE)Result;
                dataAt = aux + 2;
            }
            for (UINT n = 0; n < Result; n++)
            {
                CString one = omByteStr.Mid(n * 2, 2);
                abByteArr[dataAt + (int)n] = (BYTE)_tcstol(one, NULL, 16);
            }
            psTxCanMsgUds->m_psTxMsg->m_ucDataLen = (unsigned char)dlc;
            psTxCanMsgUds->m_psTxMsg->m_bCANFD = true;
            SendSimpleDiagnosticMessage();
            return;
        }
        SendFirstFrame(omByteStr, abByteArr, psTxCanMsgUds, fInterface);
        return;
    }

    switch (fInterface)
    {
        case INTERFACE_NORMAL_11:
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= FALSE;
            ByteArrLen = ByteArrLen-1;
            if (Result>7)                            // Long Request
            {
                int f =SendFirstFrame(omByteStr,abByteArr,psTxCanMsgUds,fInterface );

            }
            else                                     // Short request
            {
                i_counter = (int) Result;
                abByteArr[0] = Result;
                Result++;                            // Result must be increased to make the size array bigger
                CString omTempByte;
                while (omByteStr.GetLength())
                {
                    omTempByte = omByteStr.Right(NO_OF_CHAR_IN_BYTE);
                    abByteArr[i_counter--] = (BYTE)_tcstol(omTempByte, L'\0', 16);
                    omByteStr = omByteStr.Left(omByteStr.GetLength() - NO_OF_CHAR_IN_BYTE);
                }

                if(fMsgSize)
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
                }
                else
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = Result;
                }
                SendSimpleDiagnosticMessage();
            }
        }
        break;
        case INTERFACE_EXTENDED_11:
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= FALSE;
            ByteArrLen = ByteArrLen-2;
            abByteArr[0] = TargetAddress;
            if (Result>6)                                                           // Long Request - The limit of a simple request for extended is 6 because the byte that contains the TA.
            {
                m_omSendButton.EnableWindow(FALSE);
                m_nTimer = SetTimer(ID_TIMER_SEND_BUTTON, P2_Time , NULL);          // Start Timer to wait to enable by default the send button again

                int f =SendFirstFrame(omByteStr,abByteArr,psTxCanMsgUds,fInterface);

            }
            else                                             // Short request
            {
                i_counter = Result+1;
                abByteArr[1] = Result;
                Result = Result+2;
                CString omTempByte;
                while (omByteStr.GetLength())               // Take all the bytes that will be parte of the simple message
                {
                    omTempByte = omByteStr.Right(NO_OF_CHAR_IN_BYTE);
                    abByteArr[i_counter--] = (BYTE)_tcstol(omTempByte, L'\0', 16);
                    omByteStr = omByteStr.Left(omByteStr.GetLength() - NO_OF_CHAR_IN_BYTE);
                }
                if(fMsgSize)
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
                }
                else
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = Result;
                }
                SendSimpleDiagnosticMessage();
            }
        }
        break;
        case INTERFACE_NORMAL_ISO_29:
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= TRUE;
            ByteArrLen = ByteArrLen-1;
            if (Result>7)                            // Long Request
            {
                int f =SendFirstFrame(omByteStr,abByteArr,psTxCanMsgUds,fInterface );

            }
            else                                     // Short request
            {
                i_counter = (int) Result;
                abByteArr[0] = Result;
                Result++;                            // Result must be increased to make the size array bigger
                CString omTempByte;
                while (omByteStr.GetLength())
                {
                    omTempByte = omByteStr.Right(NO_OF_CHAR_IN_BYTE);
                    abByteArr[i_counter--] = (BYTE)_tcstol(omTempByte, L'\0', 16);
                    omByteStr = omByteStr.Left(omByteStr.GetLength() - NO_OF_CHAR_IN_BYTE);
                }
                if(fMsgSize)
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
                }
                else
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = Result;
                }
                SendSimpleDiagnosticMessage();
            }
        }
        break;
        case INTERFACE_NORMAL_J1939_29:
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= TRUE;
            ByteArrLen = ByteArrLen-1;
            if (Result>7)                            // Long Request
            {
                int f =SendFirstFrame(omByteStr,abByteArr,psTxCanMsgUds,fInterface );

            }
            else                                     // Short request
            {
                i_counter = (int) Result;
                abByteArr[0] = Result;
                Result++;                            // Result must be increased to make the size array bigger
                CString omTempByte;
                while (omByteStr.GetLength())
                {
                    omTempByte = omByteStr.Right(NO_OF_CHAR_IN_BYTE);
                    abByteArr[i_counter--] = (BYTE)_tcstol(omTempByte, L'\0', 16);
                    omByteStr = omByteStr.Left(omByteStr.GetLength() - NO_OF_CHAR_IN_BYTE);
                }
                if(fMsgSize)
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
                }
                else
                {
                    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = Result;
                }
                SendSimpleDiagnosticMessage();
            }
        }
        break;
    }
}

/**********************************************************************************************************
 Function Name  :   OnBnClickedSendUD
 Input(s)       :   -
 Output         :   -
 Description    :   This function is called by framework when user wants to Transmit a  message
                    It Filters the message so only the msg with a proper SA,TA and CANId can be sent
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   28.05.2013
**********************************************************************************************************/

void CUDSMainWnd::OnBnClickedSendUD()
{
    KillTimer(ID_TIMER_TP);    //Added to kill the timer everyTime I've pressed the SEND button
    FSending = TRUE;        // This flag is used to know if a message has been sent from the UDSMainWnd
    Bytes_to_Show= ("\r\n   1 -> ");
    BytesShown_Line = 1;
    m_abDatas = " ";
    if (omManagerPtr != NULL)
    {
        omManagerPtr->Data_Recibida.Empty();
    }
    CurrentService = m_omMsgDataEdit.Left(NO_OF_CHAR_IN_BYTE); //Baiasu
    m_omDiagService = initialEval(m_omMsgDataEdit);
    m_omBytes.vSetValue(0);
    //CurrentService = m_omMsgDataEdit.Left(NO_OF_CHAR_IN_BYTE); //moved up by Adrian

    UpdateData(false);
    if (psTxCanMsgUds ==NULL)
    {
        psTxCanMsgUds  = new mSTXSELMSGDATA;
    }

    UpdateData();
    //setValue();
    BYTE byAddress = (BYTE)m_omSourceAddress.lGetValue();
    if(psTxCanMsgUds != NULL )
    {
        psTxCanMsgUds->m_unCount = 1;
        psTxCanMsgUds->m_psTxMsg = new STCAN_MSG[1];
        if(psTxCanMsgUds->m_psTxMsg != NULL )
        {
            psTxCanMsgUds->m_psTxMsg->m_ucRTR = FALSE;
            psTxCanMsgUds->m_psTxMsg->m_ucChannel = (UCHAR)m_omComboChannelUDS.GetCurSel()+1;
            Current_Channel = psTxCanMsgUds->m_psTxMsg->m_ucChannel;
            psTxCanMsgUds->m_psTxMsg->m_unMsgID = (int)m_omCanID.lGetValue();
            if (m_omCheckFunctional.GetCheck() &&
                (fInterface == INTERFACE_NORMAL_11 || fInterface == INTERFACE_EXTENDED_11))
            {
                psTxCanMsgUds->m_psTxMsg->m_unMsgID = 0x7DF;
            }
            if( psTxCanMsgUds->m_psTxMsg->m_unMsgID != 0 && m_omMsgDataEdit.GetLength()!=0 )
            {
                PrepareDiagnosticMessage(m_omMsgDataEdit, psTxCanMsgUds, psTxCanMsgUds->m_psTxMsg->m_ucData, 8);     //This funtion evaluate if the message is a long or a short request and prepare the message
            }
            else
            {
                Font_Color = RGB(184,134,11 );
                m_omDiagService = "You have entered an invalid data";
                UpdateData(false);
                StartTimer();    // Start de timer for tester present
            }
            CString omLog;
            CString omData = m_omMsgDataEdit;
            if (omData.GetLength() > UDS_LOG_DATA_CHARS)
            {
                CString omHex = m_omMsgDataEdit;
                omHex.Remove(' ');
                CString omMore;
                omMore.Format("... (%d bytes)", (omHex.GetLength() + 1) / 2);
                omData = omData.Left(UDS_LOG_DATA_CHARS) + omMore;
            }
            omLog.Format("TX ch=%d id=0x%X data=%s",
                         psTxCanMsgUds->m_psTxMsg->m_ucChannel,
                         psTxCanMsgUds->m_psTxMsg->m_unMsgID,
                         omData);
            AppendLog(omLog);
        }
    }
}
/**********************************************************************************************************
Function Name  : OnTimer
Input(s)       : nIDEvent
Output         : -
Functionality  : Called during Timer events to send Tester Present ot enable the SEND button
Member of      : CUDSMainWnd
Author(s)      : Sanchez Marin Maria Alejandra
Date Created   : 28.05.2013
Modifications  :
/**********************************************************************************************************/

void  CUDSMainWnd::OnTimer(UINT_PTR nIDEvent)
{

    //Envía TesterPresent
    if(nIDEvent ==ID_TIMER_TP && psTxCanMsgUds->m_psTxMsg != NULL)      //Prepare the message
    {

        if(fInterface == INTERFACE_NORMAL_ISO_29 || fInterface ==INTERFACE_NORMAL_J1939_29)
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= TRUE;               // Initial Config
        }
        else
        {
            psTxCanMsgUds->m_psTxMsg->m_ucEXTENDED= FALSE;              // Initial Config
        }
        psTxCanMsgUds->m_psTxMsg->m_ucRTR = FALSE;
        psTxCanMsgUds->m_psTxMsg->m_ucChannel = (UCHAR)m_omComboChannelUDS.GetCurSel()+1;
        psTxCanMsgUds->m_psTxMsg->m_unMsgID = (int)m_omCanID.lGetValue();
        memset(psTxCanMsgUds->m_psTxMsg->m_ucData, 0, g_udsCanFd ? 64 : 8);
        if(fInterface == INTERFACE_EXTENDED_11 )
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte-1]= TargetAddress;
        }
        if (g_udsCanFd && UdsTxDlc() > 8)
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x00;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte + 1] = 0x02;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte + 2] = 0x3E;
            psTxCanMsgUds->m_psTxMsg->m_ucDataLen = (unsigned char)UdsTxDlc();
            if (FCRespReq)
            {
                psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte + 3] = 0x80;
            }
        }
        else
        {
        psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte]= 0x02;          //The tester Present will always have 2 bytes 0x3E y 0x00
        psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+1]= 0x3E;

        if(fMsgSize)                    // how many bytes has the flow Control?
        {
            psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;
        }
        else
        {
            psTxCanMsgUds->m_psTxMsg->m_ucDataLen = aux_Finterface + 3;
        }
        if(FCRespReq)
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+2]= 0x80;    // if NoResponse Required is activated
        }
        }
        SendSimpleDiagnosticMessage();
    }
    if ( nIDEvent == ID_TIMER_SEND_BUTTON)
    {
        m_omSendButton.EnableWindow(TRUE);
        KillTimer(ID_TIMER_SEND_BUTTON);
        /* P2 runs from the end of the request; OnClientTxDone restarts it
           after the last consecutive frame. */
        if (!UdsClientTxBusy() && m_omDiagService == UDS_WAIT_RSP_TEXT)
        {
            SetResponseService(UDS_NO_RSP_TEXT);
        }
    }
    if (nIDEvent == ID_TIMER_CF)
    {
        KillTimer(ID_TIMER_CF);
        OnContinueCf(0, 0);
    }
    if (nIDEvent == ID_TIMER_CYCLIC)
    {
        OnBnClickedSendUD();
    }
}
/******************************************************************************
 Function Name  :   OnCtlColor

 Description    :   The framework calls this member function when a child
                    control is about to be drawn. Most controls send this
                    message to their parent (usually a dialog box) to prepare
                    the pDC for drawing the control using the correct colors.

 Input(s)       :   CDC* pDC       : Contains a pointer to the display context
                                     for the child window.
                    CWnd* pWnd     : Contains a pointer to the control asking
                                     for the color. May be temporary.
                    UINT nCtlColor : Contains one of the following values,
                                     specifying the type of control
 Output         :   HBRUSH:A handle to the brush that is to be used for painting
                    the control background.
 Functionality  :   The dialog is painted and the different control is draw with
                    a different colour.
 Member of      :   CUDSMainWnd

 Author(s)      :   Sanchez Marin Maria Alejandra.
 Modifications  :
******************************************************************************/
HBRUSH CUDSMainWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr =  CDialog::OnCtlColor( pDC,  pWnd,  nCtlColor);
    if (pWnd == NULL || !::IsWindow(pWnd->m_hWnd))
    {
        return hbr;
    }
    UINT nIDD = pWnd->GetDlgCtrlID();

    switch(nIDD)
    {
        case IDC_RESPONSE_DATA:
            //pDC->SetBkMode(OPAQUE );
            //pDC->SetTextColor(RGB(69, 96,200));
            pDC->SetBkColor(RGB(255,255,255));
            break;
        case IDC_DIAG_SERVICE:
            pDC->SetTextColor(Font_Color);
            pDC->SetBkColor(RGB(255,255,255));
            break;
        case IDC_NUMBER_OF_BYTES:
            //pDC->SetTextColor(RGB(69, 96,200));
            pDC->SetBkColor(RGB(255,255,255));
            break;
        default:
            break;
    }
    return hbr;

}

/**********************************************************************************************************
Function Name  : OnBnClickedTesterPresent
Input(s)       : CUDSMainWnd
Output         : -
Functionality  : This function is called by the framework when the user has pressed the TesterPresent's
                 check box to start the timer to send the tester present message or to stops its sending
Member of      : CUDSMainWnd
Author(s)      : Sanchez Marin Maria Alejandra
Date Created   : 28.03.2013
Modifications  :
/**********************************************************************************************************/
void CUDSMainWnd::OnBnClickedTesterPresent()
{
    if (m_omCheckTP.GetCheck())
    {
        CWinThread* pomThread2 = NULL ;
        StartTimer();
        if (psTxCanMsgUds ==NULL)
        {
            psTxCanMsgUds  = new mSTXSELMSGDATA;
        }
        if(psTxCanMsgUds != NULL )          // to verify if it has been created correctly
        {
            psTxCanMsgUds->m_unCount = 1;
            psTxCanMsgUds->m_psTxMsg = new STCAN_MSG[1];
        }
    }
    else
    {
        KillTimer(ID_TIMER_TP);
    }
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void CUDSMainWnd::OnEnChangeData()
{
    CEdit* e = (CEdit*)GetDlgItem(IDC_EDIT_DATA);
    if (e == NULL || !::IsWindow(e->m_hWnd))
    {
        return;
    }

    UpdateData();
    CString omByteStr;
    omByteStr = m_omMsgDataEdit;
    omByteStr.Replace(" ","");
    omByteStr.Replace("\t","");

    UINT LengthStr = omByteStr.GetLength();
    {
        CString omSpaced;
        int nIn = (int)LengthStr;
        char* pOut = omSpaced.GetBuffer(nIn + nIn / 2 + 1);
        int nOut = 0;
        for (int i = 0; i < nIn; i++)
        {
            if (i > 0 && (i % 2) == 0)
            {
                pOut[nOut++] = ' ';
            }
            pOut[nOut++] = omByteStr[i];
        }
        omSpaced.ReleaseBuffer(nOut);
        m_omMsgDataEdit = omSpaced;
    }

    UINT Data_Length = (LengthStr/2) + LengthStr%2;;
    m_stringEditDLC = (LengthStr/2) + LengthStr%2;

    BytesShown_Line = 1;
    m_abDatas = " ";
    m_omDiagService = " ";
    m_omBytes.vSetValue(0);
    UpdateData(false);

    //e->SetFocus();
    e->SetSel(0,-1); // select all text and move cursor at the end
    e->SetSel(-1);

}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void CUDSMainWnd::UpdateSendButtonState()
{
    const BOOL bHasId = (m_omCanID.lGetValue() != 0) || (m_omCheckFunctional.GetCheck() == BST_CHECKED);
    m_omSendButton.EnableWindow(bHasId ? TRUE : FALSE);
}

void CUDSMainWnd::OnEnChangeSA()
{
    UpdateData();
    SourceAddress = m_omSourceAddress.lGetValue();
    if (fInterface != INTERFACE_NORMAL_11)
    {
        setValue();
    }
    SyncToProtocol();
    UpdateSendButtonState();
}

void CUDSMainWnd::OnEnChangeTA()
{
    UpdateData();
    TargetAddress = m_omTargetAddress.lGetValue();
    if(fInterface == INTERFACE_EXTENDED_11 )
    {
        respID = RespMsgID + TargetAddress; // Casian
    }
    if(fInterface == INTERFACE_NORMAL_ISO_29 )
    {
        MsgID = (CanID&NEG_MASK_TA_ID_29Bits)+(TargetAddress & MASK_TA_ID_29Bits);
        CanID = MsgID;
        m_omCanID.vSetValue(MsgID);
    }
    else if( fInterface == INTERFACE_NORMAL_J1939_29)
    {
        MsgID = ((CanID & 0xFFFF00FF) + (TargetAddress<<8));
        CanID = MsgID;
        m_omCanID.vSetValue(MsgID);
    }
    else if (fInterface != INTERFACE_NORMAL_11)
    {
        setValue();
    }
    SyncToProtocol();
    UpdateSendButtonState();
}

void CUDSMainWnd::OnEnChangeCanID()
{
    UpdateData();
    CanID = (UINT)m_omCanID.lGetValue();
    if (fInterface == INTERFACE_NORMAL_11 || fInterface == INTERFACE_EXTENDED_11)
    {
        if (CanID > 0x7FF)
        {
            CanID = 0x7FF;
            m_omCanID.vSetValue(CanID);
        }
        if (CanID != 0)
        {
            respID = (CanID == 0x7DF) ? 0x7E8 : (int)(CanID + 8);
            if (respID > 0x7FF)
            {
                respID = (int)CanID;
            }
        }
    }
    else
    {
        if (CanID != 0)
        {
            respID = (int)CanID;
        }
    }
    SyncToProtocol();
    UpdateSendButtonState();
}

void CUDSMainWnd::SyncToProtocol()
{
    if (omManagerPtr == NULL)
    {
        return;
    }
    omManagerPtr->MsgID = CanID;
    omManagerPtr->SourceAddress = SourceAddress;
    omManagerPtr->TargetAddress = TargetAddress;
    omManagerPtr->fInterface = fInterface;
}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________
//This function is called to set in the UDS_Main_Window the value of the CanID editor Box

void CUDSMainWnd::setValue()
{

    switch (fInterface)
    {
        case INTERFACE_NORMAL_11:
        {
            MsgID = CanID;
            m_omCanID.vSetValue(MsgID);
        }
        break;
        case INTERFACE_EXTENDED_11:
        {
            MsgID = SourceAddress + CanID;
            m_omCanID.vSetValue(MsgID);

        }
        break;
        case INTERFACE_NORMAL_ISO_29:
        {
            if( SourceAddress>0x7FF)
            {
                m_omSourceAddress.vSetValue(000);    // The SA cannot be bigger than 0x7FF
            }
            MsgID = ((CanID & NEG_MASK_SA_ID_29Bits) + (SourceAddress<<11));
            CanID = MsgID;
            m_omCanID.vSetValue(MsgID);

        }
        break;
        case INTERFACE_NORMAL_J1939_29:
        {
            MsgID = ((CanID & 0xFFFFFF00) + (SourceAddress));
            CanID = MsgID;
            m_omCanID.vSetValue(MsgID);

        }
        break;

    }
}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

BOOL CUDSMainWnd::OnInitDialog()
{
    CDialog::OnInitDialog();

    /* Get CAN DIL interface */
    DIL_GetInterface(CAN, (void**)&g_pouDIL_CAN_Interface);
    if (g_pouDIL_CAN_Interface != nullptr && g_dwClientID == 0)
    {
        g_pouDIL_CAN_Interface->DILC_RegisterClient(TRUE, g_dwClientID, _("CAN_MONITOR"));
    }

    m_omCheckTP.SetCheck(BST_UNCHECKED);
    vInitializeUDSfFields();
    m_omTreeServices.SetRedraw(FALSE);
    PopulateIso14229Tree();
    m_omTreeServices.SetRedraw(TRUE);
    m_omTreeServices.Invalidate();
    m_omListAttribute.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_omListAttribute.InsertColumn(0, "Parameter", LVCFMT_LEFT, 90);
    m_omListAttribute.InsertColumn(1, "Value", LVCFMT_LEFT, 70);
    m_omListAttribute.InsertColumn(2, "Description", LVCFMT_LEFT, 220);
    m_omEditCyclicPeriod.SetWindowText("1000");
    m_omEditSecCurrent.SetWindowText("INIT");
    for (int nLvl = 1; nLvl <= 0x41; nLvl += 2)
    {
        CString omLvl;
        omLvl.Format("%02X", nLvl);
        m_omComboSecTarget.AddString(omLvl);
    }
    m_omComboSecTarget.SetCurSel(0);
    ApplySecurityTargetToRequest();
    ApplyCanFdSwitch();
    return TRUE;
}

void CUDSMainWnd::ApplyCanFdSwitch()
{
    CButton* pChk = (CButton*)GetDlgItem(IDC_CHECK_UDS_CANFD);
    if (pChk == NULL)
    {
        return;
    }
    if (!g_udsCanFdHw)
    {
        g_udsCanFd = FALSE;
        pChk->SetCheck(BST_UNCHECKED);
        pChk->EnableWindow(FALSE);
        return;
    }
    pChk->EnableWindow(TRUE);
    pChk->SetCheck(g_udsCanFd ? BST_CHECKED : BST_UNCHECKED);
}

void CUDSMainWnd::OnCanFdSwitch()
{
    if (!g_udsCanFdHw)
    {
        g_udsCanFd = FALSE;
        ApplyCanFdSwitch();
        return;
    }
    CButton* pChk = (CButton*)GetDlgItem(IDC_CHECK_UDS_CANFD);
    g_udsCanFd = (pChk != NULL && pChk->GetCheck() == BST_CHECKED);
    g_udsFdDlc = 64;
}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void CUDSMainWnd::vInitializeUDSfFields()
{

    m_omEditDLC.vSetBase(BASE_DECIMAL);
    m_omEditMsgData.vSetBase(BASE_HEXADECIMAL);

    m_omBytes.vSetBase(BASE_DECIMAL);
    m_omBytes.vSetSigned(false);

    m_omEditDLC.vSetSigned(false);
    m_omEditMsgData.vSetSigned(false);

    m_omSourceAddress.vSetBase(BASE_HEXADECIMAL);
    m_omSourceAddress.vSetSigned(true);
    m_omSourceAddress.vSetValue(SourceAddress);

    m_omTargetAddress.vSetBase(BASE_HEXADECIMAL);
    m_omTargetAddress.vSetSigned(true);
    m_omTargetAddress.vSetValue(TargetAddress);

    m_omCanID.vSetBase(BASE_HEXADECIMAL);
    m_omCanID.vSetSigned(false);

    switch (fInterface)                       // To evaluate the status of the Textbox  SA y TA
    {
        case INTERFACE_NORMAL_11:
        {
            m_omSourceAddress.LimitText(3);
            m_omTargetAddress.LimitText(3);
            m_omSourceAddress.SetReadOnly(FALSE);
            m_omTargetAddress.SetReadOnly(FALSE);
            m_omCanID.SetReadOnly(FALSE);
            m_omCanID.LimitText(3);
            m_omCanID.vSetValue(CanID);
        }
        break;
        case INTERFACE_EXTENDED_11:
        {
            m_omSourceAddress.LimitText(2);
            m_omTargetAddress.LimitText(2);
            m_omSourceAddress.SetReadOnly(FALSE);
            m_omTargetAddress.SetReadOnly(FALSE);
            m_omCanID.SetReadOnly(FALSE);
            m_omCanID.LimitText(3);
            m_omCanID.vSetValue(CanID+SourceAddress);
        }
        break;
        case INTERFACE_NORMAL_ISO_29:
        {
            m_omSourceAddress.SetReadOnly(FALSE);
            m_omTargetAddress.SetReadOnly(FALSE);
            m_omSourceAddress.SetLimitText(3);
            m_omTargetAddress.SetLimitText(3);
            m_omCanID.SetReadOnly(FALSE);
            m_omCanID.LimitText(8);
            m_omCanID.vSetValue(CanID);
        }
        break;
        case INTERFACE_NORMAL_J1939_29:
        {
            m_omSourceAddress.SetReadOnly(FALSE);
            m_omTargetAddress.SetReadOnly(FALSE);
            m_omSourceAddress.SetLimitText(2);
            m_omTargetAddress.SetLimitText(2);
            m_omCanID.SetReadOnly(FALSE);
            m_omCanID.LimitText(8);
            m_omCanID.vSetValue(CanID);
        }
        break;

    }

    UpdateSendButtonState();


    for (INT_PTR i = 0; i < TotalChannel; i++)
    {
        CString omChannel;
        omChannel.Format("%d", i + 1);
        m_omComboChannelUDS.InsertString(i, omChannel);
    }
    m_omComboChannelUDS.SetCurSel(0);
    m_omEditDLC.vSetValue(0);
    m_Font.CreatePointFont(110, "Courier");
    CWnd* pResponse = GetDlgItem(IDC_RESPONSE_DATA);
    if (pResponse != NULL && ::IsWindow(pResponse->m_hWnd))
    {
        pResponse->SetFont(&m_Font);
    }
    m_Font.Detach();
    m_Font.CreateFont(               14,                        // nHeight
                                     5,                         // nWidth
                                     0,                         // nEscapement
                                     0,                         // nOrientation
                                     FW_BOLD,                   // nWeight
                                     FALSE,                     // bItalic
                                     FALSE,                     // bUnderline
                                     0,                         // cStrikeOut
                                     ANSI_CHARSET,              // nCharSet
                                     OUT_DEFAULT_PRECIS,        // nOutPrecision
                                     CLIP_DEFAULT_PRECIS,       // nClipPrecision
                                     DEFAULT_QUALITY,           // nQuality
                                     DEFAULT_PITCH | FF_ROMAN,  // nPitchAndFamily
                                     "Arial");                  // Facename

    CWnd* pService = GetDlgItem(IDC_DIAG_SERVICE);
    if (pService != NULL && ::IsWindow(pService->m_hWnd))
    {
        pService->SetFont(&m_Font);
    }
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void CUDSMainWnd::vSetDILInterfacePtr(void* ptrDILIntrf)
{

    g_pouDIL_CAN_Interface = (CBaseDIL_CAN*)ptrDILIntrf;
    g_pouDIL_CAN_Interface->DILC_RegisterClient(TRUE, g_dwClientID, _("CAN_MONITOR"));
    //g_pouDIL_CAN_Interface->DILC_ManageMsgBuf(MSGBUF_CLEAR, g_dwClientID, &m_ouMCCanBufFSE);
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void* CUDSMainWnd::pGetDILInterfacePtr()
{
    return (void*)g_pouDIL_CAN_Interface;
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________
/** This function is called from the Mainframe when the user has changed the channel selection */

void CUDSMainWnd::vUpdateChannelIDInfo()
{
    m_omComboChannelUDS.ResetContent();
    LONG lParam = 0;
    if(((CBaseDIL_CAN*)CUDSMainWnd::pGetDILInterfacePtr()) != NULL)
    {
        if(((CBaseDIL_CAN*)CUDSMainWnd::pGetDILInterfacePtr())
                ->DILC_GetControllerParams(lParam, 0, NUMBER_HW) == S_OK)
        {
            UINT nHardware = (UINT)lParam;
            for( INT_PTR i = 0; i < lParam; i++)
            {
                CString omChannel;
                omChannel.Format("%d", i + 1);
                m_omComboChannelUDS.InsertString(i, omChannel);
            }
        }
    }
    m_omComboChannelUDS.SetCurSel(0);
    if (g_pouDIL_CAN_Interface != nullptr && g_dwClientID == 0)
    {
        g_pouDIL_CAN_Interface->DILC_RegisterClient(TRUE, g_dwClientID, _("CAN_MONITOR"));
    }
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

CUDSMainWnd* CUDSMainWnd::s_podGetUdsMsgManager()
{
    if( m_spodInstance == NULL )
    {
        m_spodInstance = new CUDSMainWnd(0,0,INTERFACE_NORMAL_11,0);
        /*  m_spodInstance->objPrue =1;*/
        // Handling NULL condition is caller's duty
        if( m_spodInstance == NULL )
        {
            // Help debugging
            ASSERT( FALSE );
        }
    }
    // Return the pointer or NULL in case of failure
    return m_spodInstance;
}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

int CUDSMainWnd::nCalculateCurrTime(BOOL bFromDIL)      // Calculates the differential time in sec
{
    SYSTEMTIME CurrSysTimes;
    UINT64 c_TimeStamp;

    if (bFromDIL == FALSE)
    {
        GetLocalTime(&CurrSysTimes);
    }
    else
    {
        LARGE_INTEGER QueryTickCount;
        g_pouDIL_CAN_Interface->DILC_GetTimeModeMapping(CurrSysTimes, c_TimeStamp,QueryTickCount);
    }
    int nResult = (CurrSysTimes.wHour * 3600000 + CurrSysTimes.wMinute * 60000
                   + CurrSysTimes.wSecond) * 1000 + CurrSysTimes.wMilliseconds *1;
    return nResult;                  // Milliseconds
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

void CUDSMainWnd::CalculateDiffTime(void)
{
    if(c_unPreviousTime != -1 )
    {
        c_dDiffTime         = nCalculateCurrTime(FALSE) - c_unPreviousTime;
    }
    else
    {
        c_unPreviousTime = nCalculateCurrTime(FALSE) ;
    }
}
/**********************************************************************************************************
Function Name  : initialEval
Input(s)       : CUDSMainWnd
Output         : -
Functionality  : This funcition is called to evaluate if the sent message waits for
                 a response or nor (No Positive Response Required)
Member of      : CUDSMainWnd
Author(s)      : Sanchez Marin Maria Alejandra
Date Created   : 30.05.2013
Modifications  :
/**********************************************************************************************************/

CString CUDSMainWnd::initialEval(CString Data2Send )
{
    int CService;
    CService = strtol(CurrentService, NULL, 16);
    if ( CService==0x10 || CService==0x11 ||CService==0x28 ||CService==0x31 ||CService==0x3E ||CService==0x85 ||CService==0xA0)
    {
        CString SendingData = Data2Send;
        //SendingData.Replace(" ",""); //added by Alejandra - not working
        SendingData.Remove(' '); // added in adition to previos function not working
        SendingData = Data2Send.Right(Data2Send.GetLength() - NO_OF_CHAR_IN_BYTE);

        int pos;

        pos = SendingData.Find('8', 0);

        //CString SecondByte = SendingData.Left(1);         //added by Alejandra - not working
        if(pos == 1) //initial if(SecondByte == '8')
        {
            Font_Color = RGB(0,255,0 );
            return "     No Response Required";
        }
    }
    Font_Color = RGB(184,134,11 );
    return UDS_WAIT_RSP_TEXT;
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________


void CUDSMainWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if ( nChar == VK_DELETE )
    {
        OnChar(nChar, 0, 0);
    }
    else
    {
        CUDSMainWnd::OnKeyDown(nChar, nRepCnt, nFlags);
    }
}


BOOL CUDSMainWnd::PreTranslateMessage(MSG* pMsg)
{
    // Capture the space character and
    // do not process the same
    BOOL bSkip = FALSE;
    if ( pMsg->message == WM_CHAR )
    {
        //if ( pMsg->wParam == defEMPTY_CHAR)
        //{
        //    bSkip = TRUE;
        //}
    }
    if ( bSkip == FALSE)
    {
        //bSkip = CUDSMainWnd::PreTranslateMessage(pMsg);
    }

    return bSkip;
}

LRESULT CUDSMainWnd::OnCommandHelp ( WPARAM wParam, LPARAM lParam )
{
    DWORD dwDirLen = 1024, dwRetVal = 0;
    char cCurrWkDirPath[1024] = { 0 }, cChmFilePath[2048] = {0};
    dwRetVal = GetModuleFileName ( NULL, cCurrWkDirPath, dwDirLen );
    PathRemoveFileSpec ( cCurrWkDirPath );
    CString strChmFilePath = "";
    strChmFilePath = PathCombine ( cChmFilePath, cCurrWkDirPath, "BUSMASTER.chm ::/topics/Diagnostics_Main_Window.html" );
    
    ::HtmlHelp ( nullptr, strChmFilePath, HH_DISPLAY_TOPIC, 0 );

    return S_OK;
}

void UdsDrainRx();
void UdsFlushRx();
int UdsClientLongRxCopy(BYTE* out, int cap, int* total, BOOL* done);
CString UdsClientLongRxTakeSvc();

LRESULT CUDSMainWnd::OnEvaluateCanMsg(WPARAM wParam, LPARAM lParam)
{
    if (wParam == 1 || lParam == 0)
    {
        UdsDrainRx();
        return 0;
    }
    STCAN_MSG* pMsg = reinterpret_cast<STCAN_MSG*>(lParam);
    if (pMsg != nullptr)
    {
        EvaluateMessage(*pMsg);
        delete pMsg;
    }
    return 0;
}

LRESULT CUDSMainWnd::OnSendFlowControl(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    PrepareFlowControl();
    return 0;
}

LRESULT CUDSMainWnd::OnLongRxUi(WPARAM wParam, LPARAM /*lParam*/)
{
    if (wParam & 8)
    {
        SetDlgItemText(IDC_RESPONSE_DATA, "\r\nISO-TP receive aborted: consecutive frame sequence error");
        m_omSendButton.EnableWindow(TRUE);
        KillTimer(ID_TIMER_SEND_BUTTON);
        return 0;
    }
    if (wParam & 4)
    {
        KillTimer(ID_TIMER_SEND_BUTTON);
        SetTimer(ID_TIMER_SEND_BUTTON, P2_Time, NULL);
        return 0;
    }
    if (wParam & 2)
    {
        SetResponseService(UdsClientLongRxTakeSvc());
    }
    BOOL done = (wParam & 1) ? TRUE : FALSE;
    BYTE buf[8192];
    int total = 0;
    BOOL fin = FALSE;
    int got = UdsClientLongRxCopy(buf, (int)sizeof(buf), &total, &fin);
    if (total > 0)
    {
        ShowResponseBytes(buf, got, total, done || fin);
        m_omBytes.vSetValue(total);
        if (done || fin)
        {
            m_omSendButton.EnableWindow(TRUE);
            KillTimer(ID_TIMER_SEND_BUTTON);
            TryAutoSendSecurityKeyFromResponse();
        }
        else
        {
            m_omSendButton.EnableWindow(FALSE);
            SetTimer(ID_TIMER_SEND_BUTTON, P2_Time, NULL);
        }
        return 0;
    }
    if (done)
    {
        BYTE cap[512];
        int n = DIL_UDS_CopyResponse(cap, (int)sizeof(cap));
        if (n > 0)
        {
            ShowResponseBytes(cap, n, n, TRUE);
            m_omBytes.vSetValue(n);
        }
        m_omSendButton.EnableWindow(TRUE);
        KillTimer(ID_TIMER_SEND_BUTTON);
        TryAutoSendSecurityKeyFromResponse();
    }
    return 0;
}

void CUDSMainWnd::ShowResponseBytes(const BYTE* data, int got, int total, BOOL done)
{
    HWND h = ::GetDlgItem(m_hWnd, IDC_RESPONSE_DATA);
    if (h == NULL)
    {
        return;
    }
    CString text;
    /* Only dump the full hex when the PDU is complete. Intermediate updates
       for large 76 responses used to SetWindowText on every CF and re-enter
       the RX path through the message pump. */
    BOOL full = done ? TRUE : FALSE;
    if (!full)
    {
        text.Format("\r\nReceiving %d / %d bytes...", got, total);
    }
    else
    {
        int n = got;
        if (data == NULL || n < 0)
        {
            n = 0;
        }
        int lines = (n + 14) / 15;
        int cap = n * 3 + lines * 24 + 80;
        if (cap < 64)
        {
            cap = 64;
        }
        char* buf = text.GetBuffer(cap);
        int o = 0;
        int pos = 0;
        while (pos < n)
        {
            o += sprintf(buf + o, "\r\n%4d -> ", pos + 1);
            int take = n - pos;
            if (take > 15)
            {
                take = 15;
            }
            for (int i = 0; i < take; i++)
            {
                o += sprintf(buf + o, "%02X ", data[pos + i]);
            }
            pos += take;
        }
        if (done && total > got)
        {
            o += sprintf(buf + o, "\r\nIncomplete %d / %d bytes", got, total);
        }
        buf[o] = 0;
        text.ReleaseBuffer(o);
    }
    Bytes_to_Show = text;
    m_abDatas = text;
    ::SendMessage(h, EM_SETLIMITTEXT, 0, 0);
    ::SetWindowText(h, text);
    if (done)
    {
        ::SendMessage(h, EM_SETSEL, 0, 0);
        ::SendMessage(h, EM_SCROLLCARET, 0, 0);
    }
}

void CUDSMainWnd::SetResponseService(const CString& text)
{
    m_omDiagService = text;
    SetDlgItemText(IDC_DIAG_SERVICE, text);
}

void CUDSMainWnd::OnDestroy()
{
    KillTimer(ID_TIMER_CF);
    UdsClientTxCancel();
    UdsFlushRx();
    MSG msg;
    while (PeekMessage(&msg, m_hWnd, WM_UDS_CONTINUE_CF, WM_UDS_CONTINUE_CF, PM_REMOVE))
    {
    }
    while (PeekMessage(&msg, m_hWnd, WM_UDS_EVALUATE_MSG, WM_UDS_EVALUATE_MSG, PM_REMOVE))
    {
        if (msg.lParam != 0)
        {
            delete reinterpret_cast<STCAN_MSG*>(msg.lParam);
        }
    }
    if (omMainWnd == this)
    {
        omMainWnd = NULL;
    }
    CDialog::OnDestroy();
}

void CUDSMainWnd::AppendLog(const CString& omText)
{
    if (!::IsWindow(m_omEditLog.GetSafeHwnd()))
    {
        return;
    }
    CTime omNow = CTime::GetCurrentTime();
    CString omLine;
    omLine.Format("[%s] %s\r\n", omNow.Format("%H:%M:%S"), omText);
    /* SetWindowText of the whole log re-lays out every line, so each append
       cost grew with the log size and stalled the UI thread for seconds. */
    if (m_omEditLog.GetLimitText() < UDS_LOG_LIMIT)
    {
        m_omEditLog.SetLimitText(UDS_LOG_LIMIT);
    }
    int nLen = m_omEditLog.GetWindowTextLength();
    if (nLen + omLine.GetLength() > UDS_LOG_KEEP)
    {
        int nCut = m_omEditLog.LineIndex(m_omEditLog.LineFromChar(nLen - UDS_LOG_KEEP / 2) + 1);
        if (nCut > 0)
        {
            m_omEditLog.SetSel(0, nCut);
            m_omEditLog.ReplaceSel("");
            nLen = m_omEditLog.GetWindowTextLength();
        }
    }
    m_omEditLog.SetSel(nLen, nLen);
    m_omEditLog.ReplaceSel(omLine);
}

void CUDSMainWnd::SetRequestPayload(const CString& omHex)
{
    CString omClean = omHex;
    omClean.Replace(" ", "");
    omClean.MakeUpper();
    m_omEditMsgData.SetWindowText(omClean);
    m_omMsgDataEdit = omClean;
    m_omEditDLC.vSetValue(omClean.GetLength() / 2);
}

void CUDSMainWnd::FillAttribute(const CString& omName, const CString& omValue, const CString& omDesc)
{
    const int nRow = m_omListAttribute.InsertItem(m_omListAttribute.GetItemCount(), omName);
    m_omListAttribute.SetItemText(nRow, 1, omValue);
    m_omListAttribute.SetItemText(nRow, 2, omDesc);
}

void CUDSMainWnd::PopulateIso14229Tree()
{
    m_omTreeServices.DeleteAllItems();
    for (int nSvc = 0; nSvc < kIso14229ServiceCount; ++nSvc)
    {
        const UdsServiceDef& s = kIso14229Services[nSvc];
        CString omLabel;
        omLabel.Format("%s (%02X hex) service", s.m_pchName, s.m_bySid);
        HTREEITEM hSvc = m_omTreeServices.InsertItem(omLabel, TVI_ROOT, TVI_LAST);
        m_omTreeServices.SetItemData(hSvc, ((DWORD)nSvc << 16) | 0xFFFF);
        HTREEITEM hLastGroup = nullptr;
        CString omLastGroup;
        for (int nSub = 0; nSub < s.m_nSub; ++nSub)
        {
            const UdsSubFn& sf = s.m_psSub[nSub];
            HTREEITEM hParent = hSvc;
            if (sf.m_pchGroup != nullptr && sf.m_pchGroup[0] != '\0')
            {
                if (hLastGroup != nullptr && omLastGroup == sf.m_pchGroup)
                {
                    hParent = hLastGroup;
                }
                else
                {
                    HTREEITEM hFound = m_omTreeServices.GetChildItem(hSvc);
                    HTREEITEM hGroup = nullptr;
                    while (hFound != nullptr)
                    {
                        if (m_omTreeServices.GetItemText(hFound) == sf.m_pchGroup)
                        {
                            hGroup = hFound;
                            break;
                        }
                        hFound = m_omTreeServices.GetNextSiblingItem(hFound);
                    }
                    if (hGroup == nullptr)
                    {
                        hGroup = m_omTreeServices.InsertItem(sf.m_pchGroup, hSvc, TVI_LAST);
                        m_omTreeServices.SetItemData(hGroup, ((DWORD)nSvc << 16) | 0xFFFE);
                    }
                    hLastGroup = hGroup;
                    omLastGroup = sf.m_pchGroup;
                    hParent = hGroup;
                }
            }
            CString omSub;
            if (sf.m_bySf == 0xFF || (sf.m_pchGroup != nullptr && sf.m_pchGroup[0] != '\0'))
            {
                omSub = sf.m_pchName;
            }
            else if (s.m_bEmitSf)
            {
                omSub.Format("%s (%02X hex)", sf.m_pchName, sf.m_bySf);
            }
            else
            {
                omSub = sf.m_pchName;
            }
            HTREEITEM hSub = m_omTreeServices.InsertItem(omSub, hParent, TVI_LAST);
            m_omTreeServices.SetItemData(hSub, ((DWORD)nSvc << 16) | (DWORD)nSub);
        }
    }
}

void CUDSMainWnd::ApplySelectedService()
{
    HTREEITEM hSel = m_omTreeServices.GetSelectedItem();
    if (hSel == nullptr)
    {
        return;
    }
    const DWORD dwData = (DWORD)m_omTreeServices.GetItemData(hSel);
    const int nSvc = (int)(dwData >> 16);
    const int nSub = (int)(dwData & 0xFFFF);
    if (nSvc < 0 || nSvc >= kIso14229ServiceCount)
    {
        CString omText = m_omTreeServices.GetItemText(hSel);
        const int nPos = omText.Find(":");
        if (nPos >= 0)
        {
            SetRequestPayload(omText.Mid(nPos + 1));
        }
        return;
    }
    const UdsServiceDef& s = kIso14229Services[nSvc];
    m_omListAttribute.DeleteAllItems();
    CString omSid;
    omSid.Format("%02Xh", s.m_bySid);
    FillAttribute("SID", omSid, s.m_pchName);
    CString omPayload;
    omPayload.Format("%02X", s.m_bySid);
    if (nSub != 0xFFFF && nSub < s.m_nSub)
    {
        const UdsSubFn& sf = s.m_psSub[nSub];
        if (s.m_bEmitSf && sf.m_bySf != 0xFF)
        {
            CString omSf;
            omSf.Format("%02X", sf.m_bySf);
            omPayload += omSf;
            FillAttribute("subFunction", omSf + "h", sf.m_pchName);
        }
        else if (sf.m_bySf == 0xFF)
        {
            FillAttribute("subFunction", "custom", "edit Data Bytes: 28 + controlType + communicationType");
        }
        omPayload += sf.m_pchExtra;
        FillAttribute("Default payload", omPayload, sf.m_pchAttr);
        if (s.m_bCyclicHint)
        {
            FillAttribute("Cyclic", "recommended", "Enable Cyclic Send and set Period (ms)");
        }
    }
    else
    {
        FillAttribute("Usage", "expand node", "Select a sub-function to fill request bytes");
        if (s.m_nSub > 0 && s.m_bEmitSf)
        {
            CString omSf;
            omSf.Format("%02X", s.m_psSub[0].m_bySf);
            omPayload += omSf;
            omPayload += s.m_psSub[0].m_pchExtra;
        }
        else if (s.m_nSub > 0)
        {
            omPayload += s.m_psSub[0].m_pchExtra;
        }
    }
    if (s.m_bySid == 0x27)
    {
        FillAttribute("Seed&Key DLL", "Vector", "GenerateKeyEx / GenerateKeyExOpt; Unlock sends requestSeed then auto sendKey");
        BYTE bySf = 0x01;
        if (nSub != 0xFFFF && nSub < s.m_nSub)
        {
            bySf = s.m_psSub[nSub].m_bySf;
        }
        else if (m_omComboSecTarget.GetCurSel() >= 0)
        {
            CString omLvl;
            m_omComboSecTarget.GetLBText(m_omComboSecTarget.GetCurSel(), omLvl);
            bySf = (BYTE)strtol(omLvl, nullptr, 16);
            omPayload.Format("27%02X", bySf);
        }
        if ((bySf & 0x01) == 0 && bySf > 0)
        {
            bySf = (BYTE)(bySf - 1);
        }
        SyncTargetLevelCombo(bySf);
    }
    SetRequestPayload(omPayload);
    OnEnChangeData();
}

void CUDSMainWnd::OnTvnSelchangedServices(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    ApplySelectedService();
    *pResult = 0;
}

UINT CUDSMainWnd::GetCyclicPeriodMs()
{
    CString omText;
    m_omEditCyclicPeriod.GetWindowText(omText);
    const int nVal = atoi(omText);
    if (nVal < 10)
    {
        return 10;
    }
    return (UINT)nVal;
}

void CUDSMainWnd::StartCyclicTimer()
{
    StopCyclicTimer();
    m_unCyclicPeriodMs = GetCyclicPeriodMs();
    m_nCyclicTimer = SetTimer(ID_TIMER_CYCLIC, m_unCyclicPeriodMs, nullptr);
    CString omLog;
    omLog.Format("Cyclic send ON, period=%u ms", m_unCyclicPeriodMs);
    AppendLog(omLog);
}

void CUDSMainWnd::StopCyclicTimer()
{
    if (m_nCyclicTimer != 0)
    {
        KillTimer(ID_TIMER_CYCLIC);
        m_nCyclicTimer = 0;
        AppendLog("Cyclic send OFF");
    }
}

void CUDSMainWnd::OnBnClickedCyclic()
{
    if (m_omCheckCyclic.GetCheck())
    {
        StartCyclicTimer();
    }
    else
    {
        StopCyclicTimer();
    }
}

void CUDSMainWnd::OnBnClickedFunctional()
{
    UpdateSendButtonState();
    AppendLog(m_omCheckFunctional.GetCheck() ? "Functional request ON (11-bit 0x7DF)" : "Functional request OFF");
}

void CUDSMainWnd::OnBnClickedAdd()
{
    CString omData;
    m_omEditMsgData.GetWindowText(omData);
    omData.Replace(" ", "");
    if (omData.IsEmpty())
    {
        omData = "3E00";
    }
    CString omLabel;
    omLabel.Format("User request: %s", omData);
    HTREEITEM hParent = m_omTreeServices.GetSelectedItem();
    if (hParent == nullptr)
    {
        hParent = TVI_ROOT;
    }
    HTREEITEM hNew = m_omTreeServices.InsertItem(omLabel, hParent, TVI_LAST);
    m_omTreeServices.SetItemData(hNew, 0xFFFFFFFF);
    m_omTreeServices.SelectItem(hNew);
}

void CUDSMainWnd::OnBnClickedDelete()
{
    HTREEITEM hSel = m_omTreeServices.GetSelectedItem();
    if (hSel != nullptr)
    {
        m_omTreeServices.DeleteItem(hSel);
    }
}

static CString XmlEscapeAttr(CString omText)
{
    omText.Replace("&", "&amp;");
    omText.Replace("<", "&lt;");
    omText.Replace(">", "&gt;");
    omText.Replace("\"", "&quot;");
    return omText;
}

static CString XmlUnescapeAttr(CString omText)
{
    omText.Replace("&quot;", "\"");
    omText.Replace("&lt;", "<");
    omText.Replace("&gt;", ">");
    omText.Replace("&amp;", "&");
    return omText;
}

static CString XmlGetAttr(const CString& omLine, const char* pchName)
{
    CString omKey;
    omKey.Format("%s=\"", pchName);
    const int nStart = omLine.Find(omKey);
    if (nStart < 0)
    {
        return "";
    }
    const int nVal = nStart + omKey.GetLength();
    const int nEnd = omLine.Find('\"', nVal);
    if (nEnd < 0)
    {
        return "";
    }
    return XmlUnescapeAttr(omLine.Mid(nVal, nEnd - nVal));
}

CString CUDSMainWnd::GetTreeItemPayload(HTREEITEM hItem)
{
    if (hItem == nullptr)
    {
        return "";
    }
    const DWORD dwData = (DWORD)m_omTreeServices.GetItemData(hItem);
    if (dwData == 0xFFFFFFFF)
    {
        CString omText = m_omTreeServices.GetItemText(hItem);
        const int nPos = omText.ReverseFind(':');
        if (nPos >= 0)
        {
            CString omPayload = omText.Mid(nPos + 1);
            omPayload.Replace(" ", "");
            omPayload.Trim();
            return omPayload;
        }
        return "";
    }
    const int nSvc = (int)(dwData >> 16);
    const int nSub = (int)(dwData & 0xFFFF);
    if (nSvc < 0 || nSvc >= kIso14229ServiceCount)
    {
        return "";
    }
    const UdsServiceDef& s = kIso14229Services[nSvc];
    CString omPayload;
    omPayload.Format("%02X", s.m_bySid);
    if (nSub != 0xFFFF && nSub < s.m_nSub)
    {
        if (s.m_bEmitSf)
        {
            CString omSf;
            omSf.Format("%02X", s.m_psSub[nSub].m_bySf);
            if (s.m_psSub[nSub].m_bySf != 0xFF)
            {
                omPayload += omSf;
            }
        }
        omPayload += s.m_psSub[nSub].m_pchExtra;
    }
    return omPayload;
}

static int FindIsoServiceIndex(BYTE bySid)
{
    for (int i = 0; i < kIso14229ServiceCount; ++i)
    {
        if (kIso14229Services[i].m_bySid == bySid)
        {
            return i;
        }
    }
    return -1;
}

static int FindIsoSubIndex(int nSvc, BYTE bySf)
{
    if (nSvc < 0 || nSvc >= kIso14229ServiceCount)
    {
        return -1;
    }
    const UdsServiceDef& s = kIso14229Services[nSvc];
    for (int i = 0; i < s.m_nSub; ++i)
    {
        if (s.m_psSub[i].m_bySf == bySf)
        {
            return i;
        }
    }
    return -1;
}

void CUDSMainWnd::OnBnClickedImport()
{
    CFileDialog omDlg(TRUE, "xml", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "UDS services XML (*.xml)|*.xml|Service list (*.txt;*.csv)|*.txt;*.csv|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CStdioFile omFile;
    if (!omFile.Open(omDlg.GetPathName(), CFile::modeRead | CFile::typeText))
    {
        AppendLog("Import failed: cannot open file");
        return;
    }
    CString omLine;
    CString omHead;
    if (!omFile.ReadString(omHead))
    {
        omFile.Close();
        return;
    }
    const BOOL bXml = (omHead.Find("<?xml") >= 0) || (omHead.Find("<UdsServices") >= 0);
    if (bXml)
    {
        m_omTreeServices.DeleteAllItems();
        HTREEITEM hSvc = nullptr;
        auto fnConsume = [&](const CString& omCur)
        {
            if (omCur.Find("<Service ") >= 0)
            {
                const BYTE bySid = (BYTE)strtol(XmlGetAttr(omCur, "sid"), nullptr, 16);
                CString omName = XmlGetAttr(omCur, "name");
                if (omName.IsEmpty())
                {
                    omName.Format("Service %02X", bySid);
                }
                CString omLabel;
                omLabel.Format("%s (%02X hex) service", omName, bySid);
                hSvc = m_omTreeServices.InsertItem(omLabel, TVI_ROOT, TVI_LAST);
                const int nSvc = FindIsoServiceIndex(bySid);
                m_omTreeServices.SetItemData(hSvc, nSvc >= 0 ? (((DWORD)nSvc << 16) | 0xFFFF) : 0xFFFFFFFF);
            }
            else if (omCur.Find("<SubFunction ") >= 0)
            {
                const BYTE bySf = (BYTE)strtol(XmlGetAttr(omCur, "sf"), nullptr, 16);
                CString omName = XmlGetAttr(omCur, "name");
                CString omPayload = XmlGetAttr(omCur, "payload");
                if (omName.IsEmpty())
                {
                    omName = omPayload;
                }
                CString omLabel;
                omLabel.Format("%s (%02X hex)", omName, bySf);
                HTREEITEM hParent = (hSvc != nullptr) ? hSvc : TVI_ROOT;
                HTREEITEM hSub = m_omTreeServices.InsertItem(omLabel, hParent, TVI_LAST);
                const DWORD dwSvc = (hSvc != nullptr) ? (DWORD)m_omTreeServices.GetItemData(hSvc) : 0xFFFFFFFF;
                const int nSvc = (dwSvc == 0xFFFFFFFF) ? -1 : (int)(dwSvc >> 16);
                const int nSub = FindIsoSubIndex(nSvc, bySf);
                if (nSub >= 0)
                {
                    m_omTreeServices.SetItemData(hSub, ((DWORD)nSvc << 16) | (DWORD)nSub);
                }
                else
                {
                    CString omUser;
                    omUser.Format("User request: %s", omPayload);
                    m_omTreeServices.SetItemText(hSub, omUser);
                    m_omTreeServices.SetItemData(hSub, 0xFFFFFFFF);
                }
            }
            else if (omCur.Find("<UserRequest ") >= 0)
            {
                CString omPayload = XmlGetAttr(omCur, "payload");
                CString omName = XmlGetAttr(omCur, "name");
                if (omName.IsEmpty())
                {
                    omName = "User request";
                }
                CString omLabel;
                omLabel.Format("%s: %s", omName, omPayload);
                HTREEITEM hParent = (hSvc != nullptr) ? hSvc : TVI_ROOT;
                HTREEITEM hUser = m_omTreeServices.InsertItem(omLabel, hParent, TVI_LAST);
                m_omTreeServices.SetItemData(hUser, 0xFFFFFFFF);
            }
            else if (omCur.Find("</Service>") >= 0)
            {
                hSvc = nullptr;
            }
        };
        fnConsume(omHead);
        while (omFile.ReadString(omLine))
        {
            fnConsume(omLine);
        }
        omFile.Close();
        AppendLog("Imported XML: " + omDlg.GetPathName());
        return;
    }

    HTREEITEM hRoot = m_omTreeServices.InsertItem("Imported", TVI_ROOT, TVI_LAST);
    auto fnAddTxt = [&](const CString& omSrc)
    {
        CString omCur = omSrc;
        omCur.Trim();
        if (omCur.IsEmpty() || omCur[0] == '#')
        {
            return;
        }
        HTREEITEM hItem = m_omTreeServices.InsertItem("User request: " + omCur, hRoot, TVI_LAST);
        m_omTreeServices.SetItemData(hItem, 0xFFFFFFFF);
    };
    fnAddTxt(omHead);
    while (omFile.ReadString(omLine))
    {
        fnAddTxt(omLine);
    }
    omFile.Close();
    AppendLog("Imported text: " + omDlg.GetPathName());
}

void CUDSMainWnd::OnBnClickedExport()
{
    CFileDialog omDlg(FALSE, "xml", "uds_services.xml", OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                      "UDS services XML (*.xml)|*.xml|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CStdioFile omFile;
    if (!omFile.Open(omDlg.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::typeText))
    {
        AppendLog("Export failed: cannot create file");
        return;
    }
    omFile.WriteString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    omFile.WriteString("<UdsServices version=\"1\">\n");

    HTREEITEM hItem = m_omTreeServices.GetRootItem();
    while (hItem != nullptr)
    {
        const DWORD dwData = (DWORD)m_omTreeServices.GetItemData(hItem);
        const CString omText = m_omTreeServices.GetItemText(hItem);
        if (dwData == 0xFFFFFFFF)
        {
            CString omLine;
            omLine.Format("  <UserRequest name=\"%s\" payload=\"%s\"/>\n",
                          (LPCTSTR)XmlEscapeAttr(omText), (LPCTSTR)XmlEscapeAttr(GetTreeItemPayload(hItem)));
            omFile.WriteString(omLine);
        }
        else
        {
            const int nSvc = (int)(dwData >> 16);
            CString omSid = "00";
            CString omName = omText;
            if (nSvc >= 0 && nSvc < kIso14229ServiceCount)
            {
                omSid.Format("%02X", kIso14229Services[nSvc].m_bySid);
                omName = kIso14229Services[nSvc].m_pchName;
            }
            CString omOpen;
            omOpen.Format("  <Service sid=\"%s\" name=\"%s\">\n",
                          (LPCTSTR)omSid, (LPCTSTR)XmlEscapeAttr(omName));
            omFile.WriteString(omOpen);

            HTREEITEM hChild = m_omTreeServices.GetChildItem(hItem);
            while (hChild != nullptr)
            {
                const DWORD dwChild = (DWORD)m_omTreeServices.GetItemData(hChild);
                HTREEITEM hGrand = m_omTreeServices.GetChildItem(hChild);
                if (hGrand != nullptr)
                {
                    while (hGrand != nullptr)
                    {
                        const DWORD dwGrand = (DWORD)m_omTreeServices.GetItemData(hGrand);
                        if (dwGrand == 0xFFFFFFFF)
                        {
                            CString omLine;
                            omLine.Format("    <UserRequest name=\"%s\" payload=\"%s\"/>\n",
                                          (LPCTSTR)XmlEscapeAttr(m_omTreeServices.GetItemText(hGrand)),
                                          (LPCTSTR)XmlEscapeAttr(GetTreeItemPayload(hGrand)));
                            omFile.WriteString(omLine);
                        }
                        else
                        {
                            const int nSub = (int)(dwGrand & 0xFFFF);
                            CString omSf = "00";
                            CString omSubName = m_omTreeServices.GetItemText(hGrand);
                            if (nSvc >= 0 && nSvc < kIso14229ServiceCount &&
                                nSub != 0xFFFF && nSub != 0xFFFE && nSub < kIso14229Services[nSvc].m_nSub)
                            {
                                omSf.Format("%02X", kIso14229Services[nSvc].m_psSub[nSub].m_bySf);
                                omSubName = kIso14229Services[nSvc].m_psSub[nSub].m_pchName;
                            }
                            CString omLine;
                            omLine.Format("    <SubFunction sf=\"%s\" name=\"%s\" payload=\"%s\"/>\n",
                                          (LPCTSTR)omSf, (LPCTSTR)XmlEscapeAttr(omSubName),
                                          (LPCTSTR)XmlEscapeAttr(GetTreeItemPayload(hGrand)));
                            omFile.WriteString(omLine);
                        }
                        hGrand = m_omTreeServices.GetNextSiblingItem(hGrand);
                    }
                }
                else if (dwChild == 0xFFFFFFFF)
                {
                    CString omLine;
                    omLine.Format("    <UserRequest name=\"%s\" payload=\"%s\"/>\n",
                                  (LPCTSTR)XmlEscapeAttr(m_omTreeServices.GetItemText(hChild)),
                                  (LPCTSTR)XmlEscapeAttr(GetTreeItemPayload(hChild)));
                    omFile.WriteString(omLine);
                }
                else
                {
                    const int nSub = (int)(dwChild & 0xFFFF);
                    if (nSub != 0xFFFE)
                    {
                        CString omSf = "00";
                        CString omSubName = m_omTreeServices.GetItemText(hChild);
                        if (nSvc >= 0 && nSvc < kIso14229ServiceCount &&
                            nSub != 0xFFFF && nSub < kIso14229Services[nSvc].m_nSub)
                        {
                            omSf.Format("%02X", kIso14229Services[nSvc].m_psSub[nSub].m_bySf);
                            omSubName = kIso14229Services[nSvc].m_psSub[nSub].m_pchName;
                        }
                        CString omLine;
                        omLine.Format("    <SubFunction sf=\"%s\" name=\"%s\" payload=\"%s\"/>\n",
                                      (LPCTSTR)omSf, (LPCTSTR)XmlEscapeAttr(omSubName),
                                      (LPCTSTR)XmlEscapeAttr(GetTreeItemPayload(hChild)));
                        omFile.WriteString(omLine);
                    }
                }
                hChild = m_omTreeServices.GetNextSiblingItem(hChild);
            }
            omFile.WriteString("  </Service>\n");
        }
        hItem = m_omTreeServices.GetNextSiblingItem(hItem);
    }

    omFile.WriteString("</UdsServices>\n");
    omFile.Close();
    AppendLog("Exported XML: " + omDlg.GetPathName());
}

void CUDSMainWnd::OnBnClickedDataBrowse()
{
    CFileDialog omDlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Hex dump (*.hex;*.txt;*.bin)|*.hex;*.txt;*.bin|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CFile omFile;
    if (!omFile.Open(omDlg.GetPathName(), CFile::modeRead | CFile::typeBinary))
    {
        return;
    }
    const UINT nLen = (UINT)omFile.GetLength();
    if (nLen == 0 || nLen > 4096)
    {
        omFile.Close();
        return;
    }
    BYTE abyBuf[4096] = {0};
    omFile.Read(abyBuf, nLen);
    omFile.Close();
    CString omHex;
    for (UINT i = 0; i < nLen; ++i)
    {
        CString omByte;
        omByte.Format("%02X", abyBuf[i]);
        omHex += omByte;
    }
    SetRequestPayload(omHex);
    OnEnChangeData();
}

void CUDSMainWnd::SyncTargetLevelCombo(BYTE bySf)
{
    CString omLvl;
    omLvl.Format("%02X", bySf);
    const int nIdx = m_omComboSecTarget.FindStringExact(-1, omLvl);
    if (nIdx < 0)
    {
        return;
    }
    m_bSyncingSecurityUi = TRUE;
    m_omComboSecTarget.SetCurSel(nIdx);
    m_bSyncingSecurityUi = FALSE;
}

void CUDSMainWnd::ApplySecurityTargetToRequest()
{
    const int nSel = m_omComboSecTarget.GetCurSel();
    if (nSel < 0)
    {
        return;
    }
    CString omLvl;
    m_omComboSecTarget.GetLBText(nSel, omLvl);
    const int nSf = (int)strtol(omLvl, nullptr, 16);
    CString omPayload;
    omPayload.Format("27%02X", nSf);
    SetRequestPayload(omPayload);
    OnEnChangeData();
    m_omListAttribute.DeleteAllItems();
    FillAttribute("SID", "27h", "SecurityAccess");
    CString omSf;
    omSf.Format("%02Xh", nSf);
    FillAttribute("subFunction", omSf, "requestSeed (Target level)");
    FillAttribute("Default payload", omPayload, "synced from Target level");
    FillAttribute("Seed&Key DLL", "Vector", "Unlock sends this requestSeed then auto sendKey");
}

void CUDSMainWnd::OnCbnSelchangeSecTarget()
{
    if (m_bSyncingSecurityUi)
    {
        return;
    }
    ApplySecurityTargetToRequest();
}

void CUDSMainWnd::OnBnClickedUnlock()
{
    CString omLvl;
    const int nSel = m_omComboSecTarget.GetCurSel();
    if (nSel < 0)
    {
        return;
    }
    m_omComboSecTarget.GetLBText(nSel, omLvl);
    const int nSf = (int)strtol(omLvl, nullptr, 16);
    CString omPath;
    m_omEditSeedKeyDll.GetWindowText(omPath);
    if (omPath.IsEmpty())
    {
        AppendLog("27 requestSeed: select a Vector Seed&Key DLL to auto-send the key");
    }
    CString omPayload;
    omPayload.Format("27%02X", nSf);
    SetRequestPayload(omPayload);
    OnEnChangeData();
    OnBnClickedSendUD();
}

void CUDSMainWnd::OnBnClickedSeedKeyBrowse()
{
    CFileDialog omDlg(TRUE, "dll", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Seed&Key DLL (*.dll)|*.dll|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() == IDOK)
    {
        const CString omPath = omDlg.GetPathName();
        m_omEditSeedKeyDll.SetWindowText(omPath);
        CString omError;
        if (m_omSeedKeyDll.Load(omPath, omError))
        {
            AppendLog("Loaded Vector Seed&Key DLL: " + omPath);
        }
        else
        {
            AppendLog("Seed&Key DLL load failed: " + omError);
        }
    }
}

static UINT ParseHexBytes(const CString& omHex, BYTE* pbyOut, UINT unMax)
{
    UINT unCount = 0;
    int nNibble = -1;
    const int nLen = omHex.GetLength();
    for (int i = 0; i < nLen; ++i)
    {
        const TCHAR ch = omHex[i];
        int nVal = -1;
        if (ch >= '0' && ch <= '9')
        {
            nVal = ch - '0';
        }
        else if (ch >= 'A' && ch <= 'F')
        {
            nVal = ch - 'A' + 10;
        }
        else if (ch >= 'a' && ch <= 'f')
        {
            nVal = ch - 'a' + 10;
        }
        if (nVal < 0)
        {
            continue;
        }
        if (nNibble < 0)
        {
            nNibble = nVal;
        }
        else
        {
            if (unCount < unMax)
            {
                pbyOut[unCount++] = (BYTE)((nNibble << 4) | nVal);
            }
            nNibble = -1;
        }
    }
    return unCount;
}

void CUDSMainWnd::TryAutoSendSecurityKeyFromResponse()
{
    if (m_bSendingSecurityKey || omManagerPtr == NULL)
    {
        return;
    }
    BYTE abyUds[4096] = {0};
    const UINT unLen = ParseHexBytes(omManagerPtr->Data_Recibida, abyUds, 4096);
    if (unLen < 2 || abyUds[0] != 0x67)
    {
        return;
    }
    const BYTE bySf = (BYTE)(abyUds[1] & 0x7F);
    if ((bySf & 0x01) == 0)
    {
        CString omCur;
        omCur.Format("%02X", bySf - 1);
        m_omEditSecCurrent.SetWindowText(omCur);
        AppendLog("SecurityAccess unlocked, level=" + omCur);
        return;
    }
    CString omPath;
    m_omEditSeedKeyDll.GetWindowText(omPath);
    if (omPath.IsEmpty())
    {
        AppendLog("Received 67 requestSeed; no Seed&Key DLL selected");
        return;
    }
    CString omError;
    if (!m_omSeedKeyDll.Load(omPath, omError))
    {
        AppendLog("Seed&Key DLL load failed: " + omError);
        return;
    }
    const UINT unSeedLen = unLen - 2;
    if (unSeedLen == 0)
    {
        AppendLog("67 response has empty seed");
        return;
    }
    CString omVariant;
    m_omEditSeedKeyVariant.GetWindowText(omVariant);
    BYTE abyKey[256] = {0};
    UINT unKeyLen = 0;
    if (!m_omSeedKeyDll.GenerateKey(abyUds + 2, unSeedLen, bySf,
                                    (LPCSTR)omVariant, abyKey, 256, unKeyLen, omError))
    {
        AppendLog(omError);
        return;
    }
    CString omPayload;
    omPayload.Format("27%02X", bySf + 1);
    for (UINT i = 0; i < unKeyLen; ++i)
    {
        CString omByte;
        omByte.Format("%02X", abyKey[i]);
        omPayload += omByte;
    }
    CString omLog;
    omLog.Format("GenerateKeyEx level=%02X seed=%u key=%u, sendKey", bySf, unSeedLen, unKeyLen);
    AppendLog(omLog);
    m_bSendingSecurityKey = TRUE;
    SetRequestPayload(omPayload);
    OnEnChangeData();
    OnBnClickedSendUD();
    m_bSendingSecurityKey = FALSE;
}

void CUDSMainWnd::OnBnClickedHexBrowse()
{
    CFileDialog omDlg(TRUE, "hex", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Intel HEX (*.hex)|*.hex|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() == IDOK)
    {
        m_omEditHexPath.SetWindowText(omDlg.GetPathName());
    }
}

void CUDSMainWnd::OnBnClickedHexDownload()
{
    CString omPath;
    m_omEditHexPath.GetWindowText(omPath);
    if (omPath.IsEmpty())
    {
        OnBnClickedHexBrowse();
        m_omEditHexPath.GetWindowText(omPath);
    }
    if (omPath.IsEmpty())
    {
        return;
    }
    CStdioFile omFile;
    if (!omFile.Open(omPath, CFile::modeRead | CFile::typeText))
    {
        AppendLog("Cannot open HEX file");
        return;
    }
    UINT nSize = 0;
    CString omLine;
    while (omFile.ReadString(omLine))
    {
        omLine.Trim();
        if (omLine.GetLength() < 11 || omLine[0] != ':')
        {
            continue;
        }
        const int nRecLen = strtol(omLine.Mid(1, 2), nullptr, 16);
        const int nType = strtol(omLine.Mid(7, 2), nullptr, 16);
        if (nType == 0)
        {
            nSize += (UINT)nRecLen;
        }
    }
    omFile.Close();
    CString omReq;
    omReq.Format("340044%08X%08X", 0, nSize);
    SetRequestPayload(omReq);
    OnEnChangeData();
    CString omSize;
    omSize.Format("%u", nSize);
    AppendLog("RequestDownload from HEX, size=" + omSize);
    OnBnClickedSendUD();
}

void CUDSMainWnd::OnBnClickedLogSave()
{
    CFileDialog omDlg(FALSE, "log", "uds_diag.log", OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                      "Log (*.log;*.txt)|*.log;*.txt|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CString omText;
    m_omEditLog.GetWindowText(omText);
    CStdioFile omFile;
    if (omFile.Open(omDlg.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::typeText))
    {
        omFile.WriteString(omText);
        omFile.Close();
    }
}

void CUDSMainWnd::OnBnClickedLogClear()
{
    m_omEditLog.SetWindowText("");
    Bytes_to_Show = "";
    BytesShown_Line = 1;
    m_abDatas = "";
    m_omDiagService = "";
    SetDlgItemText(IDC_RESPONSE_DATA, "");
    SetDlgItemText(IDC_DIAG_SERVICE, "");
    m_omBytes.vSetValue(0);
    if (omManagerPtr != NULL)
    {
        omManagerPtr->Data_Recibida.Empty();
        omManagerPtr->numberOfBytes = 0;
    }
}
