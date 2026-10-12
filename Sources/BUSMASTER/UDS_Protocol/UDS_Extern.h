/**
 * \file      UDS_Extern.h
 * \author     Sanchez Marin Maria Alejandra
 * Designer:    Bentea Radu Mihai
 */

#pragma once

//#include "PSDI_CAN/MsgContainer_CAN.h"
#include "UDS_Protocol.h"
//#include "Include/Struct_CAN.h"
#include "CANDriverDefines.h"




#if defined USAGEMODE
#undef USAGEMODE
#endif

#if defined USAGE_EXPORT
#define USAGEMODE   __declspec(dllexport)
#else
#define USAGEMODE   __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {  // only need to export C interface if used by C++ source code
#endif
    USAGEMODE HRESULT UDS_Initialise();
    //__declspec(dllexport) HRESULT DIL_UDS_ShowWnd(void); //Funci�n Para mostrar Main Window

    USAGEMODE HRESULT DIL_UDS_ShowWnd(HWND hParent,int TotalChannels);//; //Funci�n Para mostrar Main Window
    USAGEMODE void DIL_UDS_SetCanFdAvailable(BOOL available);
    USAGEMODE HRESULT EvaluateMessage( STCAN_MSG  Mensaje  );
    USAGEMODE HRESULT UpdateChannelUDS(HWND hParent);
    USAGEMODE HRESULT DIL_UDS_ShowSettingWnd(HWND hParent);
    USAGEMODE HRESULT DIL_UDS_ShowFlashWnd(HWND hParent);
    USAGEMODE HRESULT DIL_UDS_ShowServerWnd(HWND hParent);
    USAGEMODE HRESULT DIL_UDS_ShowTransferWnd(HWND hParent);
    USAGEMODE LONG DIL_UDS_ResponseSeq();
    USAGEMODE int DIL_UDS_CopyResponse(BYTE* out, int cap);
    USAGEMODE HRESULT TX_vSetDILInterfacePtrUDS(void* ptrDILIntrf);
    USAGEMODE HRESULT DIL_MainPanel(HWND hParent,int TotalChannels);
    //int respID;

#ifdef __cplusplus
}
#endif


