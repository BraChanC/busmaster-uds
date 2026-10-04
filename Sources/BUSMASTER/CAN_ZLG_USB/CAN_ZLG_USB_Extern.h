#if !defined CAN_ZLG_USB_EXTERN_H__INCLUDED_
#define CAN_ZLG_USB_EXTERN_H__INCLUDED_

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

USAGEMODE HRESULT __cdecl GetIDIL_CAN_Controller(void** ppvInterface);

#ifdef __cplusplus
}
#endif

#endif
