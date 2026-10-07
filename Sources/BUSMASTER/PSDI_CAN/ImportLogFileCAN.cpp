/**
 * \file      ImportLogFileCAN.cpp
 * \brief     Parse BUSMASTER CAN log lines for Message Window import
 */

#include "PSDI_CAN/stdafx_CAN.h"
#include "CANDriverDefines.h"
#include "ImportLogFileCAN.h"
#include "Application/HashDefines.h"
#include <cctype>

// Flags live in Include/Struct_CAN_.h which conflicts with Kernel class STCAN_MSG.
#ifndef TX_FLAG
#define TX_FLAG  0x01
#endif
#ifndef RX_FLAG
#define RX_FLAG  0x02
#endif

CImportLogFileCAN::CImportLogFileCAN()
    : CBaseImportLogFile(CAN)
{
}

CImportLogFileCAN::~CImportLogFileCAN()
{
}

BOOL CImportLogFileCAN::ParseCanLogLine(const std::string& strLine, STCANDATA* pData) const
{
    if (pData == nullptr || strLine.empty())
    {
        return FALSE;
    }

    memset(pData, 0, sizeof(STCANDATA));

    CString omSendMsgLine(strLine.c_str());
    omSendMsgLine.TrimLeft();
    omSendMsgLine.TrimRight();
    if (omSendMsgLine.IsEmpty() || omSendMsgLine.Find("***") == 0)
    {
        return FALSE;
    }

    CString omStrTemp = omSendMsgLine.SpanExcluding("\t ");
    if (omStrTemp.IsEmpty())
    {
        return FALSE;
    }

    INT nIndex = omStrTemp.GetLength();
    omStrTemp = omSendMsgLine.Right(omSendMsgLine.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();
    omStrTemp.TrimRight();

    // Tx / Rx
    CString omStrDir = omStrTemp.SpanExcluding("\t ");
    if (omStrDir.CompareNoCase("Tx") == 0)
    {
        pData->m_ucDataType = TX_FLAG;
    }
    else if (omStrDir.CompareNoCase("Rx") == 0)
    {
        pData->m_ucDataType = RX_FLAG;
    }
    else
    {
        return FALSE;
    }
    nIndex = omStrDir.GetLength();
    omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();

    // Channel
    CString omStrChannel = omStrTemp.SpanExcluding("\t ");
    CHAR* pcStop = nullptr;
    UCHAR ucChannel = (UCHAR)strtol((LPCTSTR)omStrChannel, &pcStop, 10);
    nIndex = omStrChannel.GetLength();
    omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();

    // ID (may include name before '[')
    CString omStrMsgID = omStrTemp.SpanExcluding("\t ");
    nIndex = omStrMsgID.GetLength();
    omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();
    omStrMsgID = omStrMsgID.SpanExcluding(defMSGID_NAME_DELIMITER);

    UINT unMsgID = 0;
    if (m_bIsModeHex)
    {
        unMsgID = (UINT)strtoul((LPCTSTR)omStrMsgID, &pcStop, 16);
    }
    else
    {
        unMsgID = (UINT)strtoul((LPCTSTR)omStrMsgID, &pcStop, 10);
    }

    // Type: s / x / r ...
    CString omStrType = omStrTemp.SpanExcluding("\t ");
    pData->m_uDataInfo.m_sCANMsg.m_ucEXTENDED = (omStrType.Find(defMSGID_EXTENDED) != -1) ? 1 : 0;
    pData->m_uDataInfo.m_sCANMsg.m_ucRTR = (omStrType.Find(defMSGID_RTR) != -1) ? 1 : 0;
    nIndex = omStrType.GetLength();
    omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();

    // DLC is logged in decimal
    CString omStrDLC = omStrTemp.SpanExcluding("\t ");
    UINT unDLC = (UINT)strtoul((LPCTSTR)omStrDLC, &pcStop, 10);
    nIndex = omStrDLC.GetLength();
    omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
    omStrTemp.TrimLeft();

    if (unDLC > 8)
    {
        return FALSE;
    }

    // Data bytes (hex or decimal per log mode)
    for (UINT i = 0; i < unDLC; ++i)
    {
        omStrTemp.TrimLeft();
        if (omStrTemp.IsEmpty())
        {
            return FALSE;
        }
        CString omByte = omStrTemp.SpanExcluding("\t ");
        if (omByte.IsEmpty())
        {
            return FALSE;
        }
        int nBase = m_bIsModeHex ? 16 : 10;
        pData->m_uDataInfo.m_sCANMsg.m_ucData[i] = (BYTE)strtoul((LPCTSTR)omByte, &pcStop, nBase);
        nIndex = omByte.GetLength();
        if (omStrTemp.GetLength() <= nIndex)
        {
            omStrTemp.Empty();
        }
        else
        {
            omStrTemp = omStrTemp.Right(omStrTemp.GetLength() - nIndex - 1);
        }
    }

    pData->m_uDataInfo.m_sCANMsg.m_unMsgID = unMsgID;
    pData->m_uDataInfo.m_sCANMsg.m_ucDataLen = (UCHAR)unDLC;
    pData->m_uDataInfo.m_sCANMsg.m_ucChannel = ucChannel;
    return TRUE;
}

HRESULT CImportLogFileCAN::GetMsgAt(void* pvMsg, const unsigned long& nLineNo)
{
    if (pvMsg == nullptr)
    {
        return S_FALSE;
    }

    std::string strLine;
    m_ouReadFile.GetLine(nLineNo, strLine);
    STCANDATA* pData = reinterpret_cast<STCANDATA*>(pvMsg);
    if (!ParseCanLogLine(strLine, pData))
    {
        return S_FALSE;
    }

    if (nLineNo < m_vecTimeStamp.size())
    {
        pData->m_lTickCount.QuadPart = (__int64)m_vecTimeStamp[nLineNo];
    }
    return S_OK;
}

HRESULT CImportLogFileCAN::GetCurrMsg(void* pvMsg)
{
    return GetMsgAt(pvMsg, m_nCurrLineNo);
}

HRESULT CImportLogFileCAN::GetPrevMsg(void* pvMsg)
{
    if (m_nCurrLineNo == 0)
    {
        return S_FALSE;
    }
    --m_nCurrLineNo;
    return GetCurrMsg(pvMsg);
}

HRESULT CImportLogFileCAN::GetNextMsg(void* pvMsg)
{
    unsigned long nTotal = 0;
    GetTotalLines(nTotal);
    if (m_nCurrLineNo + 1 >= nTotal)
    {
        return S_FALSE;
    }
    ++m_nCurrLineNo;
    return GetCurrMsg(pvMsg);
}

HRESULT CImportLogFileCAN::GetCurrListMsg(std::vector<void*>& vecpvMsg)
{
    vecpvMsg.clear();
    unsigned long nTotal = 0;
    GetTotalLines(nTotal);
    if (nTotal == 0 || m_nPageLength == 0)
    {
        return S_FALSE;
    }

    unsigned long nStart = m_nCurrPageNo * m_nPageLength;
    if (nStart >= nTotal)
    {
        return S_FALSE;
    }
    unsigned long nEnd = nStart + m_nPageLength;
    if (nEnd > nTotal)
    {
        nEnd = nTotal;
    }

    for (unsigned long nIdx = nStart; nIdx < nEnd; ++nIdx)
    {
        STCANDATA* pMsg = new STCANDATA;
        if (GetMsgAt(pMsg, nIdx) == S_OK)
        {
            vecpvMsg.push_back(pMsg);
        }
        else
        {
            delete pMsg;
        }
    }
    return vecpvMsg.empty() ? S_FALSE : S_OK;
}

HRESULT CImportLogFileCAN::GetPrevListMsg(std::vector<void*>& vecpvMsg)
{
    if (m_nCurrPageNo == 0)
    {
        return S_FALSE;
    }
    --m_nCurrPageNo;
    return GetCurrListMsg(vecpvMsg);
}

HRESULT CImportLogFileCAN::GetNextListMsg(std::vector<void*>& vecpvMsg)
{
    unsigned long nPages = 0;
    GetTotalPages(nPages);
    if (m_nCurrPageNo + 1 >= nPages)
    {
        return S_FALSE;
    }
    ++m_nCurrPageNo;
    return GetCurrListMsg(vecpvMsg);
}
