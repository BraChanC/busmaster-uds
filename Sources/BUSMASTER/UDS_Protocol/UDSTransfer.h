#pragma once

HRESULT UdsTransfer_Show(HWND hParent);

/* Window that gets WM_UDS_RSP_READY whenever a response is stored. */
extern HWND volatile g_hUdsRspNotify;
