#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

class CCAN_PEAK_USBApp : public CWinApp
{
public:
    CCAN_PEAK_USBApp();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};
