#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

class CCAN_ZLG_USBApp : public CWinApp
{
public:
    CCAN_ZLG_USBApp();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};
