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
#include "BaseDIL_CAN.h"
#include "ProtocolsDefinitions.h"
#include "UDSIso14229.h"
#include "IBusMasterKernel.h"
#include <afxdlgs.h>
CUDSMainWnd* CUDSMainWnd::m_spodInstance = NULL;

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

int CUDSMainWnd::SendFirstFrame(CString omByteStr, unsigned char abByteArr[], mPSTXSELMSGDATA psTxCanMsgUds, UDS_INTERFACE FInterface)
{
    bool FirstFrame= TRUE;
    CString Length;
    CString omTempByte;
    CString omByteStrTemp;
    //int numberofFrames =-1    ;
    int c_numberOfTaken= numberOfTaken;                             // It contains the number of Data bytes that can be sent in the current message

    Length.Format("%.3x\n",TotalLength);                        //Prepare the first 2 bytes of a first frame of a long request
    Length = "1" + Length;
    omTempByte = Length.Left(2);
    abByteArr[initialByte]= (BYTE)_tcstol(omTempByte, L'\0', 16);
    omTempByte = Length.Right(3);
    abByteArr[initialByte+1]= (BYTE)_tcstol(omTempByte,L'\0', 16);

    int i=7;
    omByteStrTemp = omByteStr.Left(c_numberOfTaken);                //I take the part of omByteStr that is going to be sent in the first Frame

    //while(FWait_SendingFrame){}                                       // Wait if something is being sent in this moment
    while (omByteStrTemp.GetLength())
    {
        omTempByte = omByteStrTemp.Right(NO_OF_CHAR_IN_BYTE);
        abByteArr[i--] = (BYTE)_tcstol(omTempByte, L'\0', 16);      // Fill the array to sent with the current data
        omByteStrTemp = omByteStrTemp.Left(omByteStrTemp.GetLength() - NO_OF_CHAR_IN_BYTE);
    }
    psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;                  // The FirstFrame always has 8 butes

    SendSimpleDiagnosticMessage();                              //Send the message

    omByteStr = omByteStr.Right(((UINT)TotalLength*2)-c_numberOfTaken);          // The bytes that were sent are deleted
    DatatoSend =  omByteStr.Right(((UINT)TotalLength*2)-c_numberOfTaken);        // DatatoSend will contain the rest of the bytes that hasn't been sent yet.
    TotalLength = (((UINT)TotalLength*2)-c_numberOfTaken)/2;

    FirstFrame = FALSE;
    FWaitFlow =TRUE;                    // No Wait for the Flow Control comming from the ECU
    ConsecutiveFrame = 0x21;            //Initialize this -> because a long frame will be sent
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

void CUDSMainWnd::SendContinuosFrames( unsigned char abByteArr[],mPSTXSELMSGDATA psTxCanMsgUds, UDS_INTERFACE FInterface)
{

    CString Length;
    CString omTempByte;
    CString omByteStrTemp;
    int numberofFrames =0;                      // It will indicate how many multiples frames have been sent
    int c_numberOfTaken = numberOfTaken+2;      // The consecutive Messages will contain one byte more that the FirstFrame

    int i = aux_Finterface + c_numberOfTaken/2;         // aux_Finterface it's used to indicate that i must be bigger if we're in extended Addressing
    if (TotalLength*2<c_numberOfTaken)          // It only enters here once, at the end when the last message of the multiple frames has to be sent when
    {
        i = TotalLength+aux_Finterface;         // the number of frames that has to be sent is less than c_numberOfTaken
    }
    while (DatatoSend.GetLength())                          // While there is remaining data that has to be sent
    {
        omByteStrTemp = DatatoSend.Left(c_numberOfTaken);   //I take the part of the message that is going to be sent in the current Frame
        //while(FWait_SendingFrame){}                           // Wait if something is being sent in this moment

        while (omByteStrTemp.GetLength())
        {
            omTempByte = omByteStrTemp.Right(NO_OF_CHAR_IN_BYTE);
            abByteArr[i--] = (BYTE)_tcstol(omTempByte, L'\0', 16);              // It fills the array
            omByteStrTemp = omByteStrTemp.Left(omByteStrTemp.GetLength() - NO_OF_CHAR_IN_BYTE);
        }
        psTxCanMsgUds->m_psTxMsg->m_ucDataLen = 8;                  // Consecutive Frames can always have 8 bytes
        abByteArr[initialByte]= ConsecutiveFrame;                   // Put the initial Byte of the consecutive frames in a long request
        SendSimpleDiagnosticMessage();                              // Send the current Message

        DatatoSend = DatatoSend.Right(((UINT)TotalLength*2)-c_numberOfTaken);        // DatatoSend will contain the rest of the bytes that hasn't been sent yet.
        TotalLength = (((UINT)TotalLength*2)-c_numberOfTaken)/2;
        ConsecutiveFrame++;
        if (ConsecutiveFrame == 0x30)
        {
            ConsecutiveFrame=0x20;    // Requirement from the ISO TP
        }
        numberofFrames++;

        if (numberofFrames == BSizE)        // It enters here when I've reached the quantity of Blocks settled by the ECU in the flow Control
        {
            FWaitFlow = TRUE;               // Now it has to wait for the Flow control again
            numberofFrames = 0;
            c_dDiffTime =0;
            return ;                        // Now it has to wait for another FlowControl
        }
        else
        {
            for(c_dDiffTime =0,c_unPreviousTime =-1 ; c_dDiffTime <=STMin; CalculateDiffTime()) {}      // Wait for the STMin Time settled by the ECU in the flow Control
        }
        c_unPreviousTime = -1;              //ReStart the variables for the timmings
        c_dDiffTime = 0;

        i = aux_Finterface + c_numberOfTaken/2;             // it must be a bigger number in the case of extended addressing-> aux_Finterface to control this.
        if (TotalLength*2<c_numberOfTaken)                  // It only enters here once, at the end when the last message of the multiple frames has to be sent when
        {
            i = TotalLength+aux_Finterface;                 // the number of frames that has to be sent is less than c_numberOfTaken
        }
    }
    m_omSendButton.EnableWindow(TRUE);      // It only enters here when it has sent all the msg
    // In the case that this function cannot be completed there is a timer as default to activate the SEND button
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

    Counter_BSize = BSize;                              // BSize and SSTMin  depends of the value put in the settingsWnd.
    //FWaitLongRespBSize = Counter_BSize;                   // Not used
    psTxCanMsgUds->m_psTxMsg->m_ucDataLen= SizeFC;      //The DLC of the FCmessage depends of the value put in the settingsWnd.
    memset(psTxCanMsgUds->m_psTxMsg->m_ucData, 0, SizeFC);

    // ################## Casian #############################//
    switch(fInterface)
    {
        case INTERFACE_NORMAL_11 :
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x30;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+1] = BSize;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+2] = SSTMin; //////
            SendSimpleDiagnosticMessage();

        };
        break;

        case INTERFACE_EXTENDED_11 :
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte-1] = TargetAddress;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x30;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+1] = BSize;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+2] = SSTMin;
            SendSimpleDiagnosticMessage();

        };
        break;

        case INTERFACE_NORMAL_ISO_29 :
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x30;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+1] = BSize;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+2] = SSTMin;
            SendSimpleDiagnosticMessage();

        };
        break;

        case INTERFACE_NORMAL_J1939_29 :
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte] = 0x30;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+1] = BSize;
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte+2] = SSTMin;

            SendSimpleDiagnosticMessage();

        };
        break;

    }
    // ################## Casian #############################//

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
            omLog.Format("TX ch=%d id=0x%X data=%s",
                         psTxCanMsgUds->m_psTxMsg->m_ucChannel,
                         psTxCanMsgUds->m_psTxMsg->m_unMsgID,
                         m_omMsgDataEdit);
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
        memset(psTxCanMsgUds->m_psTxMsg->m_ucData, 0, 8);               // Initialize the bytes to 0
        if(fInterface == INTERFACE_EXTENDED_11 )
        {
            psTxCanMsgUds->m_psTxMsg->m_ucData[initialByte-1]= TargetAddress;
        }
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
        SendSimpleDiagnosticMessage();
    }
    if ( nIDEvent == ID_TIMER_SEND_BUTTON)
    {
        m_omSendButton.EnableWindow(TRUE);
        KillTimer(ID_TIMER_SEND_BUTTON);
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

    UpdateData();
    CString omByteStr;
    omByteStr = m_omMsgDataEdit;
    omByteStr.Replace(" ","");
    omByteStr.Replace("\t","");

    UINT LengthStr = omByteStr.GetLength();
    m_omMsgDataEdit = "";
    while( omByteStr.GetLength()>2)
    {
        m_omMsgDataEdit = m_omMsgDataEdit + omByteStr.Left(2)+" ";
        omByteStr = omByteStr.Right(omByteStr.GetLength()-2);
    }
    m_omMsgDataEdit = m_omMsgDataEdit + omByteStr;

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
    return TRUE;
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
    GetDlgItem(IDC_RESPONSE_DATA)->SetFont(&m_Font);
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

    GetDlgItem(IDC_DIAG_SERVICE)->SetFont(&m_Font);
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
    return "     No Response Received";        // If it's the case of  No Positive Response Required return the default value
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

void CUDSMainWnd::AppendLog(const CString& omText)
{
    if (!::IsWindow(m_omEditLog.GetSafeHwnd()))
    {
        return;
    }
    CString omOld;
    m_omEditLog.GetWindowText(omOld);
    CTime omNow = CTime::GetCurrentTime();
    CString omLine;
    omLine.Format("[%s] %s\r\n", omNow.Format("%H:%M:%S"), omText);
    omOld += omLine;
    m_omEditLog.SetWindowText(omOld);
    m_omEditLog.LineScroll(m_omEditLog.GetLineCount());
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
}
