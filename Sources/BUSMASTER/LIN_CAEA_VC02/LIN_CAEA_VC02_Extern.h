#pragma once

#if defined USAGEMODE
#undef USAGEMODE
#endif

#if defined USAGE_EXPORT
#define USAGEMODE   __declspec(dllexport)
#else
#define USAGEMODE   __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

USAGEMODE HRESULT GetIDIL_LIN_Controller(void** ppvInterface);

#ifdef __cplusplus
}
#endif
