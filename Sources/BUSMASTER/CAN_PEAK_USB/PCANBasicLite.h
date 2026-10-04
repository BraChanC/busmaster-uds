#pragma once
#include <windows.h>

#define PCAN_NONEBUS              0x00U
#define PCAN_USBBUS1              0x51U
#define PCAN_USBBUS2              0x52U
#define PCAN_USBBUS3              0x53U
#define PCAN_USBBUS4              0x54U
#define PCAN_USBBUS5              0x55U
#define PCAN_USBBUS6              0x56U
#define PCAN_USBBUS7              0x57U
#define PCAN_USBBUS8              0x58U
#define PCAN_USBBUS9              0x509U
#define PCAN_USBBUS10             0x50AU
#define PCAN_USBBUS11             0x50BU
#define PCAN_USBBUS12             0x50CU
#define PCAN_USBBUS13             0x50DU
#define PCAN_USBBUS14             0x50EU
#define PCAN_USBBUS15             0x50FU
#define PCAN_USBBUS16             0x510U

#define PCAN_ERROR_OK             0x00000U
#define PCAN_ERROR_QRCVEMPTY      0x00020U
#define PCAN_ERROR_NODRIVER       0x00200U
#define PCAN_ERROR_ILLHW          0x01400U
#define PCAN_ERROR_ILLOPERATION   0x8000000U

#define PCAN_RECEIVE_EVENT        0x03U
#define PCAN_CHANNEL_CONDITION    0x0DU
#define PCAN_HARDWARE_NAME        0x0EU
#define PCAN_DEVICE_NUMBER        0x01U
#define PCAN_CHANNEL_VERSION      0x06U
#define PCAN_DEVICE_ID            0x04U
#define PCAN_CHANNEL_IDENTIFYING  0x0FU
#define PCAN_CHANNEL_FEATURES     0x16U
#define PCAN_ALLOW_ECHO_FRAMES    0x2CU

#define PCAN_CHANNEL_UNAVAILABLE  0x00U
#define PCAN_CHANNEL_AVAILABLE    0x01U
#define PCAN_CHANNEL_OCCUPIED     0x02U
#define PCAN_CHANNEL_PCANVIEW     (PCAN_CHANNEL_AVAILABLE | PCAN_CHANNEL_OCCUPIED)

#define FEATURE_FD_CAPABLE        0x01U

#define PCAN_MESSAGE_STANDARD     0x00U
#define PCAN_MESSAGE_RTR          0x01U
#define PCAN_MESSAGE_EXTENDED     0x02U
#define PCAN_MESSAGE_FD           0x04U
#define PCAN_MESSAGE_BRS          0x08U
#define PCAN_MESSAGE_ESI          0x10U
#define PCAN_MESSAGE_ERRFRAME     0x40U
#define PCAN_MESSAGE_STATUS       0x80U

#define PCAN_BAUD_1M              0x0014U
#define PCAN_BAUD_800K            0x0016U
#define PCAN_BAUD_500K            0x001CU
#define PCAN_BAUD_250K            0x011CU
#define PCAN_BAUD_125K            0x031CU
#define PCAN_BAUD_100K            0x432FU
#define PCAN_BAUD_50K             0x472FU

typedef WORD  TPCANHandle;
typedef DWORD TPCANStatus;
typedef BYTE  TPCANParameter;
typedef BYTE  TPCANMessageType;
typedef BYTE  TPCANType;
typedef WORD  TPCANBaudrate;
typedef LPSTR TPCANBitrateFD;

typedef struct tagTPCANMsg
{
    DWORD ID;
    TPCANMessageType MSGTYPE;
    BYTE LEN;
    BYTE DATA[8];
} TPCANMsg;

typedef struct tagTPCANTimestamp
{
    DWORD millis;
    WORD millis_overflow;
    WORD micros;
} TPCANTimestamp;

typedef struct tagTPCANMsgFD
{
    DWORD ID;
    TPCANMessageType MSGTYPE;
    BYTE DLC;
    BYTE DATA[64];
} TPCANMsgFD;

typedef unsigned __int64 TPCANTimestampFD;

typedef TPCANStatus (__stdcall *PFN_CAN_Initialize)(TPCANHandle, TPCANBaudrate, TPCANType, DWORD, WORD);
typedef TPCANStatus (__stdcall *PFN_CAN_InitializeFD)(TPCANHandle, TPCANBitrateFD);
typedef TPCANStatus (__stdcall *PFN_CAN_Uninitialize)(TPCANHandle);
typedef TPCANStatus (__stdcall *PFN_CAN_Read)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
typedef TPCANStatus (__stdcall *PFN_CAN_ReadFD)(TPCANHandle, TPCANMsgFD*, TPCANTimestampFD*);
typedef TPCANStatus (__stdcall *PFN_CAN_Write)(TPCANHandle, TPCANMsg*);
typedef TPCANStatus (__stdcall *PFN_CAN_WriteFD)(TPCANHandle, TPCANMsgFD*);
typedef TPCANStatus (__stdcall *PFN_CAN_GetValue)(TPCANHandle, TPCANParameter, void*, DWORD);
typedef TPCANStatus (__stdcall *PFN_CAN_SetValue)(TPCANHandle, TPCANParameter, void*, DWORD);
typedef TPCANStatus (__stdcall *PFN_CAN_GetErrorText)(TPCANStatus, WORD, LPSTR);
