#include "stdafx.h"
#include "UDSTransfer.h"
#include "UDSMainWnd.h"
#include "resource.h"
#include "UDS_Extern.h"
#include <stdio.h>

extern CUDSMainWnd* omMainWnd;

#define XFER_TIMER 1
#define XFER_MAX   (16 * 1024 * 1024)

enum XferPhase
{
    PH_IDLE = 0,
    PH_DL_74,
    PH_DL_76,
    PH_DL_77,
    PH_UL_75,
    PH_UL_76,
    PH_UL_77
};

class CUDSTransferWnd : public CDialog
{
public:
    CUDSTransferWnd() : CDialog(IDD_UDS_TRANSFER, NULL)
    {
        m_phase = PH_IDLE;
        m_file = NULL;
        m_fileLen = 0;
        m_off = 0;
        m_seq = 1;
        m_block = 128;
        m_waitSeq = 0;
        m_sentAt = 0;
        m_blockT0 = 0;
        m_rspLen = 0;
    }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();
    virtual void PostNcDestroy();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBrowse();
    afx_msg void OnDownload();
    afx_msg void OnUpload();
    afx_msg void OnStop();
    DECLARE_MESSAGE_MAP()

    void Log(const CString& text);
    void SetBusy(BOOL busy);
    BOOL ReadUi(unsigned& addr, int& addrN, int& sizeN, BYTE& dfi, int& cap, unsigned& upSize);
    BOOL LoadFile(const CString& path);
    void FreeFile();
    CString Be(unsigned value, int n);
    CString HexOf(const BYTE* data, int n);
    BOOL SendHex(const CString& hex);
    BOOL TakeResponse();
    void Fail(const CString& text);
    void Succeed(const CString& text);
    void UpdateProgress(int done, int total);
    CString NowStamp() const;
    void SendNextDownload();
    void SendNextUpload();
    void OnResponse();

    int m_phase;
    BYTE* m_file;
    int m_fileLen;
    int m_off;
    int m_seq;
    int m_block;
    LONG m_waitSeq;
    DWORD m_sentAt;
    DWORD m_blockT0;
    BYTE m_rsp[8192];
    int m_rspLen;
    unsigned m_addr;
    int m_addrN;
    int m_sizeN;
    BYTE m_dfi;
    CString m_path;
};

static CUDSTransferWnd* g_pXfer = NULL;

BEGIN_MESSAGE_MAP(CUDSTransferWnd, CDialog)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_XFER_BROWSE, OnBrowse)
    ON_BN_CLICKED(IDC_XFER_DOWN, OnDownload)
    ON_BN_CLICKED(IDC_XFER_UP, OnUpload)
    ON_BN_CLICKED(IDC_XFER_STOP, OnStop)
END_MESSAGE_MAP()

BOOL CUDSTransferWnd::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetDlgItemText(IDC_XFER_ADDR, "0");
    SetDlgItemText(IDC_XFER_SIZE, "256");
    SetDlgItemText(IDC_XFER_DFI, "00");
    SetDlgItemText(IDC_XFER_BLOCK, "0");
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_XFER_ALEN);
    CComboBox* pLen = (CComboBox*)GetDlgItem(IDC_XFER_LLEN);
    for (int n = 1; n <= 4; n++)
    {
        CString text;
        text.Format("%d", n);
        if (pAddr != NULL)
        {
            pAddr->AddString(text);
        }
        if (pLen != NULL)
        {
            pLen->AddString(text);
        }
    }
    if (pAddr != NULL)
    {
        pAddr->SetCurSel(3);
    }
    if (pLen != NULL)
    {
        pLen->SetCurSel(3);
    }
    SetDlgItemText(IDC_XFER_STATUS, "Download sends 34/36/37. Upload sends 35/36/37. Open this page after Connect.");
    UpdateProgress(0, 0);
    return TRUE;
}

CString CUDSTransferWnd::NowStamp() const
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    CString text;
    text.Format("%02u:%02u:%02u.%03u",
                (unsigned)st.wHour, (unsigned)st.wMinute,
                (unsigned)st.wSecond, (unsigned)st.wMilliseconds);
    return text;
}

void CUDSTransferWnd::UpdateProgress(int done, int total)
{
    CProgressCtrl* pProg = (CProgressCtrl*)GetDlgItem(IDC_XFER_PROG);
    if (total <= 0)
    {
        if (pProg != NULL)
        {
            pProg->SetRange32(0, 100);
            pProg->SetPos(0);
        }
        SetDlgItemText(IDC_XFER_PCT, "0%");
        return;
    }
    if (done < 0)
    {
        done = 0;
    }
    if (done > total)
    {
        done = total;
    }
    if (pProg != NULL)
    {
        pProg->SetRange32(0, total);
        pProg->SetPos(done);
    }
    int pct = (int)(((LONGLONG)done * 100) / total);
    if (pct < 100 && done > 0 && done < total && pct == 0)
    {
        pct = 1;
    }
    if (done >= total)
    {
        pct = 100;
    }
    CString text;
    text.Format("%d%%", pct);
    SetDlgItemText(IDC_XFER_PCT, text);
}

void CUDSTransferWnd::OnOK()
{
    DestroyWindow();
}

void CUDSTransferWnd::OnCancel()
{
    DestroyWindow();
}

void CUDSTransferWnd::OnDestroy()
{
    KillTimer(XFER_TIMER);
    FreeFile();
    CDialog::OnDestroy();
}

void CUDSTransferWnd::PostNcDestroy()
{
    if (g_pXfer == this)
    {
        g_pXfer = NULL;
    }
    CDialog::PostNcDestroy();
    delete this;
}

void CUDSTransferWnd::Log(const CString& text)
{
    CString line = text;
    line += "\r\n";
    CEdit* pEdit = (CEdit*)GetDlgItem(IDC_XFER_LOG);
    if (pEdit == NULL)
    {
        return;
    }
    int n = pEdit->GetWindowTextLength();
    pEdit->SetSel(n, n);
    pEdit->ReplaceSel(line);
}

void CUDSTransferWnd::SetBusy(BOOL busy)
{
    GetDlgItem(IDC_XFER_DOWN)->EnableWindow(!busy);
    GetDlgItem(IDC_XFER_UP)->EnableWindow(!busy);
    GetDlgItem(IDC_XFER_BROWSE)->EnableWindow(!busy);
}

void CUDSTransferWnd::FreeFile()
{
    delete[] m_file;
    m_file = NULL;
    m_fileLen = 0;
}

CString CUDSTransferWnd::Be(unsigned value, int n)
{
    CString text;
    for (int i = n - 1; i >= 0; i--)
    {
        CString one;
        one.Format("%02X", (value >> (8 * i)) & 0xFF);
        text += one;
    }
    return text;
}

CString CUDSTransferWnd::HexOf(const BYTE* data, int n)
{
    CString text;
    if (data == NULL || n <= 0)
    {
        return text;
    }
    char* buf = text.GetBuffer(n * 2 + 1);
    for (int i = 0; i < n; i++)
    {
        sprintf(buf + i * 2, "%02X", data[i]);
    }
    text.ReleaseBuffer(n * 2);
    return text;
}

BOOL CUDSTransferWnd::ReadUi(unsigned& addr, int& addrN, int& sizeN, BYTE& dfi, int& cap, unsigned& upSize)
{
    CString text;
    GetDlgItemText(IDC_XFER_ADDR, text);
    text.Trim();
    text.Replace(" ", "");
    if (text.Left(2).CompareNoCase("0x") == 0)
    {
        text = text.Mid(2);
    }
    char* end = NULL;
    addr = strtoul((LPCTSTR)text, &end, 16);
    if (text.IsEmpty() || end == NULL || *end != 0)
    {
        SetDlgItemText(IDC_XFER_STATUS, "Address must be hex");
        return FALSE;
    }
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_XFER_ALEN);
    CComboBox* pLen = (CComboBox*)GetDlgItem(IDC_XFER_LLEN);
    addrN = (pAddr != NULL) ? pAddr->GetCurSel() + 1 : 4;
    sizeN = (pLen != NULL) ? pLen->GetCurSel() + 1 : 4;
    if (addrN < 1 || addrN > 4)
    {
        addrN = 4;
    }
    if (sizeN < 1 || sizeN > 4)
    {
        sizeN = 4;
    }
    unsigned addrMax = (addrN >= 4) ? 0xFFFFFFFF : ((1u << (addrN * 8)) - 1u);
    if (addrN < 4 && addr > addrMax)
    {
        SetDlgItemText(IDC_XFER_STATUS, "Address does not fit in the address field");
        return FALSE;
    }
    GetDlgItemText(IDC_XFER_DFI, text);
    text.Trim();
    end = NULL;
    unsigned dfiV = strtoul((LPCTSTR)text, &end, 16);
    if (text.IsEmpty() || end == NULL || *end != 0 || dfiV > 0xFF)
    {
        SetDlgItemText(IDC_XFER_STATUS, "DFI must be one hex byte");
        return FALSE;
    }
    dfi = (BYTE)dfiV;
    GetDlgItemText(IDC_XFER_BLOCK, text);
    text.Trim();
    cap = atoi((LPCTSTR)text);
    if (cap < 0)
    {
        cap = 0;
    }
    GetDlgItemText(IDC_XFER_SIZE, text);
    text.Trim();
    upSize = (unsigned)strtoul((LPCTSTR)text, &end, 10);
    if (text.IsEmpty() || end == (char*)(LPCTSTR)text)
    {
        upSize = 0;
    }
    GetDlgItemText(IDC_XFER_FILE, m_path);
    m_path.Trim();
    return TRUE;
}

BOOL CUDSTransferWnd::LoadFile(const CString& path)
{
    FreeFile();
    CFile file;
    if (!file.Open(path, CFile::modeRead | CFile::typeBinary))
    {
        SetDlgItemText(IDC_XFER_STATUS, "Cannot open the file");
        return FALSE;
    }
    ULONGLONG len = file.GetLength();
    if (len == 0 || len > XFER_MAX)
    {
        SetDlgItemText(IDC_XFER_STATUS, "File must be 1 byte to 16 MB");
        return FALSE;
    }
    m_file = new BYTE[(size_t)len];
    if (file.Read(m_file, (UINT)len) != (UINT)len)
    {
        FreeFile();
        SetDlgItemText(IDC_XFER_STATUS, "Cannot read the file");
        return FALSE;
    }
    m_fileLen = (int)len;
    return TRUE;
}

BOOL CUDSTransferWnd::SendHex(const CString& hex)
{
    if (omMainWnd == NULL || !::IsWindow(omMainWnd->GetSafeHwnd()))
    {
        SetDlgItemText(IDC_XFER_STATUS, "Diagnostic window is not open");
        return FALSE;
    }
    m_waitSeq = DIL_UDS_ResponseSeq();
    omMainWnd->SetRequestPayload(hex);
    omMainWnd->OnEnChangeData();
    omMainWnd->OnBnClickedSendUD();
    m_sentAt = GetTickCount();
    SetTimer(XFER_TIMER, 50, NULL);
    return TRUE;
}

BOOL CUDSTransferWnd::TakeResponse()
{
    if (DIL_UDS_ResponseSeq() == m_waitSeq)
    {
        if (omMainWnd != NULL && omMainWnd->DatatoSend.GetLength() >= 2)
        {
            m_sentAt = GetTickCount();
            return FALSE;
        }
        if (GetTickCount() - m_sentAt > 8000)
        {
            Fail("No response in 8 seconds");
        }
        return FALSE;
    }
    m_rspLen = DIL_UDS_CopyResponse(m_rsp, (int)sizeof(m_rsp));
    if (m_rspLen < 0)
    {
        m_rspLen = 0;
    }
    if (m_rspLen > (int)sizeof(m_rsp))
    {
        Fail("Response is longer than 8192 bytes");
        return FALSE;
    }
    return TRUE;
}

void CUDSTransferWnd::Fail(const CString& text)
{
    KillTimer(XFER_TIMER);
    m_phase = PH_IDLE;
    SetBusy(FALSE);
    if (m_fileLen > 0)
    {
        UpdateProgress(m_off, m_fileLen);
    }
    Log(text);
    SetDlgItemText(IDC_XFER_STATUS, text);
}

void CUDSTransferWnd::Succeed(const CString& text)
{
    KillTimer(XFER_TIMER);
    m_phase = PH_IDLE;
    SetBusy(FALSE);
    if (m_fileLen > 0)
    {
        UpdateProgress(m_fileLen, m_fileLen);
    }
    Log(text);
    SetDlgItemText(IDC_XFER_STATUS, text);
}

void CUDSTransferWnd::SendNextDownload()
{
    if (m_off >= m_fileLen)
    {
        m_phase = PH_DL_77;
        Log("37 RequestTransferExit");
        if (!SendHex("37"))
        {
            Fail("Send failed");
        }
        return;
    }
    int n = m_block;
    if (n > m_fileLen - m_off)
    {
        n = m_fileLen - m_off;
    }
    CString req;
    req.Format("36%02X%s", m_seq & 0xFF, (LPCTSTR)HexOf(m_file + m_off, n));
    m_blockT0 = GetTickCount();
    CString note;
    note.Format("%s  36 start seq %02X  %d bytes  %d / %d",
                (LPCTSTR)NowStamp(), m_seq & 0xFF, n, m_off + n, m_fileLen);
    Log(note);
    UpdateProgress(m_off + n, m_fileLen);
    m_phase = PH_DL_76;
    if (!SendHex(req))
    {
        Fail("Send failed");
    }
}

void CUDSTransferWnd::SendNextUpload()
{
    if (m_off >= m_fileLen)
    {
        m_phase = PH_UL_77;
        Log("37 RequestTransferExit");
        if (!SendHex("37"))
        {
            Fail("Send failed");
        }
        return;
    }
    CString req;
    req.Format("36%02X", m_seq & 0xFF);
    m_blockT0 = GetTickCount();
    CString note;
    note.Format("%s  36 start seq %02X  got %d / %d",
                (LPCTSTR)NowStamp(), m_seq & 0xFF, m_off, m_fileLen);
    Log(note);
    UpdateProgress(m_off, m_fileLen);
    m_phase = PH_UL_76;
    if (!SendHex(req))
    {
        Fail("Send failed");
    }
}

void CUDSTransferWnd::OnResponse()
{
    if (m_rspLen >= 3 && m_rsp[0] == 0x7F)
    {
        CString text;
        text.Format("NRC %02X for service %02X", m_rsp[2], m_rsp[1]);
        Fail(text);
        return;
    }
    if (m_phase == PH_DL_74)
    {
        if (m_rspLen < 3 || m_rsp[0] != 0x74)
        {
            Fail("Expected positive response 74");
            return;
        }
        int nBytes = (m_rsp[1] >> 4) & 0x0F;
        if (nBytes < 1 || m_rspLen < 2 + nBytes)
        {
            Fail("74 is missing maxNumberOfBlockLength");
            return;
        }
        unsigned maxMsg = 0;
        for (int i = 0; i < nBytes; i++)
        {
            maxMsg = (maxMsg << 8) | m_rsp[2 + i];
        }
        int dataMax = (int)maxMsg - 2;
        if (dataMax < 1)
        {
            dataMax = 1;
        }
        int cap = 0;
        CString capText;
        GetDlgItemText(IDC_XFER_BLOCK, capText);
        cap = atoi((LPCTSTR)capText);
        if (cap > 0 && cap < dataMax)
        {
            dataMax = cap;
        }
        if (dataMax > 4093)
        {
            dataMax = 4093;
        }
        m_block = dataMax;
        m_off = 0;
        m_seq = 1;
        UpdateProgress(0, m_fileLen);
        CString note;
        note.Format("74 max block message %u, data per 36 is %d", maxMsg, m_block);
        Log(note);
        SendNextDownload();
        return;
    }
    if (m_phase == PH_DL_76)
    {
        if (m_rspLen < 2 || m_rsp[0] != 0x76 || m_rsp[1] != (BYTE)(m_seq & 0xFF))
        {
            Fail("Expected 76 with the same block sequence");
            return;
        }
        int n = m_block;
        if (n > m_fileLen - m_off)
        {
            n = m_fileLen - m_off;
        }
        DWORD elapsed = GetTickCount() - m_blockT0;
        CString note;
        note.Format("%s  36 end seq %02X  %d bytes  %lu ms",
                    (LPCTSTR)NowStamp(), m_seq & 0xFF, n, (unsigned long)elapsed);
        Log(note);
        m_off += n;
        UpdateProgress(m_off, m_fileLen);
        m_seq = (m_seq + 1) & 0xFF;
        SendNextDownload();
        return;
    }
    if (m_phase == PH_DL_77)
    {
        if (m_rspLen < 1 || m_rsp[0] != 0x77)
        {
            Fail("Expected positive response 77");
            return;
        }
        CString text;
        text.Format("Download finished, %d bytes", m_fileLen);
        Succeed(text);
        return;
    }
    if (m_phase == PH_UL_75)
    {
        if (m_rspLen < 3 || m_rsp[0] != 0x75)
        {
            Fail("Expected positive response 75");
            return;
        }
        int nBytes = (m_rsp[1] >> 4) & 0x0F;
        if (nBytes < 1 || m_rspLen < 2 + nBytes)
        {
            Fail("75 is missing maxNumberOfBlockLength");
            return;
        }
        unsigned maxMsg = 0;
        for (int i = 0; i < nBytes; i++)
        {
            maxMsg = (maxMsg << 8) | m_rsp[2 + i];
        }
        int dataMax = (int)maxMsg - 2;
        if (dataMax < 1)
        {
            dataMax = 1;
        }
        int cap = 0;
        CString capText;
        GetDlgItemText(IDC_XFER_BLOCK, capText);
        cap = atoi((LPCTSTR)capText);
        if (cap > 0 && cap < dataMax)
        {
            dataMax = cap;
        }
        if (dataMax > 4093)
        {
            dataMax = 4093;
        }
        m_block = dataMax;
        m_off = 0;
        m_seq = 1;
        UpdateProgress(0, m_fileLen);
        int need = m_fileLen;
        delete[] m_file;
        m_file = NULL;
        if (need < 1)
        {
            need = 1;
        }
        m_file = new BYTE[need];
        memset(m_file, 0, need);
        m_fileLen = need;
        CString note;
        note.Format("75 max block message %u, data per 36 is %d", maxMsg, m_block);
        Log(note);
        SendNextUpload();
        return;
    }
    if (m_phase == PH_UL_76)
    {
        if (m_rspLen < 2 || m_rsp[0] != 0x76 || m_rsp[1] != (BYTE)(m_seq & 0xFF))
        {
            Fail("Expected 76 with the same block sequence");
            return;
        }
        int n = m_rspLen - 2;
        if (n < 0)
        {
            n = 0;
        }
        if (m_off + n > m_fileLen)
        {
            n = m_fileLen - m_off;
        }
        if (n > 0)
        {
            memcpy(m_file + m_off, m_rsp + 2, n);
            m_off += n;
        }
        if (n == 0 && m_off < m_fileLen)
        {
            Fail("76 did not contain data");
            return;
        }
        DWORD elapsed = GetTickCount() - m_blockT0;
        CString note;
        note.Format("%s  36 end seq %02X  %d bytes  %lu ms  got %d / %d",
                    (LPCTSTR)NowStamp(), m_seq & 0xFF, n, (unsigned long)elapsed,
                    m_off, m_fileLen);
        Log(note);
        UpdateProgress(m_off, m_fileLen);
        m_seq = (m_seq + 1) & 0xFF;
        SendNextUpload();
        return;
    }
    if (m_phase == PH_UL_77)
    {
        if (m_rspLen < 1 || m_rsp[0] != 0x77)
        {
            Fail("Expected positive response 77");
            return;
        }
        CFile file;
        if (!file.Open(m_path, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
        {
            Fail("Cannot write the upload file");
            return;
        }
        if (m_off > 0)
        {
            file.Write(m_file, (UINT)m_off);
        }
        file.Close();
        CString text;
        text.Format("Upload finished, %d bytes written", m_off);
        Succeed(text);
    }
}

void CUDSTransferWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != XFER_TIMER || m_phase == PH_IDLE)
    {
        return;
    }
    if (!TakeResponse())
    {
        return;
    }
    OnResponse();
}

void CUDSTransferWnd::OnBrowse()
{
    CFileDialog dlg(TRUE, "bin", NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
                    "Binary (*.bin)|*.bin|All files (*.*)|*.*||", this);
    if (dlg.DoModal() == IDOK)
    {
        SetDlgItemText(IDC_XFER_FILE, dlg.GetPathName());
    }
}

void CUDSTransferWnd::OnDownload()
{
    if (m_phase != PH_IDLE)
    {
        return;
    }
    unsigned upSize = 0;
    int cap = 0;
    if (!ReadUi(m_addr, m_addrN, m_sizeN, m_dfi, cap, upSize))
    {
        return;
    }
    if (m_path.IsEmpty() || !LoadFile(m_path))
    {
        if (m_path.IsEmpty())
        {
            SetDlgItemText(IDC_XFER_STATUS, "Choose the file to download");
        }
        return;
    }
    unsigned sizeMax = (m_sizeN >= 4) ? 0xFFFFFFFF : ((1u << (m_sizeN * 8)) - 1u);
    if ((unsigned)m_fileLen > sizeMax)
    {
        SetDlgItemText(IDC_XFER_STATUS, "File length does not fit in the size field");
        return;
    }
    CString req;
    req.Format("34%02X%02X%s%s", m_dfi, (m_sizeN << 4) | m_addrN,
               (LPCTSTR)Be(m_addr, m_addrN), (LPCTSTR)Be((unsigned)m_fileLen, m_sizeN));
    Log("34 " + req);
    m_off = 0;
    UpdateProgress(0, m_fileLen);
    m_phase = PH_DL_74;
    SetBusy(TRUE);
    if (!SendHex(req))
    {
        Fail("Send failed");
    }
}

void CUDSTransferWnd::OnUpload()
{
    if (m_phase != PH_IDLE)
    {
        return;
    }
    FreeFile();
    unsigned upSize = 0;
    int cap = 0;
    if (!ReadUi(m_addr, m_addrN, m_sizeN, m_dfi, cap, upSize))
    {
        return;
    }
    if (upSize == 0 || upSize > XFER_MAX)
    {
        SetDlgItemText(IDC_XFER_STATUS, "Upload size must be 1 byte to 16 MB");
        return;
    }
    unsigned sizeMax = (m_sizeN >= 4) ? 0xFFFFFFFF : ((1u << (m_sizeN * 8)) - 1u);
    if (upSize > sizeMax)
    {
        SetDlgItemText(IDC_XFER_STATUS, "Upload size does not fit in the size field");
        return;
    }
    if (m_path.IsEmpty())
    {
        CFileDialog dlg(FALSE, "bin", "upload.bin", OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                        "Binary (*.bin)|*.bin|All files (*.*)|*.*||", this);
        if (dlg.DoModal() != IDOK)
        {
            return;
        }
        m_path = dlg.GetPathName();
        SetDlgItemText(IDC_XFER_FILE, m_path);
    }
    m_fileLen = (int)upSize;
    CString req;
    req.Format("35%02X%02X%s%s", m_dfi, (m_sizeN << 4) | m_addrN,
               (LPCTSTR)Be(m_addr, m_addrN), (LPCTSTR)Be(upSize, m_sizeN));
    Log("35 " + req);
    m_off = 0;
    UpdateProgress(0, m_fileLen);
    m_phase = PH_UL_75;
    SetBusy(TRUE);
    if (!SendHex(req))
    {
        Fail("Send failed");
    }
}

void CUDSTransferWnd::OnStop()
{
    if (m_phase == PH_IDLE)
    {
        return;
    }
    Fail("Stopped");
}

HRESULT UdsTransfer_Show(HWND hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (g_pXfer != NULL && !::IsWindow(g_pXfer->GetSafeHwnd()))
    {
        g_pXfer = NULL;
    }
    if (g_pXfer == NULL)
    {
        g_pXfer = new CUDSTransferWnd();
        CWnd* parent = CWnd::FromHandle(hParent);
        if (!g_pXfer->Create(IDD_UDS_TRANSFER, parent))
        {
            delete g_pXfer;
            g_pXfer = NULL;
            return E_FAIL;
        }
    }
    g_pXfer->ShowWindow(SW_SHOW);
    g_pXfer->SetForegroundWindow();
    return S_OK;
}
