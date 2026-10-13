#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

class CCAN_USBCANApp : public CWinApp
{
public:
    CCAN_USBCANApp();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};
