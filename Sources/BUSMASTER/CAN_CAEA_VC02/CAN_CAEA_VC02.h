#pragma once

class CCAN_CAEA_VC02App : public CWinApp
{
public:
    CCAN_CAEA_VC02App();
    virtual BOOL InitInstance();
    DECLARE_MESSAGE_MAP()
};
