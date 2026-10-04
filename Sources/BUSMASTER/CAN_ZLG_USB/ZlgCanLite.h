#pragma once

#include <windows.h>

#define ZCAN_USBCAN1              3
#define ZCAN_USBCAN2              4
#define ZCAN_USBCAN_E_U           20
#define ZCAN_USBCAN_2E_U          21
#define ZCAN_USBCANFD_200U        41
#define ZCAN_USBCANFD_100U        42
#define ZCAN_USBCANFD_MINI        43
#define ZCAN_USBCANFD_800U        59
#define ZCAN_USBCANFD_400U        76

#define TYPE_CAN      0
#define TYPE_CANFD    1

#define STATUS_OK     1

#define CAN_EFF_FLAG  0x80000000U
#define CAN_RTR_FLAG  0x40000000U
#define CAN_ERR_FLAG  0x20000000U
#define CAN_ID_FLAG   0x1FFFFFFFU
#define CANFD_BRS     0x01

typedef void* DEVICE_HANDLE;
typedef void* CHANNEL_HANDLE;

typedef struct tagZCAN_DEVICE_INFO
{
    USHORT hw_Version;
    USHORT fw_Version;
    USHORT dr_Version;
    USHORT in_Version;
    USHORT irq_Num;
    BYTE   can_Num;
    UCHAR  str_Serial_Num[20];
    UCHAR  str_hw_Type[40];
    USHORT reserved[4];
} ZCAN_DEVICE_INFO;

typedef struct tagZCAN_CHANNEL_INIT_CONFIG
{
    UINT can_type;
    union
    {
        struct
        {
            UINT acc_code;
            UINT acc_mask;
            UINT reserved;
            BYTE filter;
            BYTE timing0;
            BYTE timing1;
            BYTE mode;
        } can;
        struct
        {
            UINT   acc_code;
            UINT   acc_mask;
            UINT   abit_timing;
            UINT   dbit_timing;
            UINT   brp;
            BYTE   filter;
            BYTE   mode;
            USHORT pad;
            UINT   reserved;
        } canfd;
    };
} ZCAN_CHANNEL_INIT_CONFIG;

typedef struct tag_can_frame
{
    UINT can_id;
    BYTE can_dlc;
    BYTE __pad;
    BYTE __res0;
    BYTE __res1;
    BYTE data[8];
} zlg_can_frame;

typedef struct tag_canfd_frame
{
    UINT can_id;
    BYTE len;
    BYTE flags;
    BYTE __res0;
    BYTE __res1;
    BYTE data[64];
} zlg_canfd_frame;

typedef struct tagZCAN_Transmit_Data
{
    zlg_can_frame frame;
    UINT          transmit_type;
} ZCAN_Transmit_Data;

typedef struct tagZCAN_Receive_Data
{
    zlg_can_frame frame;
    UINT64        timestamp;
} ZCAN_Receive_Data;

typedef struct tagZCAN_TransmitFD_Data
{
    zlg_canfd_frame frame;
    UINT            transmit_type;
} ZCAN_TransmitFD_Data;

typedef struct tagZCAN_ReceiveFD_Data
{
    zlg_canfd_frame frame;
    UINT64          timestamp;
} ZCAN_ReceiveFD_Data;

typedef DEVICE_HANDLE (__stdcall *PFN_ZCAN_OpenDevice)(UINT, UINT, UINT);
typedef UINT (__stdcall *PFN_ZCAN_CloseDevice)(DEVICE_HANDLE);
typedef UINT (__stdcall *PFN_ZCAN_GetDeviceInf)(DEVICE_HANDLE, ZCAN_DEVICE_INFO*);
typedef CHANNEL_HANDLE (__stdcall *PFN_ZCAN_InitCAN)(DEVICE_HANDLE, UINT, ZCAN_CHANNEL_INIT_CONFIG*);
typedef UINT (__stdcall *PFN_ZCAN_StartCAN)(CHANNEL_HANDLE);
typedef UINT (__stdcall *PFN_ZCAN_ResetCAN)(CHANNEL_HANDLE);
typedef UINT (__stdcall *PFN_ZCAN_GetReceiveNum)(CHANNEL_HANDLE, BYTE);
typedef UINT (__stdcall *PFN_ZCAN_Transmit)(CHANNEL_HANDLE, ZCAN_Transmit_Data*, UINT);
typedef UINT (__stdcall *PFN_ZCAN_Receive)(CHANNEL_HANDLE, ZCAN_Receive_Data*, UINT, int);
typedef UINT (__stdcall *PFN_ZCAN_TransmitFD)(CHANNEL_HANDLE, ZCAN_TransmitFD_Data*, UINT);
typedef UINT (__stdcall *PFN_ZCAN_ReceiveFD)(CHANNEL_HANDLE, ZCAN_ReceiveFD_Data*, UINT, int);
typedef UINT (__stdcall *PFN_ZCAN_SetValue)(DEVICE_HANDLE, const char*, const void*);
