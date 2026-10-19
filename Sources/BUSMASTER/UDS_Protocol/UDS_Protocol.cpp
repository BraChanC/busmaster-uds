/**
 * \file      UDS_Protocol.cpp
 * \brief     Definition file for CUDS_Protocol class
 * \author    Sanchez Marin Maria Alejandra
 *  Manager of the UDS_Protocol Porject
 */

#pragma once
#include "StdAfx.h"
#define USAGE_EXPORT
#include "UDS_Extern.h"
#include "UDSMainWnd.h"
#include "UDS_NegRespMng.h"
#include "UDSSettingsWnd.h"
#include "UDSFlashWnd.h"
#include "UDSServer.h"
#include "UDSTransfer.h"
#include "UDS_Protocol.h"
#include "CANDriverDefines.h"
#include <stdio.h>

CUDSMainWnd* omMainWnd = NULL;
CUDS_NegRespMng* NegRespManager = NULL;
CUDSSettingsWnd* omSettingsWnd = NULL;
CUDSFlashWnd* omFlashWnd = NULL;
CUDS_Protocol* omManagerPtr = NULL;

/** Variable used to control if a message received should be shown */
bool FDontShow =FALSE;      // if it's TRUE the message should not be shown
// This variable is used to check if the response corresponds to my request

/** It's the number of bytes received in a long response  */
float Length_Received =0;

/**  It contains the number of Data bytes that can be sent in the current message */
int numberOfTaken;

int initialByte;
/** It's used to control the index i according to if it's working on extended or normal addressing  */
int aux_Finterface;
unsigned char abByteArr[64];

/** Corresponds to the STMin received from the flowControlMessage */
int STMin;
// BSize depends of the value put in the settingsWnd.
int BSize;

//; Corresponds to the BSize received from the flowControlMessage
int BSizE;
int SizeFC;
// Time to wait to able the send button again
int P2_Time=250;

int P2_Time_Extended =2000;

int Current_Channel;

/** This variable is used to know if a message has been sent from the UDSMainWnd  */
bool FSending = FALSE;

CString Bytes_to_Show= ("\r\n   1-> ");

/** This variable is used to determinate how many continuos frames will be received */
float TotalFrames;

/** it corresponds to the exact ID of the message received    */
int respID = 0x7E8;
BOOL g_udsCanFd = FALSE;
BOOL g_udsCanFdHw = FALSE;
int g_udsFdDlc = 64;

/** It Corresponds to the value put in the settingWnd if it's extended   */
int RespMsgID;

/** If TRUE the msg should have 8 bytes always  */
bool fMsgSize = FALSE;

/** If TRUE the msg should have 8 bytes always   */
bool FCRespReq = FALSE;


int BytesShown_Line = 1;
int LengthLastFrame;
CString CurrentService;
UINT_PTR m_nTimer;



BEGIN_MESSAGE_MAP(CUDS_Protocol, CWinApp)
END_MESSAGE_MAP()


/**
* CUDS_Protocol Construction
*/
CUDS_Protocol::CUDS_Protocol()
{
    omManagerPtr=this;
    SourceAddress =0;
    TargetAddress =0;
    fInterface = INTERFACE_NORMAL_11;
    MsgID = 0x7E0;
    TargetAddress = 0x7E8;
    numberOfBytes = 1;
    Data_Recibida = "";
    Current_Channel=1 ;
}

// The one and only CUDS_Protocol object
CUDS_Protocol theApp;

/**
* CUDS_Protocol initialization
*/

static HINSTANCE ghLangInst=NULL;

BOOL CUDS_Protocol::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

/**********************************************************************************************************
 Function Name  :   DIL_UDS_ShowWnd

 Description    :   It's called from the MainFrm to show the MainWnd
 Member of      :   CUDS_Protocol

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   01.03.2013
**********************************************************************************************************/
static HRESULT UdsEnsureMainWnd(HWND hParent, int TotalChannels)
{
    if (omMainWnd != NULL && !::IsWindow(omMainWnd->GetSafeHwnd()))
    {
        omMainWnd = NULL;
    }

    if (omMainWnd == NULL)
    {
        CUDSMainWnd* pDlg = new CUDSMainWnd(omManagerPtr->SourceAddress, omManagerPtr->TargetAddress, omManagerPtr->fInterface,omManagerPtr->MsgID, NULL);
        pDlg->TotalChannel = TotalChannels;
        if (!pDlg->Create(IDM_UDS, NULL))
        {
            delete pDlg;
            return E_FAIL;
        }
        if (hParent != NULL)
        {
            ::SetWindowLongPtr(pDlg->GetSafeHwnd(), GWLP_HWNDPARENT, (LONG_PTR)hParent);
        }
        omMainWnd = pDlg;
        if (NegRespManager == NULL)
        {
            NegRespManager = new CUDS_NegRespMng(omManagerPtr);
        }
        /* CFX may have loaded before the window existed — push IDs into UI. */
        if (omManagerPtr != NULL && omManagerPtr->MsgID != 0)
        {
            omMainWnd->fInterface = omManagerPtr->fInterface;
            omMainWnd->SourceAddress = omManagerPtr->SourceAddress;
            omMainWnd->TargetAddress = omManagerPtr->TargetAddress;
            omMainWnd->CanID = omManagerPtr->MsgID;
            omManagerPtr->UpdateFields();
        }
    }
    else
    {
        omMainWnd->fInterface = omManagerPtr->fInterface;
        omMainWnd->SourceAddress = omManagerPtr->SourceAddress;
        omMainWnd->TargetAddress = omManagerPtr->TargetAddress;
        omMainWnd->CanID = omManagerPtr->MsgID;
        omManagerPtr->UpdateFields();
    }

    omMainWnd->SetWindowText("Main Diagnostic Window");
    omMainWnd->ApplyCanFdSwitch();
    return 0;
}

USAGEMODE HRESULT DIL_UDS_ShowWnd(HWND hParent,int TotalChannels)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    HRESULT hr = UdsEnsureMainWnd(hParent, TotalChannels);
    if (hr == 0)
    {
        omMainWnd->ShowWindow(SW_SHOW);
    }
    return hr;
}

USAGEMODE HRESULT DIL_UDS_PrepareWnd(HWND hParent, int TotalChannels)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return UdsEnsureMainWnd(hParent, TotalChannels);
}

USAGEMODE void DIL_UDS_SetCanFdAvailable(BOOL available)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    g_udsCanFdHw = available ? TRUE : FALSE;
    if (!g_udsCanFdHw)
    {
        g_udsCanFd = FALSE;
    }
    if (omMainWnd != NULL && ::IsWindow(omMainWnd->GetSafeHwnd()))
    {
        omMainWnd->ApplyCanFdSwitch();
    }
}

USAGEMODE void DIL_UDS_SetChannelBitrate(int channel, UINT nominalBps, UINT dataBps, BOOL canFd)
{
    UdsClientSetChannelBitrate(channel, nominalBps, dataBps, canFd);
}

USAGEMODE HRESULT DIL_UDS_ShowFlashWnd(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle(hParent);
    if (omFlashWnd == nullptr)
    {
        omFlashWnd = new CUDSFlashWnd(pParent);
        if (!omFlashWnd->Create(IDD_UDS_FLASH, pParent))
        {
            delete omFlashWnd;
            omFlashWnd = nullptr;
            return E_FAIL;
        }
    }
    omFlashWnd->ShowWindow(SW_SHOW);
    omFlashWnd->SetForegroundWindow();
    return 0;
}

USAGEMODE HRESULT DIL_UDS_ShowServerWnd(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return UdsServer_Show(hParent);
}

USAGEMODE HRESULT DIL_UDS_ShowTransferWnd(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return UdsTransfer_Show(hParent);
}
/**********************************************************************************************************
 Function Name  :   UpdateChannelUDS

 Description    :   It's called from the MainFrm to update the channel count in the MainWnd
 Member of      :   CUDS_Protocol

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   01.06.2013
**********************************************************************************************************/

USAGEMODE HRESULT UpdateChannelUDS(HWND hParent)
{

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (omMainWnd != NULL)
    {
        omMainWnd->vUpdateChannelIDInfo();

    }
    //objParent.Detach();
    return 0;
}

/**********************************************************************************************************
 Function Name  :   Show_ResponseData

 Input(s)       :   psMsg[] contains the bytes to be shown, Datalen is the position inside of psMsg[]
                    that contains the last byte to showt, posFirstByte is the location inside of psMsg[]
                    where this function should start to read.
 Output         :    -
 Description    :   This function is called by EvaluateMessage to show in the Response Data section
                    the bytes received in the bus as response of a request sent from the UDS tool.
 Member of      :   CUDS_Protocol

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   19.06.2013
/**********************************************************************************************************/

static BYTE* g_capBuf = NULL;
static int g_capLen = 0;
static int g_capCap = 0;
static volatile LONG g_capSeq = 0;
HWND volatile g_hUdsRspNotify = NULL;
static CRITICAL_SECTION g_capCs;
static BOOL g_capCsReady = FALSE;

struct UdsCapRecent
{
    DWORD hash;
    DWORD tick;
    int len;
    BYTE head[8];
};

static UdsCapRecent g_capRecent[8];
static int g_capRecentN = 0;

static void EnsureCapCs()
{
    if (!g_capCsReady)
    {
        InitializeCriticalSection(&g_capCs);
        g_capCsReady = TRUE;
    }
}

static DWORD CapHash(const BYTE* data, int n)
{
    DWORD h = (DWORD)n;
    for (int i = 0; i < n; i++)
    {
        h = (h * 16777619u) ^ data[i];
    }
    return h;
}

static BOOL CapSeenLocked(const BYTE* data, int n)
{
    if (data == NULL || n <= 0)
    {
        return FALSE;
    }
    DWORD now = GetTickCount();
    DWORD hash = CapHash(data, n);
    int head = (n < 8) ? n : 8;
    int count = (g_capRecentN < 8) ? g_capRecentN : 8;
    for (int i = 0; i < count; i++)
    {
        if (g_capRecent[i].len == n && g_capRecent[i].hash == hash
                && (now - g_capRecent[i].tick) < 8000
                && memcmp(g_capRecent[i].head, data, head) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void CapRememberLocked(const BYTE* data, int n)
{
    if (data == NULL || n <= 0)
    {
        return;
    }
    UdsCapRecent& slot = g_capRecent[g_capRecentN & 7];
    slot.hash = CapHash(data, n);
    slot.tick = GetTickCount();
    slot.len = n;
    int head = (n < 8) ? n : 8;
    memset(slot.head, 0, sizeof(slot.head));
    memcpy(slot.head, data, head);
    g_capRecentN++;
}

static void StoreCapLocked(const BYTE* data, int n, BOOL /*remember*/)
{
    if (n < 0)
    {
        n = 0;
    }
    if (n > 0x100000)
    {
        n = 0x100000;
    }
    /* Dedupe both client Capture and server Publish. Otherwise the same
       multi-frame 76 is stored twice (Capture then NoteTxDone Publish),
       Transfer sees a fake second response and aborts before 36 02. */
    if (CapSeenLocked(data, n))
    {
        return;
    }
    if (g_capCap < n)
    {
        delete[] g_capBuf;
        g_capCap = (n < 256) ? 256 : n;
        g_capBuf = new BYTE[g_capCap];
    }
    if (n > 0 && data != NULL && g_capBuf != NULL)
    {
        memcpy(g_capBuf, data, n);
    }
    g_capLen = n;
    CapRememberLocked(data, n);
    InterlockedIncrement(&g_capSeq);
    HWND hNotify = g_hUdsRspNotify;
    if (hNotify != NULL)
    {
        ::PostMessage(hNotify, WM_UDS_RSP_READY, 0, 0);
    }
}

static void CaptureUdsResponse(const BYTE* data, int n)
{
    EnsureCapCs();
    EnterCriticalSection(&g_capCs);
    StoreCapLocked(data, n, FALSE);
    LeaveCriticalSection(&g_capCs);
}

void UdsPublishServerResponse(const BYTE* data, int n)
{
    if (data == NULL || n <= 0)
    {
        return;
    }
    EnsureCapCs();
    EnterCriticalSection(&g_capCs);
    StoreCapLocked(data, n, TRUE);
    LeaveCriticalSection(&g_capCs);
}

USAGEMODE LONG DIL_UDS_ResponseSeq()
{
    return g_capSeq;
}

USAGEMODE int DIL_UDS_CopyResponse(BYTE* out, int cap)
{
    EnsureCapCs();
    EnterCriticalSection(&g_capCs);
    int n = g_capLen;
    if (n < 0)
    {
        n = 0;
    }
    int copy = n;
    if (copy > cap)
    {
        copy = cap;
    }
    if (copy > 0 && out != NULL && g_capBuf != NULL)
    {
        memcpy(out, g_capBuf, copy);
    }
    LeaveCriticalSection(&g_capCs);
    return n;
}

void CUDS_Protocol::Show_ResponseData(unsigned char psMsg[], unsigned char Datalen , int posFirstByte )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    int loc = Datalen;
    char hex [9];
    if(omMainWnd!=NULL )
    {

        int intResp;
        for (int ByteCounter = posFirstByte+1; ByteCounter<loc ; ByteCounter++)
        {
            UCHAR TempByte = (psMsg[ByteCounter]) ;
            int Temp_char2Int =  TempByte;
            intResp = sprintf (hex,"%02X ",Temp_char2Int);
            Bytes_to_Show = Bytes_to_Show + hex;
            BytesShown_Line++;
            numberOfBytes++;
            Data_Recibida = Data_Recibida + hex;

            if (BytesShown_Line == NUM_BYTES_SHOWN_RESP+1)      // It enters here when it has been shown a complete line with NUM_BYTES_SHOWN_RESP+1
            {
                CString CstringNumberOfBytes;                       // elements  in the response data section
                CstringNumberOfBytes.Format("%d", numberOfBytes);
                int i =4;                                               // Prepares the number of Bytes to have all the data aligned in the Response
                while(i- CstringNumberOfBytes.GetLength()>0)            // data section del Main Window
                {
                    CstringNumberOfBytes = " " + CstringNumberOfBytes ;
                }
                BytesShown_Line = 1;
                //intResp = sprintf (hex,"%03d-> ",numberOfBytes);
                Bytes_to_Show= Bytes_to_Show + ("\r\n") +CstringNumberOfBytes +" -> ";

            }
        }
        omMainWnd->m_abDatas = Bytes_to_Show + '\0';

        omMainWnd->UpdateData(false);
        int pay = loc - (posFirstByte + 1);
        if (pay < 0)
        {
            pay = 0;
        }
        CaptureUdsResponse((pay > 0) ? (psMsg + posFirstByte + 1) : NULL, pay);
    }
}




/**********************************************************************************************************
 Function Name  :   EvaluateMessage

 Description    :   Evaluate any message received from the ECU -Filter the message received and call
                    initialize it the variables to show it
 Member of      :

 Author(s)      :   Sanchez Marin Maria Alejandra

 Date Created   :   19.06.2013
**********************************************************************************************************/

struct UdsRxNode
{
    STCAN_MSG msg;
    UdsRxNode* next;
};

static CRITICAL_SECTION g_udsRxCs;
static volatile LONG g_udsRxInit = 0;
static UdsRxNode* g_udsRxHead = NULL;
static UdsRxNode* g_udsRxTail = NULL;
static volatile LONG g_udsRxWake = 0;

static BYTE* g_longBuf = NULL;
static int g_longCap = 0;
static int g_longGot = 0;
static int g_longTotal = 0;
static int g_longPub = 0;
static CRITICAL_SECTION g_longCs;
static volatile LONG g_longCsInit = 0;
static CString g_longSvcPending;
/* Next expected CF sequence number (ISO 15765-2: FF=0, first CF=1, wraps 0..F). */
static int g_longExpSn = 1;
static int g_longCfCount = 0;
/* Set when the receiver must abort (N_WRONG_SN). Read once by Transfer. */
static char g_longErr[200];

static void EnsureLongCs()
{
    if (g_longCsInit == 2)
    {
        return;
    }
    if (InterlockedCompareExchange(&g_longCsInit, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_longCs);
        g_longCsInit = 2;
    }
    else
    {
        while (g_longCsInit != 2)
        {
            Sleep(0);
        }
    }
}

/* TRUE while client ISO-TP is still assembling a multi-frame response. */
BOOL UdsClientLongRxPending()
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    BOOL pending = (g_longTotal > 0 && g_longGot < g_longTotal);
    LeaveCriticalSection(&g_longCs);
    return pending;
}

int UdsClientLongRxGot()
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    int got = g_longGot;
    LeaveCriticalSection(&g_longCs);
    return got;
}

int UdsClientLongRxTotal()
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    int total = g_longTotal;
    LeaveCriticalSection(&g_longCs);
    return total;
}

/* Copy assembled bytes for UI thread. Returns got; total out-param. */
int UdsClientLongRxCopy(BYTE* out, int cap, int* total, BOOL* done)
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    int got = g_longGot;
    int tot = g_longTotal;
    BOOL fin = (tot > 0 && got >= tot);
    int n = got;
    if (n > cap)
    {
        n = cap;
    }
    if (n > 0 && out != NULL && g_longBuf != NULL)
    {
        memcpy(out, g_longBuf, n);
    }
    LeaveCriticalSection(&g_longCs);
    if (total != NULL)
    {
        *total = tot;
    }
    if (done != NULL)
    {
        *done = fin;
    }
    return got;
}

/* Returns TRUE once after the client aborted a multi-frame receive. */
BOOL UdsClientTakeRxError(CString& text)
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    BOOL has = (g_longErr[0] != 0);
    if (has)
    {
        text = g_longErr;
        g_longErr[0] = 0;
    }
    LeaveCriticalSection(&g_longCs);
    return has;
}

/* ISO 15765-2 N_Cr: the receiver gives up when the next CF does not arrive. */
void UdsClientAbortLongRx()
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    if (g_longTotal > 0)
    {
        g_longTotal = 0;
        g_longGot = 0;
        TotalFrames = 0;
        FSending = FALSE;
    }
    LeaveCriticalSection(&g_longCs);
}

CString UdsClientLongRxTakeSvc()
{
    EnsureLongCs();
    EnterCriticalSection(&g_longCs);
    CString svc = g_longSvcPending;
    g_longSvcPending.Empty();
    LeaveCriticalSection(&g_longCs);
    return svc;
}

static void PostLongRxUi(BOOL done, const CString* svc)
{
    CUDSMainWnd* wnd = omMainWnd;
    if (wnd == NULL || !::IsWindow(wnd->GetSafeHwnd()))
    {
        return;
    }
    if (svc != NULL)
    {
        EnsureLongCs();
        EnterCriticalSection(&g_longCs);
        g_longSvcPending = *svc;
        LeaveCriticalSection(&g_longCs);
    }
    /* wParam: bit0=done, bit1=has service text */
    WPARAM wp = 0;
    if (done)
    {
        wp |= 1;
    }
    if (svc != NULL)
    {
        wp |= 2;
    }
    wnd->PostMessage(WM_UDS_LONG_RX_UI, wp, 0);
}

static void EnsureUdsRxCs()
{
    if (g_udsRxInit == 2)
    {
        return;
    }
    if (InterlockedCompareExchange(&g_udsRxInit, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_udsRxCs);
        g_udsRxInit = 2;
    }
    else
    {
        while (g_udsRxInit != 2)
        {
            Sleep(0);
        }
    }
}

static void WakeUdsRx()
{
    CUDSMainWnd* wnd = omMainWnd;
    if (wnd == NULL || !::IsWindow(wnd->GetSafeHwnd()))
    {
        return;
    }
    wnd->PostMessage(WM_UDS_EVALUATE_MSG, 1, 0);
}

void UdsEnqueueRx(const STCAN_MSG& msg)
{
    EnsureUdsRxCs();
    UdsRxNode* node = new UdsRxNode;
    node->msg = msg;
    node->next = NULL;
    EnterCriticalSection(&g_udsRxCs);
    BOOL wasEmpty = (g_udsRxHead == NULL);
    if (g_udsRxTail != NULL)
    {
        g_udsRxTail->next = node;
    }
    else
    {
        g_udsRxHead = node;
    }
    g_udsRxTail = node;
    LeaveCriticalSection(&g_udsRxCs);
    if (wasEmpty && InterlockedCompareExchange(&g_udsRxWake, 1, 0) == 0)
    {
        WakeUdsRx();
    }
}

static BOOL PopUdsRx(STCAN_MSG& msg)
{
    EnsureUdsRxCs();
    EnterCriticalSection(&g_udsRxCs);
    UdsRxNode* node = g_udsRxHead;
    if (node != NULL)
    {
        g_udsRxHead = node->next;
        if (g_udsRxHead == NULL)
        {
            g_udsRxTail = NULL;
        }
    }
    LeaveCriticalSection(&g_udsRxCs);
    if (node == NULL)
    {
        return FALSE;
    }
    msg = node->msg;
    delete node;
    return TRUE;
}

void UdsDrainRx()
{
    for (int n = 0; n < 256; n++)
    {
        STCAN_MSG msg;
        if (!PopUdsRx(msg))
        {
            break;
        }
        EvaluateMessage(msg);
    }
    EnsureUdsRxCs();
    EnterCriticalSection(&g_udsRxCs);
    BOOL more = (g_udsRxHead != NULL);
    LeaveCriticalSection(&g_udsRxCs);
    if (more)
    {
        WakeUdsRx();
        return;
    }
    InterlockedExchange(&g_udsRxWake, 0);
    EnterCriticalSection(&g_udsRxCs);
    more = (g_udsRxHead != NULL);
    LeaveCriticalSection(&g_udsRxCs);
    if (more && InterlockedCompareExchange(&g_udsRxWake, 1, 0) == 0)
    {
        WakeUdsRx();
    }
}

void UdsFlushRx()
{
    if (g_udsRxInit != 2)
    {
        return;
    }
    EnterCriticalSection(&g_udsRxCs);
    UdsRxNode* node = g_udsRxHead;
    g_udsRxHead = NULL;
    g_udsRxTail = NULL;
    LeaveCriticalSection(&g_udsRxCs);
    while (node != NULL)
    {
        UdsRxNode* next = node->next;
        delete node;
        node = next;
    }
    InterlockedExchange(&g_udsRxWake, 0);
}

static void BeginLongResponse(int total)
{
    if (total < 1)
    {
        total = 1;
    }
    if (total > 16 * 1024 * 1024)
    {
        total = 16 * 1024 * 1024;
    }
    if (g_longCap < total)
    {
        delete[] g_longBuf;
        g_longBuf = new BYTE[total];
        g_longCap = (g_longBuf != NULL) ? total : 0;
    }
    g_longGot = 0;
    g_longTotal = total;
    g_longPub = 0;
    g_longExpSn = 1;
    g_longCfCount = 0;
    g_longErr[0] = 0;
}

static void AppendLongResponse(const BYTE* data, int n)
{
    if (g_longBuf == NULL || data == NULL || n <= 0 || g_longGot >= g_longTotal)
    {
        return;
    }
    if (g_longGot + n > g_longTotal)
    {
        n = g_longTotal - g_longGot;
    }
    memcpy(g_longBuf + g_longGot, data, n);
    g_longGot += n;
}

static void PublishLongResponse(BOOL done, const CString* svc)
{
    /* Capture first — Transfer waits on g_capSeq. Must run on the read
       thread as soon as the PDU is complete (not after UI drain). */
    if (done && g_longBuf != NULL && g_longGot > 0)
    {
        CaptureUdsResponse(g_longBuf, g_longGot);
        if (omManagerPtr != NULL)
        {
            CString hex;
            int n = g_longGot;
            if (n > 64)
            {
                n = 64;
            }
            for (int i = 0; i < n; i++)
            {
                CString one;
                one.Format("%02X ", g_longBuf[i]);
                hex += one;
            }
            omManagerPtr->Data_Recibida = hex;
            omManagerPtr->numberOfBytes = g_longGot;
        }
    }
    /* Never SetWindowText here — it re-enters the pump and was the old
       reason CFs visible in Message Window never reached g_longGot. */
    if (!done)
    {
        if (g_longPub != 0 && (g_longGot - g_longPub) < 4096 && svc == NULL)
        {
            return;
        }
        g_longPub = g_longGot;
    }
    else
    {
        g_longPub = g_longGot;
    }
    PostLongRxUi(done, svc);
}

/* ISO-TP client assembly. Called from the CAN read thread for every RX that
   also reaches Message Window — must NOT defer assembly to the UI thread. */
static void UdsClientHandleCan(const STCAN_MSG& Mensaje)
{
    CUDSMainWnd* pMainWnd = omMainWnd;
    if (pMainWnd == NULL || !::IsWindow(pMainWnd->GetSafeHwnd()) || omManagerPtr == NULL)
    {
        return;
    }

    unsigned char psMsg[64];
    memcpy(psMsg, Mensaje.m_ucData, 64);
    unsigned char Datalen = Mensaje.m_ucDataLen;
    UINT MessageID = Mensaje.m_unMsgID;
    UINT Channel = Mensaje.m_ucChannel;

    /* Do not read MFC edits from the CAN read thread — use cached globals. */
    UINT Check_SourceAddress = (UINT)(omManagerPtr->SourceAddress << 8);
    UINT Check_TargetAddress = (UINT)omManagerPtr->TargetAddress;
    UINT Check_MSG_received = respID + Check_TargetAddress + Check_SourceAddress;

    BOOL idMatch = (respID == MessageID || Check_MSG_received == MessageID);
    if (!idMatch)
    {
        return;
    }
    if (!FSending || Current_Channel != Channel)
    {
        return;
    }

    EnsureLongCs();
    EnterCriticalSection(&g_longCs);

    TYPE_OF_FRAME TypeofFrame = omManagerPtr->getFrameType(psMsg[initialByte]);
    BOOL longPending = (g_longTotal > 0 && g_longGot < g_longTotal);

    if (FWaitFlow == TRUE && TypeofFrame == FLOW_CONTROL)
    {
        LeaveCriticalSection(&g_longCs);
        switch (omManagerPtr->TypeofControl)
        {
            case 0:
            {
                BSizE = psMsg[initialByte + 1];
                if (psMsg[initialByte + 2] <= 0x7F)
                {
                    STMin = psMsg[initialByte + 2];
                }
                else if (psMsg[initialByte + 2] >= 0xF1 && psMsg[initialByte + 2] <= 0xF9)
                {
                    STMin = 1;
                }
                else
                {
                    STMin = 127;
                }
                FWaitFlow = FALSE;
                UdsClientTxOnFc(BSizE, psMsg[initialByte + 2]);
            }
            break;
            case 1:
                pMainWnd->PostMessage(WM_UDS_LONG_RX_UI, 4, 0); /* restart P2 timer */
                break;
            default:
                FWaitFlow = FALSE;
                break;
        }
        return;
    }
    else if (TypeofFrame == LONG_RESPONSE)
    {
        int TempValue = strtol(CurrentService, NULL, 16);
        int ffData = 6 - aux_Finterface;
        int sidPos = initialByte + 2;
        if (psMsg[initialByte] == 0x10 && psMsg[initialByte + 1] == 0x00
                && Datalen >= (unsigned char)(initialByte + 7))
        {
            unsigned int len32 = ((unsigned int)psMsg[initialByte + 2] << 24)
                                 | ((unsigned int)psMsg[initialByte + 3] << 16)
                                 | ((unsigned int)psMsg[initialByte + 4] << 8)
                                 | (unsigned int)psMsg[initialByte + 5];
            Length_Received = (float)len32;
            ffData = (int)Datalen - 6 - aux_Finterface;
            if (ffData < 1)
            {
                ffData = 1;
            }
            sidPos = initialByte + 6;
        }
        else
        {
            Length_Received = (float)(((psMsg[initialByte] & 0x0F) << 8) | psMsg[initialByte + 1]);
            ffData = (Datalen > 8) ? ((int)Datalen - 2 - aux_Finterface) : (6 - aux_Finterface);
        }
        if (sidPos < Datalen && psMsg[sidPos] == (unsigned char)(TempValue + 0x40))
        {
            omManagerPtr->numberOfBytes = 1;
            int cfSize = (Datalen > 8) ? ((int)Datalen - 1 - aux_Finterface) : (7 - aux_Finterface);
            if (cfSize < 1)
            {
                cfSize = 1;
            }
            int remain = (int)Length_Received - ffData;
            if (remain < 0)
            {
                remain = 0;
            }
            LengthLastFrame = (cfSize > 0) ? (remain % cfSize) : 0;
            int nCf = 0;
            if (remain > 0 && cfSize > 0)
            {
                nCf = (remain + cfSize - 1) / cfSize;
            }
            TotalFrames = (float)nCf;
            BeginLongResponse((int)Length_Received);
            if (Datalen > sidPos)
            {
                AppendLongResponse(psMsg + sidPos, Datalen - sidPos);
            }
            LeaveCriticalSection(&g_longCs);
            /* FC goes out on the UI thread: psTxCanMsgUds is the shared TX
               buffer that the UI thread also uses for requests. */
            pMainWnd->PostMessage(WM_UDS_SEND_FC, 0, 0);
            CString svc;
            if (NegRespManager != NULL)
            {
                svc = NegRespManager->evaluateResp(psMsg, sidPos - 1);
            }
            EnsureLongCs();
            EnterCriticalSection(&g_longCs);
            PublishLongResponse(FALSE, &svc);
            LeaveCriticalSection(&g_longCs);
            return;
        }
        LeaveCriticalSection(&g_longCs);
        return;
    }
    else if (TypeofFrame == CONSECUTIVE_FRAME)
    {
        if (!longPending)
        {
            LeaveCriticalSection(&g_longCs);
            return;
        }
        int sn = psMsg[initialByte] & 0x0F;
        if (sn != g_longExpSn)
        {
            /* ISO 15765-2 N_WRONG_SN: a CF is missing or duplicated. Abort
               instead of waiting forever for bytes that will never come. */
            _snprintf(g_longErr, sizeof(g_longErr),
                      "ISO-TP CF sequence error: expected SN %X, got %X after %d CF (%d / %d bytes)",
                      g_longExpSn, sn, g_longCfCount, g_longGot, g_longTotal);
            g_longErr[sizeof(g_longErr) - 1] = 0;
            g_longTotal = 0;
            g_longGot = 0;
            TotalFrames = 0;
            FSending = FALSE;
            LeaveCriticalSection(&g_longCs);
            pMainWnd->PostMessage(WM_UDS_LONG_RX_UI, 8, 0);
            return;
        }
        g_longExpSn = (g_longExpSn + 1) & 0x0F;
        g_longCfCount++;
        TotalFrames--;
        if (TotalFrames == 0)
        {
            FSending = FALSE;
        }
        else if (TotalFrames < 0)
        {
            Datalen = (unsigned char)(LengthLastFrame + 1 + aux_Finterface);
            FSending = FALSE;
        }
        if (Datalen > (unsigned char)(initialByte + 1))
        {
            AppendLongResponse(psMsg + initialByte + 1, Datalen - (initialByte + 1));
        }
        BOOL done = (TotalFrames <= 0 || (g_longTotal > 0 && g_longGot >= g_longTotal));
        if (g_longTotal > 0 && g_longGot >= g_longTotal)
        {
            TotalFrames = 0;
            FSending = FALSE;
        }
        PublishLongResponse(done, NULL);
        BOOL needFc = FALSE;
        if (BSize > 0)
        {
            Counter_BSize--;
            if (Counter_BSize == 0)
            {
                needFc = TRUE;
            }
        }
        LeaveCriticalSection(&g_longCs);
        if (needFc)
        {
            pMainWnd->PostMessage(WM_UDS_SEND_FC, 0, 0);
        }
        return;
    }
    else
    {
        /* While assembling a multi-frame response, ignore non-CF/FF frames
           (including stray FC) so FSending is not cleared at 6/4095. */
        if (longPending)
        {
            LeaveCriticalSection(&g_longCs);
            return;
        }
        omManagerPtr->numberOfBytes = 1;
        FSending = FALSE;
        int sfPci = initialByte;
        int sfLen = psMsg[initialByte];
        if (Datalen > 8 && psMsg[initialByte] == 0x00)
        {
            sfPci = initialByte + 1;
            sfLen = psMsg[initialByte + 1];
            Length_Received = (float)sfLen;
        }
        LeaveCriticalSection(&g_longCs);
        if (!FDontShow && GetWindowThreadProcessId(pMainWnd->GetSafeHwnd(), NULL) == GetCurrentThreadId())
        {
            pMainWnd->m_omDiagService = NegRespManager->evaluateResp(psMsg, sfPci);
            Length_Received, omManagerPtr->Show_ResponseData(psMsg, (unsigned char)(sfLen + sfPci + 1), sfPci);
            pMainWnd->TryAutoSendSecurityKeyFromResponse();
            pMainWnd->m_omBytes.vSetValue(Length_Received);
        }
        else if (!FDontShow)
        {
            CString svc = NegRespManager->evaluateResp(psMsg, sfPci);
            /* Simple response still Captures via Show_ResponseData on UI. */
            CaptureUdsResponse(psMsg + sfPci + 1, sfLen);
            PostLongRxUi(TRUE, &svc);
        }
        FDontShow = FALSE;
        return;
    }
}

USAGEMODE HRESULT EvaluateMessage( STCAN_MSG  Mensaje  )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CUDSMainWnd* pMainWnd = omMainWnd;
    BOOL bClientUi = (pMainWnd != NULL && ::IsWindow(pMainWnd->GetSafeHwnd()));
    /* Every RX reaches the server engine on this thread. It has its own lock;
       gating it on the client window thread dropped all server RX whenever
       the diagnostic window was open. */
    UdsServer_OnCan(Mensaje);
    if (!bClientUi)
    {
        return 0;
    }

    /* Assemble on this thread. Message Window and EvaluateMessage share the
       PSDI read path — deferring CF assembly to the UI queue left the diag
       window at "Receiving 6 / 4095" while the bus log already had all CFs. */
    UdsClientHandleCan(Mensaje);
    return 0;
}
/**********************************************************************************************************
 Function Name  :   getFrameType

 Description    :   It returns the type of frame that was received
 Member of      :   CUDS_Protocol

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   19.06.2013
**********************************************************************************************************/

TYPE_OF_FRAME CUDS_Protocol::getFrameType(BYTE FirstByte)
{
    BYTE Mask_FB = FirstByte&0xF0;
    int TempByte = FirstByte+(0x30);

    if (0x60 <= TempByte && TempByte< 0x62)         //it supports all cases of Flow Control
    {
        TypeofControl = FirstByte& 0x0F;
        return FLOW_CONTROL;
    }
    else if(Mask_FB == 0x10)
    {
        Length_Received = (FirstByte&0x0F)<<8;      //long response
        return  LONG_RESPONSE;
    }
    else if(Mask_FB == 0x20)
    {
        return CONSECUTIVE_FRAME;                   //If it's a consecutive frame the Length_Received doesn't change.
    }
    Length_Received = FirstByte;
    return SIMPLE_RESPONSE;
}

/**********************************************************************************************************
 Function Name  :   DIL_UDS_ShowSettingWnd

 Description    :   It's called from the MainFrm to show the settings Wnd
 Member of      :   CUDS_Protocol

 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   01.03.2013
**********************************************************************************************************/

USAGEMODE HRESULT DIL_UDS_ShowSettingWnd(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle(hParent);

    if (omSettingsWnd == NULL)
    {
        omSettingsWnd = new CUDSSettingsWnd(pParent, omManagerPtr);
        if (!omSettingsWnd->Create(IDD_SETTINGS_UDS, pParent))
        {
            delete omSettingsWnd;
            omSettingsWnd = NULL;
            return E_FAIL;
        }
    }
    omSettingsWnd->SyncFromProtocol();
    omSettingsWnd->ShowWindow(SW_SHOW);
    omSettingsWnd->SetForegroundWindow();
    return 0;
}

USAGEMODE void DIL_UDS_GetSettings(SUDS_SETTINGS_CFX* pSettings)
{
    if (pSettings == NULL)
    {
        return;
    }
    memset(pSettings, 0, sizeof(SUDS_SETTINGS_CFX));
    if (omManagerPtr != NULL)
    {
        pSettings->nInterface = (int)omManagerPtr->fInterface;
        pSettings->nDiagnostics = (int)omManagerPtr->fDiagnostics;
        pSettings->unMsgId = omManagerPtr->MsgID;
        pSettings->unSourceAddr = omManagerPtr->SourceAddress;
        pSettings->unTargetAddr = omManagerPtr->TargetAddress;
    }
    pSettings->nRespId = respID;
    pSettings->nRespMsgId = RespMsgID;
    pSettings->nStMin = SSTMin & 0xFF;
    pSettings->nBlockSize = BSize & 0xFF;
    pSettings->nP2 = P2_Time;
    pSettings->nP2Extended = P2_Time_Extended;
    pSettings->nS3Client = S3_Client;
    pSettings->nS3Server = S3_Server;
    pSettings->nSizeFC = SizeFC;
    pSettings->nMsgSizeAlways8 = fMsgSize ? 1 : 0;
    pSettings->nFcNoRespReq = FCRespReq ? 1 : 0;
}

USAGEMODE void DIL_UDS_SetSettings(const SUDS_SETTINGS_CFX* pSettings)
{
    if (pSettings == NULL)
    {
        return;
    }
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    SSTMin = pSettings->nStMin & 0xFF;
    BSize = pSettings->nBlockSize & 0xFF;
    P2_Time = (pSettings->nP2 > 0) ? pSettings->nP2 : 250;
    P2_Time_Extended = (pSettings->nP2Extended > 0) ? pSettings->nP2Extended : 2000;
    S3_Client = (pSettings->nS3Client > 0) ? pSettings->nS3Client : 2000;
    S3_Server = (pSettings->nS3Server > 0) ? pSettings->nS3Server : 5000;
    if (pSettings->nSizeFC == 8)
    {
        SizeFC = 8;
    }
    else if (pSettings->nSizeFC >= 3)
    {
        SizeFC = pSettings->nSizeFC;
    }
    else
    {
        SizeFC = 8;
    }
    fMsgSize = (pSettings->nMsgSizeAlways8 != 0);
    FCRespReq = (pSettings->nFcNoRespReq != 0);
    if (pSettings->nRespId != 0)
    {
        respID = pSettings->nRespId;
    }
    RespMsgID = pSettings->nRespMsgId;

    UDS_INTERFACE eIface = (UDS_INTERFACE)pSettings->nInterface;
    switch (eIface)
    {
        case INTERFACE_EXTENDED_11:
            numberOfTaken = 10;
            initialByte = 1;
            aux_Finterface = 1;
            break;
        default:
            numberOfTaken = 12;
            initialByte = 0;
            aux_Finterface = 0;
            break;
    }

    if (omManagerPtr != NULL)
    {
        omManagerPtr->fInterface = eIface;
        omManagerPtr->fDiagnostics = (DIAGNOSTIC_STANDARD)pSettings->nDiagnostics;
        /* Do not clear a working MsgID with a missing/zero CFX field. */
        if (pSettings->unMsgId != 0)
        {
            omManagerPtr->MsgID = pSettings->unMsgId;
        }
        if (pSettings->unSourceAddr != 0 || pSettings->unMsgId != 0)
        {
            omManagerPtr->SourceAddress = (pSettings->unSourceAddr != 0)
                                         ? pSettings->unSourceAddr
                                         : pSettings->unMsgId;
        }
        if (pSettings->unTargetAddr != 0 || pSettings->nRespId != 0)
        {
            omManagerPtr->TargetAddress = (pSettings->unTargetAddr != 0)
                                         ? pSettings->unTargetAddr
                                         : (UINT)pSettings->nRespId;
        }
        if (omMainWnd != NULL && ::IsWindow(omMainWnd->GetSafeHwnd()))
        {
            omMainWnd->fInterface = omManagerPtr->fInterface;
            omMainWnd->SourceAddress = omManagerPtr->SourceAddress;
            omMainWnd->TargetAddress = omManagerPtr->TargetAddress;
            omMainWnd->CanID = omManagerPtr->MsgID;
        }
        omManagerPtr->UpdateFields();
    }
    if (omSettingsWnd != NULL && ::IsWindow(omSettingsWnd->GetSafeHwnd()))
    {
        omSettingsWnd->SyncFromProtocol();
    }
}

USAGEMODE void DIL_UDS_GetIsoTpParams(int* stMin, int* blockSize)
{
    SUDS_SETTINGS_CFX s;
    DIL_UDS_GetSettings(&s);
    if (stMin != NULL)
    {
        *stMin = s.nStMin;
    }
    if (blockSize != NULL)
    {
        *blockSize = s.nBlockSize;
    }
}

USAGEMODE void DIL_UDS_SetIsoTpParams(int stMin, int blockSize)
{
    SUDS_SETTINGS_CFX s;
    DIL_UDS_GetSettings(&s);
    s.nStMin = stMin;
    s.nBlockSize = blockSize;
    DIL_UDS_SetSettings(&s);
}

//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

USAGEMODE HRESULT TX_vSetDILInterfacePtrUDS(void* ptrDILIntrf)
{
    CUDSMainWnd::s_podGetUdsMsgManager()->vSetDILInterfacePtr(ptrDILIntrf);
    //    m_spodInstance = CUDSMainWnd::s_podGetUdsMsgManager();//->vSetDILInterfacePtr(ptrDILIntrf);
    return S_OK;
}

/**********************************************************************************************************
 Function Name  :   UpdateFields
 Description    :   This function is called from the SettingsWnd to save all the values tha were settled
                    in the settings Window. This function also sets some values in the MainWnd such as
                    SA, TA,CANId
 Member of      :   CUDS_Protocol
 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   01.03.2013
**********************************************************************************************************/

void CUDS_Protocol::UpdateFields()
{
    if(omMainWnd!=NULL)
    {
        omMainWnd->fInterface =fInterface;
        switch (fInterface)
        {
            case INTERFACE_NORMAL_11:
            {
                omMainWnd->CanID = MsgID;
                omMainWnd->m_omTargetAddress.vSetValue(TargetAddress);
                omMainWnd->m_omSourceAddress.vSetValue(SourceAddress);
                omMainWnd->m_omSourceAddress.SetReadOnly(FALSE);
                omMainWnd->m_omTargetAddress.SetReadOnly(FALSE);
                omMainWnd->m_omCanID.SetReadOnly(FALSE);
                omMainWnd->m_omCanID.vSetValue(MsgID);
            }
            break;
            case INTERFACE_EXTENDED_11:
            {
                omMainWnd->CanID =MsgID;
                omMainWnd->m_omSourceAddress.SetReadOnly(FALSE);
                omMainWnd->m_omTargetAddress.SetReadOnly(FALSE);
                omMainWnd->m_omSourceAddress.SetLimitText(2);
                omMainWnd->m_omTargetAddress.SetLimitText(2);
                if(MsgID ==0)
                {
                    omMainWnd->m_omSourceAddress.vSetValue(0);
                    omMainWnd->m_omTargetAddress.vSetValue(0);
                }
                SourceAddress = omMainWnd->m_omSourceAddress.lGetValue();
                TargetAddress = omMainWnd->m_omTargetAddress.lGetValue();

                int MsgID2 =    SourceAddress + MsgID;       // This is necessary to set the proper CAN ID when the user has recently select the
                omMainWnd->m_omCanID.vSetValue(MsgID2);      // the option Extended addressing in the Interface Combo Box.
                respID = TargetAddress + RespMsgID;
            }
            break;
            case INTERFACE_NORMAL_ISO_29:
            {
                omMainWnd->CanID = MsgID;
                omMainWnd->m_omSourceAddress.SetLimitText(3);
                omMainWnd->m_omTargetAddress.SetLimitText(3);
                omMainWnd->m_omSourceAddress.vSetValue(SourceAddress);
                omMainWnd->m_omTargetAddress.vSetValue(TargetAddress);
                omMainWnd->m_omSourceAddress.SetReadOnly(false);
                omMainWnd->m_omTargetAddress.SetReadOnly(false);
                //omMainWnd->m_omCanID.vSetValue(MsgID);
            }
            break;
            case INTERFACE_NORMAL_J1939_29:
            {
                omMainWnd->CanID = MsgID;
                omMainWnd->m_omSourceAddress.SetLimitText(2);
                omMainWnd->m_omTargetAddress.SetLimitText(2);
                omMainWnd->m_omSourceAddress.vSetValue(SourceAddress);
                omMainWnd->m_omTargetAddress.vSetValue(TargetAddress);
                omMainWnd->m_omSourceAddress.SetReadOnly(false);
                omMainWnd->m_omTargetAddress.SetReadOnly(false);
                //omMainWnd->m_omCanID.vSetValue(MsgID);
            }
            break;
        }
        /*
        ########################### Casian ##############################

        if( omMainWnd->CanID == 0 || SourceAddress==TargetAddress)
        {
            omMainWnd->m_omSendButton.EnableWindow(FALSE);
            omMainWnd->m_omCanID.vSetValue(0);

        }
        else
        {
            omMainWnd->m_omSendButton.EnableWindow(TRUE);
        }
        ########################### Casian ##############################
        */
    }


}

/**********************************************************************************************************
 Function Name  :   StartTimer_Disable_Send
 Description    :   This function is called from the CUDS_NegRespMng to disable the send button and to
                    start again the default timer
 Member of      :   CUDS_Protocol
 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   11.06.2013
**********************************************************************************************************/

void CUDS_Protocol::StartTimer_Disable_Send()
{
    omMainWnd->m_omSendButton.EnableWindow(FALSE);
    //omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);                //ReStar the timer that Enables the SendButton
    omMainWnd->SetTimer(ID_TIMER_SEND_BUTTON,P2_Time , NULL);   // Time to wait to able the send button again
}
/**********************************************************************************************************
 Function Name  :   StartTimer_Disable_Send
 Description    :   This function is called from the CUDS_NegRespMng to able the SEND button and to
                    kill the timer
 Member of      :   CUDS_Protocol
 Author(s)      :   Sanchez Marin Maria Alejandra
 Date Created   :   11.06.2013
**********************************************************************************************************/

void CUDS_Protocol::KillTimer_Able_Send()
{
    omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);          //ReStar the timer that Enables the SendButton
    omMainWnd->m_omSendButton.EnableWindow(TRUE);
}
//________________________________________________________________________________________________________________________________________________________________
//________________________________________________________________________________________________________________________________________________________________

