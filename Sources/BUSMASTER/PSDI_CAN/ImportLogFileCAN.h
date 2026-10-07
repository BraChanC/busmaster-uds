/**
 * \file      ImportLogFileCAN.h
 * \brief     Import BUSMASTER CAN .log files into Message Window
 */
#pragma once

#include "Utility/BaseImportLogFile.h"

// STCANDATA comes from Kernel CANDriverDefines.h (class), not Include/Struct_CAN_.h
// (typedef struct) — including both causes C2371 redefinition errors.
class STCANDATA;

class CImportLogFileCAN : public CBaseImportLogFile
{
public:
    CImportLogFileCAN();
    virtual ~CImportLogFileCAN();

    virtual HRESULT GetMsgAt(void* pvMsg, const unsigned long& nLineNo);
    virtual HRESULT GetCurrMsg(void* pvMsg);
    virtual HRESULT GetPrevMsg(void* pvMsg);
    virtual HRESULT GetNextMsg(void* pvMsg);
    virtual HRESULT GetCurrListMsg(std::vector<void*>& vecpvMsg);
    virtual HRESULT GetPrevListMsg(std::vector<void*>& vecpvMsg);
    virtual HRESULT GetNextListMsg(std::vector<void*>& vecpvMsg);

private:
    BOOL ParseCanLogLine(const std::string& strLine, STCANDATA* pData) const;
};
