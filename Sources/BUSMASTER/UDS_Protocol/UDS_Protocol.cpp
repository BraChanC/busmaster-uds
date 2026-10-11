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
#include "UDS_Protocol.h"
#include "CANDriverDefines.h"


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
USAGEMODE HRESULT DIL_UDS_ShowWnd(HWND hParent,int TotalChannels)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

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
    }
    else
    {
        omMainWnd->fInterface = omManagerPtr->fInterface;
        omMainWnd->SourceAddress = omManagerPtr->SourceAddress;
        omMainWnd->TargetAddress = omManagerPtr->TargetAddress;
        omMainWnd->CanID = omManagerPtr->MsgID;
        omManagerPtr->UpdateFields();
    }

    omMainWnd->ShowWindow(SW_SHOW);
    omMainWnd->SetWindowText("Main Diagnostic Window");
    omMainWnd->ApplyCanFdSwitch();
    return 0;
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

static void PublishLongResponse(BOOL done)
{
    if (omMainWnd == NULL || !::IsWindow(omMainWnd->GetSafeHwnd()))
    {
        return;
    }
    if (!done && g_longTotal > 4096 && g_longPub != 0 && (g_longGot - g_longPub) < 16384)
    {
        return;
    }
    g_longPub = g_longGot;
    omMainWnd->ShowResponseBytes(g_longBuf, g_longGot, g_longTotal, done);
    if (done && omManagerPtr != NULL)
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

USAGEMODE HRESULT EvaluateMessage( STCAN_MSG  Mensaje  )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CUDSMainWnd* pMainWnd = omMainWnd;
    BOOL bClientUi = (pMainWnd != NULL && ::IsWindow(pMainWnd->GetSafeHwnd()));
    if (!bClientUi || GetWindowThreadProcessId(pMainWnd->GetSafeHwnd(), NULL) == GetCurrentThreadId())
    {
        UdsServer_OnCan(Mensaje);
    }
    if (!bClientUi)
    {
        return 0;
    }

    DWORD dwUiTid = GetWindowThreadProcessId(pMainWnd->GetSafeHwnd(), NULL);
    if (dwUiTid != GetCurrentThreadId())
    {
        UdsEnqueueRx(Mensaje);
        return 0;
    }

    unsigned char psMsg[64];
    memcpy( psMsg , Mensaje.m_ucData , 64);
    unsigned char Datalen = Mensaje.m_ucDataLen;
    UINT Check_SourceAddress;
    UINT Check_TargetAddress;
    UINT MessageID = Mensaje.m_unMsgID;
    UINT Channel =  Mensaje.m_ucChannel;

    UINT Check_MSG_received;

    // -------for checking also if the SA and TA are inserted in Main window and settings window holds only the base address (0x18DA0000)- Variable used is "Check_MSG_received" - Casian
    Check_SourceAddress = pMainWnd->m_omSourceAddress.lGetValue()<<8;
    Check_TargetAddress = pMainWnd->m_omTargetAddress.lGetValue();

    Check_MSG_received = respID + (Check_TargetAddress) + Check_SourceAddress;
    // Datalen is the DLC of the message received
    if(FSending && (respID == MessageID || Check_MSG_received == MessageID) && Current_Channel == Channel)    // -------  // Evaluates the received message when: 1.MainWnd created - 2. Response to my Msg - 3.I've sent something
    {

        TYPE_OF_FRAME TypeofFrame = omManagerPtr->getFrameType(psMsg[initialByte]);     // This is a general function because it uses the variable initialByte

        if (FWaitFlow == TRUE && TypeofFrame ==  FLOW_CONTROL )                 //Am I waiting for a flow control?
        {

            switch (omManagerPtr->TypeofControl)      // Para evaluar como deben estar los Textbox del SA y TA
            {
                case 0:
                {

                    BSizE = psMsg[initialByte+1];
                    if(psMsg[initialByte+2]<=0x7F)                  // Evaluate all the possible Values for STmin
                    {
                        STMin = psMsg[initialByte+2];
                    }
                    else if( 0xF1<=psMsg[initialByte+2]<=0xF9)
                    {
                        STMin = (psMsg[initialByte+2]&0x0F)*0.1;
                    }
                    else
                    {
                        STMin = 127;                                 //According to the ISO-TP
                    }
                    FWaitFlow = FALSE;                              // Don't wait for the flowcontrol anymore

                    omMainWnd->SendContinuosFrames(  omMainWnd->psTxCanMsgUds->m_psTxMsg->m_ucData,omMainWnd->psTxCanMsgUds,omMainWnd->fInterface );

                }
                break;
                case 1:
                {
                    omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);                 // Wait,ReStart Timer -> do not able the SEND button yet
                    omMainWnd->SetTimer(ID_TIMER_SEND_BUTTON,P2_Time , NULL);
                }
                break;
                default:                    // If I've received any reserved message or  an Overflow message.
                {
                    FWaitFlow = FALSE;      // To indicate that I should not receive a new flow control
                }                   // If it's -1 then it's not a flow control message
                //do nothing
            }

        }
        else if(TypeofFrame ==  LONG_RESPONSE)           //Is it a long response??
        {

            int TempValue = strtol(CurrentService, NULL, 16);
            int ffData = 6 - aux_Finterface;
            int sidPos = initialByte + 2;
            /* ISO 15765-2 FF with a 32-bit length: 10 00 + 4 length bytes + data. */
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
                omManagerPtr->numberOfBytes =1;
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
                TotalFrames = (float)remain / (float)cfSize;
                LengthLastFrame = remain % cfSize;
                BeginLongResponse((int)Length_Received);
                if (Datalen > sidPos)
                {
                    AppendLongResponse(psMsg + sidPos, Datalen - sidPos);
                }
                omMainWnd->PrepareFlowControl();
                PublishLongResponse(FALSE);
                omMainWnd->SetResponseService(NegRespManager->evaluateResp(psMsg , sidPos - 1));
                omMainWnd->m_omSendButton.EnableWindow(FALSE);
                m_nTimer = omMainWnd->SetTimer(ID_TIMER_SEND_BUTTON,P2_Time /*TIME_UNABLE_SEND_BUTTON*/, NULL);
            }

        }
        else if(TypeofFrame== CONSECUTIVE_FRAME)                          //Is it a consecutive Frame?
        {
            Counter_BSize--;
            TotalFrames--;
            if (((int)g_longGot & 31) == 0 || TotalFrames <= 0)
            {
                omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);
                omMainWnd->SetTimer(ID_TIMER_SEND_BUTTON,P2_Time , NULL);
            }

            if(TotalFrames==0)                       //It enters here when the last frame has exactly 7/6 bytes of data
            {
                FSending = FALSE;                    //Stops showing the bytes in the mainWnd
                omMainWnd->m_omSendButton.EnableWindow(TRUE);
                omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);

            }
            else if(TotalFrames<0)                   //It enters here when the last frame has less than 7/6 bytes of data
            {
                Datalen = LengthLastFrame+1+aux_Finterface;
                FSending = FALSE;                    //Stops showing the bytes in the mainWnd
                omMainWnd->KillTimer(ID_TIMER_SEND_BUTTON);
                omMainWnd->m_omSendButton.EnableWindow(TRUE);
            }
            if (Datalen > (unsigned char)(initialByte + 1))
            {
                AppendLongResponse(psMsg + initialByte + 1, Datalen - (initialByte + 1));
            }
            PublishLongResponse(TotalFrames <= 0);
            if (TotalFrames <= 0 && omMainWnd != NULL)
            {
                omMainWnd->TryAutoSendSecurityKeyFromResponse();
            }
            if(Counter_BSize ==0)
            {
                omMainWnd->PrepareFlowControl();    // If I've received BSize frames-> Send the flow control
            }

        }
        else                                  // This case corresponds to a simple response
        {
            omManagerPtr->numberOfBytes =1;
            FSending = FALSE;                 //Show this message and then Stop showing the bytes
            int sfPci = initialByte;
            int sfLen = psMsg[initialByte];
            if (Datalen > 8 && psMsg[initialByte] == 0x00)
            {
                sfPci = initialByte + 1;
                sfLen = psMsg[initialByte + 1];
                Length_Received = (float)sfLen;
            }
            omMainWnd->m_omDiagService = NegRespManager->evaluateResp(psMsg , sfPci);         // Show the description of the Response
            if(!FDontShow)
            {
                Length_Received, omManagerPtr->Show_ResponseData(psMsg ,(unsigned char)(sfLen + sfPci + 1), sfPci);
                if (omMainWnd != NULL)
                {
                    omMainWnd->TryAutoSendSecurityKeyFromResponse();
                }
            }
            FDontShow = FALSE;                // The nextFramme can be shown

        }
        if (TypeofFrame == LONG_RESPONSE || (TypeofFrame == CONSECUTIVE_FRAME && TotalFrames <= 0))
        {
            omMainWnd->m_omBytes.vSetValue(g_longTotal);
        }
        else if (TypeofFrame != CONSECUTIVE_FRAME)
        {
            omMainWnd->m_omBytes.vSetValue(Length_Received);
        }
    }
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

