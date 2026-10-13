#pragma once

#include <windows.h>

#ifdef VC02_EXPORTS
#define VC02_API __declspec(dllexport)
#else
#define VC02_API __declspec(dllimport)
#endif

extern "C" {

enum
{
    VC02_KIND_CAN = 1,
    VC02_KIND_LIN = 2
};

enum
{
    VC02_FAMILY_VC01 = 1,
    VC02_FAMILY_VC02 = 2
};

struct VC02_DEVICE
{
    int com;
    int canCount;
    int linCount;
    char version[64];
    int family;
};

struct VC02_MSG
{
    int kind;
    int channel;
    int isFd;
    int isExt;
    int isTx;
    int isErr;
    int errCode;
    unsigned int id;
    int dlc;
    unsigned char data[64];
    double timeSec;
};

VC02_API int vc02_find(VC02_DEVICE* devices, int maxCount);
VC02_API void* vc02_open(int com);
VC02_API void vc02_close(void* session);
VC02_API int vc02_set_can(void* session, int channel, unsigned baud, unsigned fdBaud, int enableFd, int termination, int listenOnly, int resend);
VC02_API int vc02_set_lin(void* session, int channel, unsigned baud, int master);
VC02_API int vc02_tx_can(void* session, int channel, unsigned id, int ext, int fd, int brs, const unsigned char* data, int dlc);
VC02_API int vc02_tx_lin(void* session, int channel, unsigned id, const unsigned char* data, int dlc);
VC02_API int vc02_read(void* session, VC02_MSG* msgs, int maxCount, int kind);
VC02_API const char* vc02_last_error(void);

}
