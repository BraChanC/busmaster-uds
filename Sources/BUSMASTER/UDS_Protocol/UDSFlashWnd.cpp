#include "StdAfx.h"
#include <algorithm>
#include "UDSFlashWnd.h"
#include "UDSMainWnd.h"
#include "UDSIso14229.h"

extern "C" HRESULT DIL_UDS_ShowWnd(HWND hParent, int TotalChannels);
extern "C" HRESULT DIL_UDS_ShowSettingWnd(HWND hParent);

extern CUDSMainWnd* omMainWnd;

IMPLEMENT_DYNAMIC(CUDSFlashWnd, CDialog)

CUDSFlashWnd::CUDSFlashWnd(CWnd* pParent)
    : CDialog(CUDSFlashWnd::IDD, pParent)
    , m_bUpdating(FALSE)
    , m_bRunning(FALSE)
{
}

void CUDSFlashWnd::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_FLASH_LIST, m_omList);
    DDX_Control(pDX, IDC_FLASH_TEMPLATE, m_omComboTpl);
    DDX_Control(pDX, IDC_FL_SERVLIST, m_omSvcList);
    DDX_Control(pDX, IDC_DL_BLOCKS, m_omDlBlocks);
}

BEGIN_MESSAGE_MAP(CUDSFlashWnd, CDialog)
    ON_BN_CLICKED(IDC_FLASH_ADD, OnBnClickedAdd)
    ON_BN_CLICKED(IDC_FLASH_DEL, OnBnClickedDel)
    ON_BN_CLICKED(IDC_FLASH_UP, OnBnClickedUp)
    ON_BN_CLICKED(IDC_FLASH_DOWN, OnBnClickedDown)
    ON_BN_CLICKED(IDC_FLASH_DEFAULT, OnBnClickedDefault)
    ON_BN_CLICKED(IDC_FLASH_IMPORT, OnBnClickedImport)
    ON_BN_CLICKED(IDC_FLASH_EXPORT, OnBnClickedExport)
    ON_BN_CLICKED(IDC_FLASH_START, OnBnClickedStart)
    ON_BN_CLICKED(IDC_FLASH_STOP, OnBnClickedStop)
    ON_BN_CLICKED(IDC_FLASH_INSERT, OnBnClickedInsert)
    ON_BN_CLICKED(IDC_FLASH_BROWSE, OnBnClickedBrowse)
    ON_BN_CLICKED(IDC_DL_BROWSE, OnBnClickedDlBrowse)
    ON_BN_CLICKED(IDC_DL_ADD, OnBnClickedDlAdd)
    ON_BN_CLICKED(IDC_FL_SEC_BROWSE, OnBnClickedSecBrowse)
    ON_BN_CLICKED(IDC_FL_ADDTO, OnBnClickedAddTo)
    ON_BN_CLICKED(IDC_FL_CLEAR, OnBnClickedClear)
    ON_BN_CLICKED(IDC_FL_ADDDELAY, OnBnClickedAddDelay)
    ON_BN_CLICKED(IDC_FL_NEW, OnBnClickedNew)
    ON_BN_CLICKED(IDC_FL_OPEN, OnBnClickedOpen)
    ON_BN_CLICKED(IDC_FL_SAVE, OnBnClickedSave)
    ON_BN_CLICKED(IDC_FL_EXPORT2, OnBnClickedExport2)
    ON_BN_CLICKED(IDC_FL_SAVEENC, OnBnClickedSaveEnc)
    ON_BN_CLICKED(IDC_FL_APPLY, OnBnClickedApply)
    ON_BN_CLICKED(IDC_FL_MORE, OnBnClickedMore)
    ON_BN_CLICKED(IDC_FL_SVCEDIT, OnBnClickedSvcEdit)
    ON_LBN_SELCHANGE(IDC_FL_SERVLIST, OnLbnSelchangeService)
    ON_CBN_SELCHANGE(IDC_FL_SF, OnCbnSelchangeSf)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_FLASH_LIST, OnLvnItemChanged)
    ON_EN_CHANGE(IDC_FLASH_NAME, OnEnChangeDetail)
    ON_EN_CHANGE(IDC_FLASH_PAYLOAD, OnEnChangeDetail)
    ON_EN_CHANGE(IDC_FLASH_DELAY, OnEnChangeDetail)
    ON_EN_CHANGE(IDC_FLASH_FILE, OnEnChangeDetail)
    ON_BN_CLICKED(IDC_FLASH_WAIT, OnEnChangeDetail)
    ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CUDSFlashWnd::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_omList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_omList.InsertColumn(0, "#", LVCFMT_LEFT, 28);
    m_omList.InsertColumn(1, "Description", LVCFMT_LEFT, 150);
    m_omList.InsertColumn(2, "Data", LVCFMT_LEFT, 180);
    m_omComboTpl.AddString("DiagnosticSessionControl extended (1003)");
    m_omComboTpl.AddString("DiagnosticSessionControl programming (1002)");
    m_omComboTpl.AddString("SecurityAccess requestSeed (2701)");
    m_omComboTpl.AddString("CommunicationControl disableRxAndTx (280303)");
    m_omComboTpl.AddString("ControlDTCSetting off (8502)");
    m_omComboTpl.AddString("startRoutine eraseMemory (3101FF00)");
    m_omComboTpl.AddString("startRoutine checkProgrammingDependencies (3101FF01)");
    m_omComboTpl.AddString("RequestDownload ALFID44 (3400440000000000000004)");
    m_omComboTpl.AddString("TransferData block (3601)");
    m_omComboTpl.AddString("RequestTransferExit (37)");
    m_omComboTpl.AddString("ECUReset hardReset (1101)");
    m_omComboTpl.AddString("Custom (empty payload)");
    m_omComboTpl.SetCurSel(0);
    InitDlPanel();
    FillServiceList();
    LoadDefaultSteps();
    RefreshList();
    if (!m_asSteps.empty())
    {
        m_omList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        LoadEditorsFromSel();
    }
    return TRUE;
}

void CUDSFlashWnd::InitFileDownloadStep(SUdsFlashStep& s)
{
    s.bEnable = TRUE;
    s.omName = "File download";
    s.omPayload = "FILEDL";
    s.unDelayMs = 50;
    s.bWaitRsp = TRUE;
    s.nKind = 1;
    s.nAddrMode = 0;
    s.nErase = 0;
    s.byDfi = 0x00;
    s.unBlkDelay = 0;
    s.nAddrSize = 4;
    s.nLenSize = 4;
    s.omStartAddr = "00000000";
    s.nCrcType = 0;
    s.dwPoly = 0x04C11DB7;
    s.dwInit = 0xFFFFFFFF;
    s.dwXor = 0xFFFFFFFF;
    s.bRefIn = TRUE;
    s.bRefOut = TRUE;
    s.nCrcMode = 2;
    s.omCrcCmd = "31010202";
}

void CUDSFlashWnd::InitDlPanel()
{
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_DL_ADDRMODE);
    if (pAddr != nullptr)
    {
        pAddr->ResetContent();
        pAddr->AddString("Physical");
        pAddr->AddString("Functional");
        pAddr->SetCurSel(0);
    }
    CComboBox* pErase = (CComboBox*)GetDlgItem(IDC_DL_ERASE);
    if (pErase != nullptr)
    {
        pErase->ResetContent();
        pErase->AddString("None");
        pErase->AddString("eraseMemory 3101FF00");
        pErase->SetCurSel(0);
    }
    CComboBox* pAs = (CComboBox*)GetDlgItem(IDC_DL_ADDRSZ);
    CComboBox* pLs = (CComboBox*)GetDlgItem(IDC_DL_LENSZ);
    if (pAs != nullptr)
    {
        pAs->ResetContent();
        pAs->AddString("1");
        pAs->AddString("2");
        pAs->AddString("3");
        pAs->AddString("4");
        pAs->SetCurSel(3);
    }
    if (pLs != nullptr)
    {
        pLs->ResetContent();
        pLs->AddString("1");
        pLs->AddString("2");
        pLs->AddString("3");
        pLs->AddString("4");
        pLs->SetCurSel(3);
    }
    CComboBox* pCrc = (CComboBox*)GetDlgItem(IDC_DL_CRCTYPE);
    if (pCrc != nullptr)
    {
        pCrc->ResetContent();
        pCrc->AddString("CRC32");
        pCrc->AddString("None");
        pCrc->SetCurSel(0);
    }
    CComboBox* pCan = (CComboBox*)GetDlgItem(IDC_FL_CANTYPE);
    if (pCan != nullptr)
    {
        pCan->ResetContent();
        pCan->AddString("CAN");
        pCan->AddString("CANFD");
        pCan->SetCurSel(0);
    }
    CComboBox* pFlAddr = (CComboBox*)GetDlgItem(IDC_FL_ADDR);
    if (pFlAddr != nullptr)
    {
        pFlAddr->ResetContent();
        pFlAddr->AddString("Physical");
        pFlAddr->AddString("Functional");
        pFlAddr->SetCurSel(0);
    }
    CComboBox* pSup = (CComboBox*)GetDlgItem(IDC_FL_SUPPRESS);
    if (pSup != nullptr)
    {
        pSup->ResetContent();
        pSup->AddString("No");
        pSup->AddString("Yes");
        pSup->SetCurSel(0);
    }
    SetDlgItemText(IDC_FL_REQID, "700");
    SetDlgItemText(IDC_FL_FNID, "7DF");
    SetDlgItemText(IDC_FL_RESID, "701");
    SetDlgItemText(IDC_FL_DELAYMS, "1000");
    m_omDlBlocks.SetExtendedStyle(m_omDlBlocks.GetExtendedStyle() | LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    m_omDlBlocks.InsertColumn(0, "Block", LVCFMT_LEFT, 250);
    SUdsFlashStep s = {};
    InitFileDownloadStep(s);
    WriteDlPanel(s);
}

void CUDSFlashWnd::ReadDlPanel(SUdsFlashStep& s)
{
    GetDlgItemText(IDC_DL_DESC, s.omName);
    if (s.omName.IsEmpty())
    {
        s.omName = "File download";
    }
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_DL_ADDRMODE);
    s.nAddrMode = (pAddr != nullptr) ? pAddr->GetCurSel() : 0;
    if (s.nAddrMode < 0)
    {
        s.nAddrMode = 0;
    }
    CComboBox* pErase = (CComboBox*)GetDlgItem(IDC_DL_ERASE);
    s.nErase = (pErase != nullptr) ? pErase->GetCurSel() : 0;
    if (s.nErase < 0)
    {
        s.nErase = 0;
    }
    CString omDfi;
    GetDlgItemText(IDC_DL_DFI, omDfi);
    s.byDfi = (BYTE)ParseHexU32(omDfi);
    CString omBlk;
    GetDlgItemText(IDC_DL_BLKDELAY, omBlk);
    s.unBlkDelay = (UINT)atoi(omBlk);
    CComboBox* pAs = (CComboBox*)GetDlgItem(IDC_DL_ADDRSZ);
    CComboBox* pLs = (CComboBox*)GetDlgItem(IDC_DL_LENSZ);
    s.nAddrSize = (pAs != nullptr) ? (pAs->GetCurSel() + 1) : 4;
    s.nLenSize = (pLs != nullptr) ? (pLs->GetCurSel() + 1) : 4;
    if (s.nAddrSize < 1)
    {
        s.nAddrSize = 4;
    }
    if (s.nLenSize < 1)
    {
        s.nLenSize = 4;
    }
    GetDlgItemText(IDC_DL_STARTADDR, s.omStartAddr);
    GetDlgItemText(IDC_DL_PATH, s.omFile);
    CComboBox* pCrc = (CComboBox*)GetDlgItem(IDC_DL_CRCTYPE);
    s.nCrcType = (pCrc != nullptr) ? pCrc->GetCurSel() : 0;
    if (s.nCrcType < 0)
    {
        s.nCrcType = 0;
    }
    CString omPoly, omInit, omXor;
    GetDlgItemText(IDC_DL_POLY, omPoly);
    GetDlgItemText(IDC_DL_INIT, omInit);
    GetDlgItemText(IDC_DL_XOR, omXor);
    s.dwPoly = ParseHexU32(omPoly);
    s.dwInit = ParseHexU32(omInit);
    s.dwXor = ParseHexU32(omXor);
    CButton* pIn = (CButton*)GetDlgItem(IDC_DL_REFIN);
    CButton* pOut = (CButton*)GetDlgItem(IDC_DL_REFOUT);
    s.bRefIn = (pIn != nullptr && pIn->GetCheck() == BST_CHECKED);
    s.bRefOut = (pOut != nullptr && pOut->GetCheck() == BST_CHECKED);
    CButton* p37 = (CButton*)GetDlgItem(IDC_DL_CRC37);
    CButton* pF1 = (CButton*)GetDlgItem(IDC_DL_CRC31F1A0);
    s.nCrcMode = 2;
    if (p37 != nullptr && p37->GetCheck() == BST_CHECKED)
    {
        s.nCrcMode = 0;
    }
    else if (pF1 != nullptr && pF1->GetCheck() == BST_CHECKED)
    {
        s.nCrcMode = 1;
    }
    GetDlgItemText(IDC_DL_CRCCMD, s.omCrcCmd);
    s.omCrcCmd = CleanHex(s.omCrcCmd);
    s.nKind = 1;
    s.omPayload = "FILEDL";
    s.bEnable = TRUE;
    s.bWaitRsp = TRUE;
    if (s.unDelayMs == 0)
    {
        s.unDelayMs = 50;
    }
}

void CUDSFlashWnd::WriteDlPanel(const SUdsFlashStep& s)
{
    m_bUpdating = TRUE;
    SetDlgItemText(IDC_DL_DESC, s.omName);
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_DL_ADDRMODE);
    if (pAddr != nullptr)
    {
        pAddr->SetCurSel((s.nAddrMode == 1) ? 1 : 0);
    }
    CComboBox* pErase = (CComboBox*)GetDlgItem(IDC_DL_ERASE);
    if (pErase != nullptr)
    {
        pErase->SetCurSel((s.nErase == 1) ? 1 : 0);
    }
    CString om;
    om.Format("%02X", s.byDfi);
    SetDlgItemText(IDC_DL_DFI, om);
    om.Format("%u", s.unBlkDelay);
    SetDlgItemText(IDC_DL_BLKDELAY, om);
    CComboBox* pAs = (CComboBox*)GetDlgItem(IDC_DL_ADDRSZ);
    CComboBox* pLs = (CComboBox*)GetDlgItem(IDC_DL_LENSZ);
    if (pAs != nullptr)
    {
        int n = s.nAddrSize - 1;
        if (n < 0 || n > 3)
        {
            n = 3;
        }
        pAs->SetCurSel(n);
    }
    if (pLs != nullptr)
    {
        int n = s.nLenSize - 1;
        if (n < 0 || n > 3)
        {
            n = 3;
        }
        pLs->SetCurSel(n);
    }
    SetDlgItemText(IDC_DL_STARTADDR, s.omStartAddr.IsEmpty() ? "00000000" : s.omStartAddr);
    SetDlgItemText(IDC_DL_PATH, s.omFile);
    CComboBox* pCrc = (CComboBox*)GetDlgItem(IDC_DL_CRCTYPE);
    if (pCrc != nullptr)
    {
        pCrc->SetCurSel((s.nCrcType == 1) ? 1 : 0);
    }
    om.Format("0x%08X", s.dwPoly);
    SetDlgItemText(IDC_DL_POLY, om);
    om.Format("0x%08X", s.dwInit);
    SetDlgItemText(IDC_DL_INIT, om);
    om.Format("0x%08X", s.dwXor);
    SetDlgItemText(IDC_DL_XOR, om);
    CButton* pIn = (CButton*)GetDlgItem(IDC_DL_REFIN);
    CButton* pOut = (CButton*)GetDlgItem(IDC_DL_REFOUT);
    if (pIn != nullptr)
    {
        pIn->SetCheck(s.bRefIn ? BST_CHECKED : BST_UNCHECKED);
    }
    if (pOut != nullptr)
    {
        pOut->SetCheck(s.bRefOut ? BST_CHECKED : BST_UNCHECKED);
    }
    CButton* p37 = (CButton*)GetDlgItem(IDC_DL_CRC37);
    CButton* pF1 = (CButton*)GetDlgItem(IDC_DL_CRC31F1A0);
    CButton* pTot = (CButton*)GetDlgItem(IDC_DL_CRC310202);
    if (p37 != nullptr)
    {
        p37->SetCheck(s.nCrcMode == 0 ? BST_CHECKED : BST_UNCHECKED);
    }
    if (pF1 != nullptr)
    {
        pF1->SetCheck(s.nCrcMode == 1 ? BST_CHECKED : BST_UNCHECKED);
    }
    if (pTot != nullptr)
    {
        pTot->SetCheck(s.nCrcMode == 2 ? BST_CHECKED : BST_UNCHECKED);
    }
    SetDlgItemText(IDC_DL_CRCCMD, s.omCrcCmd.IsEmpty() ? "31010202" : s.omCrcCmd);
    ApplyFirmwarePath(s.omFile);
    m_bUpdating = FALSE;
}

void CUDSFlashWnd::LoadDefaultSteps()
{
    m_asSteps.clear();
    const struct
    {
        const char* pchName;
        const char* pchPayload;
        UINT unDelay;
    } kDef[] =
    {
        {"extendedDiagnosticSession", "1003", 50},
        {"checkProgrammingPreconditions", "31010203", 80},
        {"ControlDTCSetting off", "8502", 50},
        {"CommunicationControl disableRxAndTx", "280301", 50},
        {"programmingSession", "1002", 50},
        {"ECUReset hardReset", "1101", 100},
    };
    for (int i = 0; i < (int)(sizeof(kDef) / sizeof(kDef[0])); ++i)
    {
        SUdsFlashStep s = {};
        s.bEnable = TRUE;
        s.omName = kDef[i].pchName;
        s.omPayload = kDef[i].pchPayload;
        s.unDelayMs = kDef[i].unDelay;
        s.bWaitRsp = TRUE;
        m_asSteps.push_back(s);
    }
}

void CUDSFlashWnd::RefreshList()
{
    m_bUpdating = TRUE;
    const int nSel = GetSel();
    m_omList.DeleteAllItems();
    for (int i = 0; i < (int)m_asSteps.size(); ++i)
    {
        const SUdsFlashStep& s = m_asSteps[(size_t)i];
        CString om;
        om.Format("%d", i + 1);
        const int nItem = m_omList.InsertItem(i, om);
        m_omList.SetItemText(nItem, 1, s.omName);
        CString omData = s.omPayload;
        if (s.nKind == 1)
        {
            omData = s.omFile.IsEmpty() ? "FILEDL" : s.omFile;
        }
        else if (s.nKind == 3)
        {
            omData.Format("%s DLL:%s", (LPCTSTR)s.omPayload, (LPCTSTR)s.omFile);
        }
        m_omList.SetItemText(nItem, 2, omData);
    }
    if (nSel >= 0 && nSel < (int)m_asSteps.size())
    {
        m_omList.SetItemState(nSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_omList.EnsureVisible(nSel, FALSE);
    }
    m_bUpdating = FALSE;
}

int CUDSFlashWnd::GetSel() const
{
    POSITION pos = m_omList.GetFirstSelectedItemPosition();
    if (pos == nullptr)
    {
        return -1;
    }
    return m_omList.GetNextSelectedItem(pos);
}

void CUDSFlashWnd::SaveEditorsToSel()
{
    const int nSel = GetSel();
    if (nSel < 0 || nSel >= (int)m_asSteps.size())
    {
        return;
    }
    SUdsFlashStep& s = m_asSteps[(size_t)nSel];
    GetDlgItemText(IDC_FLASH_NAME, s.omName);
    GetDlgItemText(IDC_FLASH_PAYLOAD, s.omPayload);
    s.omPayload = CleanHex(s.omPayload);
    CString omDelay;
    GetDlgItemText(IDC_FLASH_DELAY, omDelay);
    s.unDelayMs = (UINT)atoi(omDelay);
    CButton* pWait = (CButton*)GetDlgItem(IDC_FLASH_WAIT);
    s.bWaitRsp = (pWait != nullptr && pWait->GetCheck() == BST_CHECKED);
    if (s.nKind == 1)
    {
        const CString omNameKeep = s.omName;
        const UINT unDelayKeep = s.unDelayMs;
        const BOOL bWaitKeep = s.bWaitRsp;
        ReadDlPanel(s);
        if (!omNameKeep.IsEmpty())
        {
            s.omName = omNameKeep;
        }
        s.unDelayMs = unDelayKeep;
        s.bWaitRsp = bWaitKeep;
        s.omPayload = "FILEDL";
    }
    else
    {
        GetDlgItemText(IDC_FLASH_FILE, s.omFile);
    }
    if (nSel < m_omList.GetItemCount())
    {
        s.bEnable = m_omList.GetCheck(nSel);
        m_omList.SetItemText(nSel, 1, s.omName);
        m_omList.SetItemText(nSel, 2, s.omPayload);
        CString om;
        om.Format("%u", s.unDelayMs);
        m_omList.SetItemText(nSel, 3, om);
        m_omList.SetItemText(nSel, 4, s.bWaitRsp ? "Yes" : "No");
        m_omList.SetItemText(nSel, 5, s.omFile);
    }
}

void CUDSFlashWnd::LoadEditorsFromSel()
{
    const int nSel = GetSel();
    if (nSel < 0 || nSel >= (int)m_asSteps.size())
    {
        return;
    }
    m_bUpdating = TRUE;
    const SUdsFlashStep& s = m_asSteps[(size_t)nSel];
    SetDlgItemText(IDC_FLASH_NAME, s.omName);
    SetDlgItemText(IDC_FLASH_PAYLOAD, s.omPayload);
    CString om;
    om.Format("%u", s.unDelayMs);
    SetDlgItemText(IDC_FLASH_DELAY, om);
    CButton* pWait = (CButton*)GetDlgItem(IDC_FLASH_WAIT);
    if (pWait != nullptr)
    {
        pWait->SetCheck(s.bWaitRsp ? BST_CHECKED : BST_UNCHECKED);
    }
    SetDlgItemText(IDC_FLASH_FILE, s.omFile);
    m_bUpdating = FALSE;
    if (s.nKind == 1)
    {
        WriteDlPanel(s);
    }
}

void CUDSFlashWnd::OnEnChangeDetail()
{
    if (!m_bUpdating)
    {
        SaveEditorsToSel();
    }
}

void CUDSFlashWnd::OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNm = (LPNMLISTVIEW)pNMHDR;
    if (!m_bUpdating && pNm != nullptr)
    {
        if ((pNm->uChanged & LVIF_STATE) && (pNm->uNewState & LVIS_SELECTED))
        {
            LoadEditorsFromSel();
        }
        if (pNm->iItem >= 0 && pNm->iItem < (int)m_asSteps.size())
        {
            m_asSteps[(size_t)pNm->iItem].bEnable = TRUE;
        }
    }
    *pResult = 0;
}

void CUDSFlashWnd::OnBnClickedAdd()
{
    SaveEditorsToSel();
    SUdsFlashStep s = {};
    s.bEnable = TRUE;
    s.omName = "Custom step";
    s.unDelayMs = 50;
    s.bWaitRsp = TRUE;
    m_asSteps.push_back(s);
    RefreshList();
    m_omList.SetItemState((int)m_asSteps.size() - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    LoadEditorsFromSel();
}

void CUDSFlashWnd::OnBnClickedDlAdd()
{
    SaveEditorsToSel();
    SUdsFlashStep s = {};
    InitFileDownloadStep(s);
    ReadDlPanel(s);
    m_asSteps.push_back(s);
    RefreshList();
    m_omList.SetItemState((int)m_asSteps.size() - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    LoadEditorsFromSel();
}

void CUDSFlashWnd::OnBnClickedDel()
{
    const int nSel = GetSel();
    if (nSel < 0 || nSel >= (int)m_asSteps.size())
    {
        return;
    }
    m_asSteps.erase(m_asSteps.begin() + nSel);
    RefreshList();
}

void CUDSFlashWnd::OnBnClickedUp()
{
    SaveEditorsToSel();
    const int nSel = GetSel();
    if (nSel <= 0)
    {
        return;
    }
    std::swap(m_asSteps[(size_t)nSel], m_asSteps[(size_t)nSel - 1]);
    RefreshList();
    m_omList.SetItemState(nSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CUDSFlashWnd::OnBnClickedDown()
{
    SaveEditorsToSel();
    const int nSel = GetSel();
    if (nSel < 0 || nSel + 1 >= (int)m_asSteps.size())
    {
        return;
    }
    std::swap(m_asSteps[(size_t)nSel], m_asSteps[(size_t)nSel + 1]);
    RefreshList();
    m_omList.SetItemState(nSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CUDSFlashWnd::OnBnClickedDefault()
{
    LoadDefaultSteps();
    RefreshList();
    if (!m_asSteps.empty())
    {
        m_omList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        LoadEditorsFromSel();
    }
}

void CUDSFlashWnd::OnBnClickedInsert()
{
    SaveEditorsToSel();
    const int nSel = m_omComboTpl.GetCurSel();
    struct STpl { const char* pchName; const char* pchPayload; };
    static const STpl kTpl[] =
    {
        {"DiagnosticSessionControl extended", "1003"},
        {"DiagnosticSessionControl programming", "1002"},
        {"SecurityAccess requestSeed", "2701"},
        {"CommunicationControl disableRxAndTx", "280303"},
        {"ControlDTCSetting off", "8502"},
        {"startRoutine eraseMemory", "3101FF00"},
        {"startRoutine checkProgrammingDependencies", "3101FF01"},
        {"RequestDownload ALFID44", "3400440000000000000004"},
        {"TransferData block", "3601"},
        {"RequestTransferExit", "37"},
        {"ECUReset hardReset", "1101"},
        {"Custom step", ""},
    };
    const int nIdx = (nSel >= 0 && nSel < (int)(sizeof(kTpl) / sizeof(kTpl[0]))) ? nSel : 11;
    SUdsFlashStep s = {};
    s.bEnable = TRUE;
    s.omName = kTpl[nIdx].pchName;
    s.omPayload = kTpl[nIdx].pchPayload;
    s.unDelayMs = 50;
    s.bWaitRsp = TRUE;
    const int nAt = GetSel();
    if (nAt >= 0 && nAt < (int)m_asSteps.size())
    {
        m_asSteps.insert(m_asSteps.begin() + nAt + 1, s);
        RefreshList();
        m_omList.SetItemState(nAt + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    else
    {
        m_asSteps.push_back(s);
        RefreshList();
        m_omList.SetItemState((int)m_asSteps.size() - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    LoadEditorsFromSel();
}

void CUDSFlashWnd::OnBnClickedBrowse()
{
    CFileDialog omDlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Firmware (*.hex;*.bin)|*.hex;*.bin|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() == IDOK)
    {
        SetDlgItemText(IDC_FLASH_FILE, omDlg.GetPathName());
        SaveEditorsToSel();
    }
}

void CUDSFlashWnd::OnBnClickedDlBrowse()
{
    CFileDialog omDlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Firmware (*.hex;*.s19;*.s28;*.s37;*.mot;*.srec;*.bin)|*.hex;*.s19;*.s28;*.s37;*.mot;*.srec;*.bin|"
                      "Intel HEX (*.hex)|*.hex|"
                      "Motorola S-record (*.s19;*.s28;*.s37;*.mot;*.srec)|*.s19;*.s28;*.s37;*.mot;*.srec|"
                      "Binary (*.bin)|*.bin|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() == IDOK)
    {
        SetDlgItemText(IDC_DL_PATH, omDlg.GetPathName());
        if (!ApplyFirmwarePath(omDlg.GetPathName()))
        {
            AfxMessageBox("Failed to parse firmware file.");
        }
        const int nSel = GetSel();
        if (nSel >= 0 && nSel < (int)m_asSteps.size() && m_asSteps[(size_t)nSel].nKind == 1)
        {
            SaveEditorsToSel();
        }
    }
}

void CUDSFlashWnd::OnBnClickedSecBrowse()
{
    CFileDialog omDlg(TRUE, "dll", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "Seed&Key DLL (*.dll)|*.dll|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() == IDOK)
    {
        SetDlgItemText(IDC_FL_SEC_DLL, omDlg.GetPathName());
    }
}

CString CUDSFlashWnd::CleanHex(CString omHex) const
{
    omHex.Replace(" ", "");
    omHex.Replace("\r", "");
    omHex.Replace("\n", "");
    omHex.MakeUpper();
    return omHex;
}

CString CUDSFlashWnd::BeHex(UINT64 unVal, int nBytes) const
{
    if (nBytes < 1)
    {
        nBytes = 1;
    }
    if (nBytes > 8)
    {
        nBytes = 8;
    }
    CString om;
    for (int i = nBytes - 1; i >= 0; --i)
    {
        CString omB;
        omB.Format("%02X", (unsigned)((unVal >> (8 * i)) & 0xFF));
        om += omB;
    }
    return om;
}

DWORD CUDSFlashWnd::ParseHexU32(CString omText) const
{
    omText.Trim();
    omText.Replace("0x", "");
    omText.Replace("0X", "");
    return (DWORD)strtoul(omText, nullptr, 16);
}

DWORD CUDSFlashWnd::CrcReflect(DWORD dwVal, int nBits) const
{
    DWORD dwR = 0;
    for (int i = 0; i < nBits; ++i)
    {
        if (dwVal & (1u << i))
        {
            dwR |= 1u << (nBits - 1 - i);
        }
    }
    return dwR;
}

DWORD CUDSFlashWnd::Crc32(const BYTE* pby, UINT unLen, DWORD dwPoly, DWORD dwInit, DWORD dwXor, BOOL bRefIn, BOOL bRefOut) const
{
    DWORD dwPolyUse = bRefIn ? CrcReflect(dwPoly, 32) : dwPoly;
    DWORD dwCrc = dwInit;
    for (UINT i = 0; i < unLen; ++i)
    {
        if (bRefIn)
        {
            dwCrc ^= pby[i];
            for (int b = 0; b < 8; ++b)
            {
                dwCrc = (dwCrc & 1) ? ((dwCrc >> 1) ^ dwPolyUse) : (dwCrc >> 1);
            }
        }
        else
        {
            dwCrc ^= ((DWORD)pby[i] << 24);
            for (int b = 0; b < 8; ++b)
            {
                dwCrc = (dwCrc & 0x80000000) ? ((dwCrc << 1) ^ dwPolyUse) : (dwCrc << 1);
            }
        }
    }
    if (bRefIn != bRefOut)
    {
        dwCrc = CrcReflect(dwCrc, 32);
    }
    return dwCrc ^ dwXor;
}

BOOL CUDSFlashWnd::HexToBytes(const CString& omHex, std::vector<BYTE>& aby) const
{
    CString om = CleanHex(omHex);
    if (om.GetLength() % 2 != 0)
    {
        return FALSE;
    }
    aby.resize((size_t)(om.GetLength() / 2));
    for (int i = 0; i < om.GetLength(); i += 2)
    {
        aby[(size_t)(i / 2)] = (BYTE)strtol(om.Mid(i, 2), nullptr, 16);
    }
    return TRUE;
}

void CUDSFlashWnd::PumpWait(UINT unMs)
{
    const DWORD dwStart = GetTickCount();
    while (m_bRunning && (GetTickCount() - dwStart) < unMs)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_bRunning = FALSE;
                PostQuitMessage((int)msg.wParam);
                return;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
}

namespace
{
int FwNibble(TCHAR ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    return -1;
}

BOOL FwReadBytes(const CString& om, int nPos, int nCount, std::vector<BYTE>& aby)
{
    aby.clear();
    if (nCount < 0 || om.GetLength() < nPos + nCount * 2)
    {
        return FALSE;
    }
    aby.resize((size_t)nCount);
    for (int i = 0; i < nCount; ++i)
    {
        const int nHi = FwNibble(om[nPos + i * 2]);
        const int nLo = FwNibble(om[nPos + i * 2 + 1]);
        if (nHi < 0 || nLo < 0)
        {
            return FALSE;
        }
        aby[(size_t)i] = (BYTE)((nHi << 4) | nLo);
    }
    return TRUE;
}

void FwAppend(SFwImage& omImg, UINT64 unAddr, const BYTE* pby, int nLen)
{
    if (pby == nullptr || nLen <= 0)
    {
        return;
    }
    if (!omImg.asSeg.empty())
    {
        SFwSeg& omLast = omImg.asSeg.back();
        if (omLast.unAddr + omLast.aby.size() == unAddr)
        {
            omLast.aby.insert(omLast.aby.end(), pby, pby + nLen);
            return;
        }
    }
    SFwSeg omSeg;
    omSeg.unAddr = unAddr;
    omSeg.aby.assign(pby, pby + nLen);
    omImg.asSeg.push_back(omSeg);
}

BOOL ParseIntelHexText(CStdioFile& omFile, SFwImage& omImg)
{
    UINT32 unExtLin = 0;
    UINT32 unExtSeg = 0;
    CString omLine;
    while (omFile.ReadString(omLine))
    {
        omLine.Trim();
        if (omLine.GetLength() < 11 || omLine[0] != ':')
        {
            continue;
        }
        std::vector<BYTE> abyHdr;
        if (!FwReadBytes(omLine, 1, 4, abyHdr))
        {
            continue;
        }
        const int nRecLen = abyHdr[0];
        const UINT unOff = ((UINT)abyHdr[1] << 8) | abyHdr[2];
        const int nType = abyHdr[3];
        std::vector<BYTE> abyData;
        if (!FwReadBytes(omLine, 9, nRecLen, abyData))
        {
            continue;
        }
        if (nType == 0)
        {
            const UINT64 unAddr = ((UINT64)unExtLin << 16) + ((UINT64)unExtSeg << 4) + unOff;
            if (!abyData.empty())
            {
                FwAppend(omImg, unAddr, &abyData[0], (int)abyData.size());
            }
        }
        else if (nType == 1)
        {
            break;
        }
        else if (nType == 2 && abyData.size() >= 2)
        {
            unExtSeg = ((UINT)abyData[0] << 8) | abyData[1];
            unExtLin = 0;
        }
        else if (nType == 3 && abyData.size() >= 4)
        {
            const UINT unCs = ((UINT)abyData[0] << 8) | abyData[1];
            const UINT unIp = ((UINT)abyData[2] << 8) | abyData[3];
            omImg.bHasEntry = TRUE;
            omImg.unEntry = ((UINT64)unCs << 4) + unIp;
        }
        else if (nType == 4 && abyData.size() >= 2)
        {
            unExtLin = ((UINT)abyData[0] << 8) | abyData[1];
            unExtSeg = 0;
        }
        else if (nType == 5 && abyData.size() >= 4)
        {
            omImg.bHasEntry = TRUE;
            omImg.unEntry = ((UINT64)abyData[0] << 24) | ((UINT64)abyData[1] << 16) |
                            ((UINT64)abyData[2] << 8) | abyData[3];
        }
    }
    return !omImg.asSeg.empty();
}

BOOL ParseSRecordText(CStdioFile& omFile, SFwImage& omImg)
{
    CString omLine;
    while (omFile.ReadString(omLine))
    {
        omLine.Trim();
        if (omLine.GetLength() < 4 || (omLine[0] != 'S' && omLine[0] != 's'))
        {
            continue;
        }
        const TCHAR chType = omLine[1];
        std::vector<BYTE> abyCnt;
        if (!FwReadBytes(omLine, 2, 1, abyCnt))
        {
            continue;
        }
        const int nCount = abyCnt[0];
        std::vector<BYTE> abyRest;
        if (!FwReadBytes(omLine, 4, nCount, abyRest) || abyRest.size() < 3)
        {
            continue;
        }
        int nAddrBytes = 0;
        BOOL bData = FALSE;
        BOOL bEntry = FALSE;
        if (chType == '1')
        {
            nAddrBytes = 2;
            bData = TRUE;
        }
        else if (chType == '2')
        {
            nAddrBytes = 3;
            bData = TRUE;
        }
        else if (chType == '3')
        {
            nAddrBytes = 4;
            bData = TRUE;
        }
        else if (chType == '7')
        {
            nAddrBytes = 4;
            bEntry = TRUE;
        }
        else if (chType == '8')
        {
            nAddrBytes = 3;
            bEntry = TRUE;
        }
        else if (chType == '9')
        {
            nAddrBytes = 2;
            bEntry = TRUE;
        }
        else
        {
            continue;
        }
        if ((int)abyRest.size() < nAddrBytes + 1)
        {
            continue;
        }
        UINT64 unAddr = 0;
        for (int i = 0; i < nAddrBytes; ++i)
        {
            unAddr = (unAddr << 8) | abyRest[(size_t)i];
        }
        if (bEntry)
        {
            omImg.bHasEntry = TRUE;
            omImg.unEntry = unAddr;
            continue;
        }
        const int nData = (int)abyRest.size() - nAddrBytes - 1;
        if (bData && nData > 0)
        {
            FwAppend(omImg, unAddr, &abyRest[(size_t)nAddrBytes], nData);
        }
    }
    return !omImg.asSeg.empty();
}
}

static bool FwSegLess(const SFwSeg& a, const SFwSeg& b)
{
    return a.unAddr < b.unAddr;
}

void CUDSFlashWnd::MergeFwSegs(SFwImage& omImg) const
{
    omImg.unTotal = 0;
    if (omImg.asSeg.empty())
    {
        return;
    }
    if (omImg.asSeg.size() >= 2)
    {
        std::sort(omImg.asSeg.begin(), omImg.asSeg.end(),
                  FwSegLess);
        std::vector<SFwSeg> asOut;
        asOut.push_back(omImg.asSeg[0]);
        for (size_t i = 1; i < omImg.asSeg.size(); ++i)
        {
            SFwSeg& omLast = asOut.back();
            const SFwSeg& omCur = omImg.asSeg[i];
            const UINT64 unEnd = omLast.unAddr + omLast.aby.size();
            if (omCur.unAddr <= unEnd)
            {
                const UINT64 unSkip = unEnd - omCur.unAddr;
                if (unSkip < omCur.aby.size())
                {
                    omLast.aby.insert(omLast.aby.end(), omCur.aby.begin() + (size_t)unSkip, omCur.aby.end());
                }
            }
            else
            {
                asOut.push_back(omCur);
            }
        }
        omImg.asSeg.swap(asOut);
    }
    for (size_t i = 0; i < omImg.asSeg.size(); ++i)
    {
        omImg.unTotal += (UINT)omImg.asSeg[i].aby.size();
    }
}

BOOL CUDSFlashWnd::ParseFirmware(const CString& omPath, SFwImage& omImg)
{
    omImg = SFwImage();
    CString omLow = omPath;
    omLow.MakeLower();
    const BOOL bHexExt = (omLow.Right(4) == ".hex");
    const BOOL bSrecExt = (omLow.Right(4) == ".s19" || omLow.Right(4) == ".s28" ||
                           omLow.Right(4) == ".s37" || omLow.Right(4) == ".mot" ||
                           omLow.Right(5) == ".srec" || omLow.Right(3) == ".s1" ||
                           omLow.Right(3) == ".s2" || omLow.Right(3) == ".s3");
    if (bHexExt || bSrecExt)
    {
        CStdioFile omFile;
        if (!omFile.Open(omPath, CFile::modeRead | CFile::typeText))
        {
            return FALSE;
        }
        const BOOL bOk = bHexExt ? ParseIntelHexText(omFile, omImg) : ParseSRecordText(omFile, omImg);
        omFile.Close();
        if (!bOk)
        {
            return FALSE;
        }
        MergeFwSegs(omImg);
        return omImg.unTotal > 0;
    }

    CFile omPeek;
    if (!omPeek.Open(omPath, CFile::modeRead | CFile::typeBinary))
    {
        return FALSE;
    }
    BYTE byFirst = 0;
    const UINT nRead = omPeek.Read(&byFirst, 1);
    omPeek.Close();
    if (nRead == 1 && (byFirst == ':' || byFirst == 'S' || byFirst == 's'))
    {
        CStdioFile omFile;
        if (!omFile.Open(omPath, CFile::modeRead | CFile::typeText))
        {
            return FALSE;
        }
        const BOOL bOk = (byFirst == ':') ? ParseIntelHexText(omFile, omImg) : ParseSRecordText(omFile, omImg);
        omFile.Close();
        if (!bOk)
        {
            return FALSE;
        }
        MergeFwSegs(omImg);
        return omImg.unTotal > 0;
    }

    CFile omBin;
    if (!omBin.Open(omPath, CFile::modeRead | CFile::typeBinary))
    {
        return FALSE;
    }
    const UINT nLen = (UINT)omBin.GetLength();
    if (nLen == 0)
    {
        omBin.Close();
        return FALSE;
    }
    SFwSeg omSeg;
    CString omStart;
    GetDlgItemText(IDC_DL_STARTADDR, omStart);
    omSeg.unAddr = _strtoui64(CleanHex(omStart), nullptr, 16);
    omSeg.aby.resize(nLen);
    omBin.Read(&omSeg.aby[0], nLen);
    omBin.Close();
    omImg.asSeg.push_back(omSeg);
    omImg.unTotal = nLen;
    return TRUE;
}

void CUDSFlashWnd::ShowFwInfo(const SFwImage& omImg)
{
    m_omDlBlocks.DeleteAllItems();
    for (int i = 0; i < (int)omImg.asSeg.size(); ++i)
    {
        CString omItem;
        omItem.Format("%d. addr 0x%X, length %u", i + 1,
                      (UINT)omImg.asSeg[(size_t)i].unAddr,
                      (UINT)omImg.asSeg[(size_t)i].aby.size());
        const int nRow = m_omDlBlocks.InsertItem(i, omItem);
        m_omDlBlocks.SetCheck(nRow, TRUE);
    }
    if (!omImg.asSeg.empty())
    {
        SetDlgItemText(IDC_DL_STARTADDR, BeHex(omImg.asSeg[0].unAddr, 4));
    }
    CString omLen;
    omLen.Format("%u", omImg.unTotal);
    SetDlgItemText(IDC_DL_LEN, omLen);
    if (omImg.bHasEntry)
    {
        SetDlgItemText(IDC_DL_ENTRY, BeHex(omImg.unEntry, 4));
    }
    else
    {
        SetDlgItemText(IDC_DL_ENTRY, "");
    }
}

BOOL CUDSFlashWnd::ApplyFirmwarePath(const CString& omPath)
{
    if (omPath.IsEmpty())
    {
        m_omFw = SFwImage();
        ShowFwInfo(m_omFw);
        SetDlgItemText(IDC_DL_LEN, "0");
        SetDlgItemText(IDC_DL_ENTRY, "");
        return FALSE;
    }
    SFwImage omImg;
    if (!ParseFirmware(omPath, omImg))
    {
        m_omFw = SFwImage();
        ShowFwInfo(m_omFw);
        return FALSE;
    }
    m_omFw = omImg;
    ShowFwInfo(m_omFw);
    return TRUE;
}

CString CUDSFlashWnd::BytesToHex(const BYTE* pby, UINT unLen) const
{
    CString omOut;
    for (UINT i = 0; i < unLen; ++i)
    {
        CString omB;
        omB.Format("%02X", pby[i]);
        omOut += omB;
    }
    return omOut;
}

BOOL CUDSFlashWnd::LoadFileBytes(const CString& omPath, CString& omHexOut, UINT& unSize)
{
    omHexOut.Empty();
    unSize = 0;
    SFwImage omImg;
    if (!ParseFirmware(omPath, omImg))
    {
        return FALSE;
    }
    for (size_t i = 0; i < omImg.asSeg.size(); ++i)
    {
        if (!omImg.asSeg[i].aby.empty())
        {
            omHexOut += BytesToHex(&omImg.asSeg[i].aby[0], (UINT)omImg.asSeg[i].aby.size());
        }
        unSize += (UINT)omImg.asSeg[i].aby.size();
    }
    return unSize > 0;
}

void CUDSFlashWnd::SendPayload(const CString& omHex)
{
    if (omMainWnd == nullptr)
    {
        AfxMessageBox("UDS main window is not available.");
        return;
    }
    omMainWnd->SetRequestPayload(omHex);
    omMainWnd->OnEnChangeData();
    omMainWnd->OnBnClickedSendUD();
}

BOOL CUDSFlashWnd::RunFileDownload(const SUdsFlashStep& s)
{
    if (omMainWnd == nullptr)
    {
        return FALSE;
    }
    SFwImage omImg;
    if (s.omFile.IsEmpty() || !ParseFirmware(s.omFile, omImg))
    {
        omMainWnd->AppendLog("Flash: cannot load download file " + s.omFile);
        return FALSE;
    }
    std::vector<SFwSeg> asUse;
    const int nListed = m_omDlBlocks.GetItemCount();
    for (int i = 0; i < (int)omImg.asSeg.size(); ++i)
    {
        if (nListed == (int)omImg.asSeg.size() && m_omDlBlocks.GetCheck(i) == FALSE)
        {
            continue;
        }
        asUse.push_back(omImg.asSeg[(size_t)i]);
    }
    if (asUse.empty())
    {
        omMainWnd->AppendLog("Flash: no firmware blocks selected");
        return FALSE;
    }
    const int nOldFn = omMainWnd->m_omCheckFunctional.GetCheck();
    omMainWnd->m_omCheckFunctional.SetCheck(s.nAddrMode == 1 ? BST_CHECKED : BST_UNCHECKED);

    if (s.nErase == 1)
    {
        omMainWnd->AppendLog("Flash eraseMemory 3101FF00");
        SendPayload("3101FF00");
        PumpWait(s.unDelayMs ? s.unDelayMs : 200);
    }

    const int nAddr = (s.nAddrSize >= 1 && s.nAddrSize <= 4) ? s.nAddrSize : 4;
    const int nLen = (s.nLenSize >= 1 && s.nLenSize <= 4) ? s.nLenSize : 4;
    const BYTE byAlfid = (BYTE)((nLen << 4) | nAddr);
    const int nChunk = 128;
    const UINT unBlkWait = s.unBlkDelay ? s.unBlkDelay : 20;
    std::vector<BYTE> abyAll;

    for (size_t nSeg = 0; nSeg < asUse.size() && m_bRunning; ++nSeg)
    {
        const SFwSeg& omSeg = asUse[nSeg];
        if (omSeg.aby.empty())
        {
            continue;
        }
        abyAll.insert(abyAll.end(), omSeg.aby.begin(), omSeg.aby.end());
        const CString omFileHex = BytesToHex(&omSeg.aby[0], (UINT)omSeg.aby.size());
        CString om34;
        om34.Format("34%02X%02X%s%s", s.byDfi, byAlfid,
                    (LPCTSTR)BeHex(omSeg.unAddr, nAddr), (LPCTSTR)BeHex((UINT64)omSeg.aby.size(), nLen));
        omMainWnd->AppendLog("RequestDownload " + om34);
        SendPayload(om34);
        PumpWait(s.unDelayMs ? s.unDelayMs : 80);
        if (!m_bRunning)
        {
            break;
        }

        int nSeq = 1;
        for (int nOff = 0; nOff < (int)omFileHex.GetLength() && m_bRunning; nOff += nChunk * 2)
        {
            int nTake = nChunk * 2;
            if (nTake > (int)omFileHex.GetLength() - nOff)
            {
                nTake = (int)omFileHex.GetLength() - nOff;
            }
            CString omBlk;
            omBlk.Format("36%02X%s", nSeq & 0xFF, (LPCTSTR)omFileHex.Mid(nOff, nTake));
            SendPayload(omBlk);
            PumpWait(unBlkWait);

            if (s.nCrcType == 0 && (s.nCrcMode == 0 || s.nCrcMode == 1) && m_bRunning)
            {
                std::vector<BYTE> abyBlk;
                HexToBytes(omFileHex.Mid(nOff, nTake), abyBlk);
                const DWORD dwCrc = Crc32(abyBlk.empty() ? nullptr : &abyBlk[0], (UINT)abyBlk.size(),
                                          s.dwPoly, s.dwInit, s.dwXor, s.bRefIn, s.bRefOut);
                const CString omCrc = BeHex(dwCrc, 4);
                if (s.nCrcMode == 0)
                {
                    SendPayload("37" + omCrc);
                }
                else
                {
                    SendPayload("3101F1A0" + omCrc);
                }
                PumpWait(s.unDelayMs ? s.unDelayMs : 50);
            }
            nSeq = (nSeq + 1) & 0xFF;
            if (nSeq == 0)
            {
                nSeq = 1;
            }
        }

        if (m_bRunning && s.nCrcMode != 0)
        {
            SendPayload("37");
            PumpWait(s.unDelayMs ? s.unDelayMs : 80);
        }
    }

    if (m_bRunning && s.nCrcMode == 2)
    {
        CString omCmd = CleanHex(s.omCrcCmd);
        if (omCmd.IsEmpty())
        {
            omCmd = "31010202";
        }
        if (s.nCrcType == 0 && !abyAll.empty())
        {
            const DWORD dwCrc = Crc32(&abyAll[0], (UINT)abyAll.size(),
                                      s.dwPoly, s.dwInit, s.dwXor, s.bRefIn, s.bRefOut);
            omCmd += BeHex(dwCrc, 4);
        }
        omMainWnd->AppendLog("CRC total " + omCmd);
        SendPayload(omCmd);
        PumpWait(s.unDelayMs ? s.unDelayMs : 80);
    }

    omMainWnd->m_omCheckFunctional.SetCheck(nOldFn);
    return m_bRunning;
}

void CUDSFlashWnd::OnBnClickedStart()
{
    SaveEditorsToSel();
    if (omMainWnd == nullptr)
    {
        AfxMessageBox("Open the UDS Main Window first.");
        return;
    }
    m_bRunning = TRUE;
    GetDlgItem(IDC_FLASH_START)->EnableWindow(FALSE);
    for (int i = 0; i < (int)m_asSteps.size() && m_bRunning; ++i)
    {
        SUdsFlashStep s = m_asSteps[(size_t)i];
        if (!s.bEnable)
        {
            continue;
        }
        CString omLog;
        omLog.Format("Flash step %d: %s  %s", i + 1, (LPCTSTR)s.omName, (LPCTSTR)s.omPayload);
        omMainWnd->AppendLog(omLog);
        if (s.nKind == 2)
        {
            PumpWait(s.unDelayMs ? s.unDelayMs : 1000);
            continue;
        }
        if (s.nKind == 3)
        {
            if (!s.omFile.IsEmpty())
            {
                omMainWnd->m_omEditSeedKeyDll.SetWindowText(s.omFile);
                CString omErr;
                if (!omMainWnd->m_omSeedKeyDll.Load(s.omFile, omErr))
                {
                    omMainWnd->AppendLog("Seed&Key DLL load failed: " + omErr);
                }
            }
            omMainWnd->m_omEditSeedKeyVariant.SetWindowText(s.omCrcCmd);
            SendPayload(CleanHex(s.omPayload));
            PumpWait(s.unDelayMs > 200 ? s.unDelayMs : 500);
            continue;
        }
        if (s.nKind == 1)
        {
            RunFileDownload(s);
            continue;
        }
        CString omPayload = CleanHex(s.omPayload);
        if (!s.omFile.IsEmpty())
        {
            CString omFileHex;
            UINT unSize = 0;
            if (!LoadFileBytes(s.omFile, omFileHex, unSize))
            {
                omMainWnd->AppendLog("Flash: cannot load file " + s.omFile);
                continue;
            }
            if (omPayload.GetLength() >= 2 && omPayload.Left(2) == "34")
            {
                omPayload.Format("340044%08X%08X", 0, unSize);
                SendPayload(omPayload);
            }
            else if (omPayload.GetLength() >= 2 && omPayload.Left(2) == "36")
            {
                const int nChunk = 128;
                int nSeq = 1;
                for (int nOff = 0; nOff < (int)omFileHex.GetLength() && m_bRunning; nOff += nChunk * 2)
                {
                    int nTake = nChunk * 2;
                    if (nTake > (int)omFileHex.GetLength() - nOff)
                    {
                        nTake = (int)omFileHex.GetLength() - nOff;
                    }
                    CString omBlk;
                    omBlk.Format("36%02X%s", nSeq & 0xFF, (LPCTSTR)omFileHex.Mid(nOff, nTake));
                    SendPayload(omBlk);
                    PumpWait(s.unDelayMs ? s.unDelayMs : 20);
                    nSeq = (nSeq + 1) & 0xFF;
                    if (nSeq == 0)
                    {
                        nSeq = 1;
                    }
                }
                continue;
            }
            else
            {
                SendPayload(omPayload);
            }
        }
        else if (!omPayload.IsEmpty())
        {
            SendPayload(omPayload);
        }
        UINT unWait = s.unDelayMs;
        if (s.bWaitRsp && unWait < 50)
        {
            unWait = 50;
        }
        PumpWait(unWait);
    }
    if (omMainWnd != nullptr)
    {
        omMainWnd->AppendLog(m_bRunning ? "Flash sequence finished" : "Flash sequence stopped");
    }
    m_bRunning = FALSE;
    GetDlgItem(IDC_FLASH_START)->EnableWindow(TRUE);
}

void CUDSFlashWnd::OnBnClickedStop()
{
    m_bRunning = FALSE;
}

void CUDSFlashWnd::OnBnClickedImport()
{
    CFileDialog omDlg(TRUE, "xml", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                      "ECU flash XML (*.xml)|*.xml|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CStdioFile omFile;
    if (!omFile.Open(omDlg.GetPathName(), CFile::modeRead | CFile::typeText))
    {
        return;
    }
    m_asSteps.clear();
    CString omLine;
    while (omFile.ReadString(omLine))
    {
        if (omLine.Find("<Step ") < 0)
        {
            continue;
        }
        SUdsFlashStep s = {};
        s.bEnable = omLine.Find("enable=\"0\"") < 0;
        auto fnAttr = [&](const char* pch) -> CString
        {
            CString omKey;
            omKey.Format("%s=\"", pch);
            const int nA = omLine.Find(omKey);
            if (nA < 0)
            {
                return "";
            }
            const int nB = nA + omKey.GetLength();
            const int nC = omLine.Find('\"', nB);
            if (nC < 0)
            {
                return "";
            }
            return omLine.Mid(nB, nC - nB);
        };
        s.omName = fnAttr("name");
        s.omPayload = fnAttr("payload");
        s.unDelayMs = (UINT)atoi(fnAttr("delay"));
        s.bWaitRsp = fnAttr("wait") != "0";
        s.omFile = fnAttr("file");
        s.nKind = atoi(fnAttr("kind"));
        s.nAddrMode = atoi(fnAttr("addrmode"));
        s.nErase = atoi(fnAttr("erase"));
        s.byDfi = (BYTE)ParseHexU32(fnAttr("dfi"));
        s.unBlkDelay = (UINT)atoi(fnAttr("blkdelay"));
        s.nAddrSize = atoi(fnAttr("addrsz"));
        s.nLenSize = atoi(fnAttr("lensz"));
        s.omStartAddr = fnAttr("start");
        s.nCrcType = atoi(fnAttr("crctype"));
        CString omPoly = fnAttr("poly");
        CString omInit = fnAttr("init");
        CString omXor = fnAttr("xor");
        s.dwPoly = omPoly.IsEmpty() ? 0x04C11DB7 : ParseHexU32(omPoly);
        s.dwInit = omInit.IsEmpty() ? 0xFFFFFFFF : ParseHexU32(omInit);
        s.dwXor = omXor.IsEmpty() ? 0xFFFFFFFF : ParseHexU32(omXor);
        s.bRefIn = fnAttr("refin") != "0";
        s.bRefOut = fnAttr("refout") != "0";
        s.nCrcMode = atoi(fnAttr("crcmode"));
        s.omCrcCmd = fnAttr("crccmd");
        if (s.nKind == 1)
        {
            if (s.nAddrSize < 1)
            {
                s.nAddrSize = 4;
            }
            if (s.nLenSize < 1)
            {
                s.nLenSize = 4;
            }
            if (s.omCrcCmd.IsEmpty())
            {
                s.omCrcCmd = "31010202";
            }
            s.omPayload = "FILEDL";
        }
        m_asSteps.push_back(s);
    }
    omFile.Close();
    RefreshList();
}

void CUDSFlashWnd::OnBnClickedExport()
{
    SaveEditorsToSel();
    CFileDialog omDlg(FALSE, "xml", "ecu_flash.xml", OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                      "ECU flash XML (*.xml)|*.xml|All files (*.*)|*.*||", this);
    if (omDlg.DoModal() != IDOK)
    {
        return;
    }
    CStdioFile omFile;
    if (!omFile.Open(omDlg.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::typeText))
    {
        return;
    }
    omFile.WriteString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<EcuFlash>\n");
    for (size_t i = 0; i < m_asSteps.size(); ++i)
    {
        const SUdsFlashStep& s = m_asSteps[i];
        CString om;
        om.Format("  <Step enable=\"%d\" name=\"%s\" payload=\"%s\" delay=\"%u\" wait=\"%d\" file=\"%s\""
                  " kind=\"%d\" addrmode=\"%d\" erase=\"%d\" dfi=\"%02X\" blkdelay=\"%u\" addrsz=\"%d\" lensz=\"%d\""
                  " start=\"%s\" crctype=\"%d\" poly=\"%08X\" init=\"%08X\" xor=\"%08X\" refin=\"%d\" refout=\"%d\""
                  " crcmode=\"%d\" crccmd=\"%s\"/>\n",
                  s.bEnable ? 1 : 0, (LPCTSTR)s.omName, (LPCTSTR)s.omPayload,
                  s.unDelayMs, s.bWaitRsp ? 1 : 0, (LPCTSTR)s.omFile,
                  s.nKind, s.nAddrMode, s.nErase, s.byDfi, s.unBlkDelay, s.nAddrSize, s.nLenSize,
                  (LPCTSTR)s.omStartAddr, s.nCrcType, s.dwPoly, s.dwInit, s.dwXor,
                  s.bRefIn ? 1 : 0, s.bRefOut ? 1 : 0, s.nCrcMode, (LPCTSTR)s.omCrcCmd);
        omFile.WriteString(om);
    }
    omFile.WriteString("</EcuFlash>\n");
    omFile.Close();
}

void CUDSFlashWnd::OnCancel()
{
    ShowWindow(SW_HIDE);
}

void CUDSFlashWnd::OnOK()
{
    ShowWindow(SW_HIDE);
}

void CUDSFlashWnd::OnClose()
{
    ShowWindow(SW_HIDE);
}

CString CUDSFlashWnd::WideToAcp(const wchar_t* pch) const
{
    if (pch == nullptr)
    {
        return "";
    }
    const int n = WideCharToMultiByte(CP_ACP, 0, pch, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
    {
        return "";
    }
    CString om;
    WideCharToMultiByte(CP_ACP, 0, pch, -1, om.GetBuffer(n), n, nullptr, nullptr);
    om.ReleaseBuffer();
    return om;
}

const UdsServiceDef* CUDSFlashWnd::FindService(BYTE bySid) const
{
    for (int i = 0; i < kIso14229ServiceCount; ++i)
    {
        if (kIso14229Services[i].m_bySid == bySid)
        {
            return &kIso14229Services[i];
        }
    }
    return nullptr;
}

void CUDSFlashWnd::FillServiceList()
{
    m_omSvcList.ResetContent();
    static const struct
    {
        BYTE bySid;
        const char* pch;
    } kSvc[] =
    {
        {0x10, "(10) DiagnosticSessionControl"},
        {0x11, "(11) ECUReset"},
        {0x14, "(14) ClearDiagnosticInformation"},
        {0x19, "(19) ReadDTCInformation"},
        {0x22, "(22) ReadDataByIdentifier"},
        {0x23, "(23) ReadMemoryByAddress"},
        {0x24, "(24) ReadScalingDataByIdentifier"},
        {0x27, "(27) SecurityAccess"},
        {0x28, "(28) CommunicationControl"},
        {0x2A, "(2A) ReadDataByPeriodicIdentifier"},
        {0x2C, "(2C) DynamicallyDefineDataIdentifier"},
        {0x2E, "(2E) WriteDataByIdentifier"},
        {0x2F, "(2F) InputOutputControlByIdentifier"},
        {0x31, "(31) RoutineControl"},
        {0x34, "(34) RequestDownload"},
        {0x3D, "(3D) WriteMemoryByAddress"},
        {0x83, "(83) AccessTimingParameter"},
        {0x84, "(84) SecuredDataTransmission"},
        {0x85, "(85) ControlDTCSetting"},
        {0x86, "(86) ResponseOnEvent"},
        {0x87, "(87) LinkControl"},
    };
    for (int i = 0; i < (int)(sizeof(kSvc) / sizeof(kSvc[0])); ++i)
    {
        const int n = m_omSvcList.AddString(kSvc[i].pch);
        m_omSvcList.SetItemData(n, kSvc[i].bySid);
    }
    m_omSvcList.SetCurSel(0);
    UpdateServicePanel();
}

void CUDSFlashWnd::ShowFileDownload(BOOL bShow)
{
    ShowGroup(IDC_DL_GROUP, bShow);
}

void CUDSFlashWnd::ShowSecurity(BOOL bShow)
{
    ShowGroup(IDC_FL_SEC_GROUP, bShow);
}

void CUDSFlashWnd::ShowGroup(UINT unGrpId, BOOL bShow)
{
    const int nCmd = bShow ? SW_SHOW : SW_HIDE;
    CWnd* pGrp = GetDlgItem(unGrpId);
    if (pGrp != nullptr)
    {
        pGrp->ShowWindow(nCmd);
    }
    CRect rcGrp(0, 0, 0, 0);
    if (pGrp != nullptr)
    {
        pGrp->GetWindowRect(&rcGrp);
        ScreenToClient(&rcGrp);
        rcGrp.InflateRect(2, 2);
    }
    for (CWnd* p = GetWindow(GW_CHILD); p != nullptr; p = p->GetNextWindow())
    {
        if (p == pGrp)
        {
            continue;
        }
        CRect rc;
        p->GetWindowRect(&rc);
        ScreenToClient(&rc);
        const CPoint pt = rc.CenterPoint();
        if (rcGrp.PtInRect(pt))
        {
            p->ShowWindow(nCmd);
        }
    }
}

void CUDSFlashWnd::UpdateServicePanel()
{
    const int nSel = m_omSvcList.GetCurSel();
    if (nSel < 0)
    {
        return;
    }
    const BYTE bySid = (BYTE)m_omSvcList.GetItemData(nSel);
    CString omSid;
    omSid.Format("%02X", bySid);
    SetDlgItemText(IDC_FL_SID, omSid);
    CString omName;
    m_omSvcList.GetText(nSel, omName);
    SetDlgItemText(IDC_DL_DESC, omName);
    CComboBox* pSf = (CComboBox*)GetDlgItem(IDC_FL_SF);
    if (pSf == nullptr)
    {
        return;
    }
    pSf->ResetContent();
    const UdsServiceDef* ps = FindService(bySid);
    if (ps != nullptr)
    {
        for (int i = 0; i < ps->m_nSub; ++i)
        {
            CString om;
            om.Format("%02X %s", ps->m_psSub[i].m_bySf, ps->m_psSub[i].m_pchName);
            const int n = pSf->AddString(om);
            pSf->SetItemData(n, (DWORD)i);
        }
        pSf->SetCurSel(0);
        if (ps->m_nSub > 0)
        {
            SetDlgItemText(IDC_FL_DATA, ps->m_psSub[0].m_pchExtra);
        }
        pSf->EnableWindow(ps->m_nSub > 0 ? TRUE : FALSE);
    }
    else
    {
        SetDlgItemText(IDC_FL_DATA, "");
    }
    ShowFileDownload(bySid == 0x34);
    ShowSecurity(bySid == 0x27);
}

void CUDSFlashWnd::OnLbnSelchangeService()
{
    UpdateServicePanel();
}

void CUDSFlashWnd::OnCbnSelchangeSf()
{
    CString omSid;
    GetDlgItemText(IDC_FL_SID, omSid);
    const UdsServiceDef* ps = FindService((BYTE)ParseHexU32(omSid));
    CComboBox* pSf = (CComboBox*)GetDlgItem(IDC_FL_SF);
    if (ps == nullptr || pSf == nullptr || pSf->GetCurSel() < 0)
    {
        return;
    }
    const int nIdx = (int)pSf->GetItemData(pSf->GetCurSel());
    if (nIdx >= 0 && nIdx < ps->m_nSub)
    {
        SetDlgItemText(IDC_FL_DATA, ps->m_psSub[nIdx].m_pchExtra);
    }
}

CString CUDSFlashWnd::BuildGenericPayload()
{
    CString omSid;
    GetDlgItemText(IDC_FL_SID, omSid);
    const BYTE bySid = (BYTE)ParseHexU32(omSid);
    CString omData;
    GetDlgItemText(IDC_FL_DATA, omData);
    omData = CleanHex(omData);
    const UdsServiceDef* ps = FindService(bySid);
    CComboBox* pSf = (CComboBox*)GetDlgItem(IDC_FL_SF);
    CComboBox* pSup = (CComboBox*)GetDlgItem(IDC_FL_SUPPRESS);
    const BOOL bSup = (pSup != nullptr && pSup->GetCurSel() == 1);
    CString om;
    if (ps != nullptr && ps->m_bEmitSf && pSf != nullptr && pSf->GetCurSel() >= 0)
    {
        const int nIdx = (int)pSf->GetItemData(pSf->GetCurSel());
        DWORD dwSf = 0;
        if (nIdx >= 0 && nIdx < ps->m_nSub)
        {
            dwSf = ps->m_psSub[nIdx].m_bySf;
        }
        if (bSup)
        {
            dwSf |= 0x80;
        }
        om.Format("%02X%02X%s", bySid, (BYTE)dwSf, (LPCTSTR)omData);
    }
    else
    {
        om.Format("%02X%s", bySid, (LPCTSTR)omData);
    }
    return om;
}

void CUDSFlashWnd::OnBnClickedAddTo()
{
    CString omSid;
    GetDlgItemText(IDC_FL_SID, omSid);
    const BYTE bySid = (BYTE)ParseHexU32(omSid);
    SUdsFlashStep s = {};
    s.bEnable = TRUE;
    s.bWaitRsp = TRUE;
    s.unDelayMs = 50;
    GetDlgItemText(IDC_DL_DESC, s.omName);
    CComboBox* pAddr = (CComboBox*)GetDlgItem(IDC_FL_ADDR);
    s.nAddrMode = (pAddr != nullptr && pAddr->GetCurSel() == 1) ? 1 : 0;
    if (bySid == 0x34)
    {
        InitFileDownloadStep(s);
        ReadDlPanel(s);
        GetDlgItemText(IDC_DL_DESC, s.omName);
        s.nAddrMode = (pAddr != nullptr && pAddr->GetCurSel() == 1) ? 1 : 0;
        s.omPayload = "FILEDL";
        s.nKind = 1;
    }
    else if (bySid == 0x27)
    {
        s.omPayload = BuildGenericPayload();
        GetDlgItemText(IDC_FL_SEC_DLL, s.omFile);
        GetDlgItemText(IDC_FL_SEC_VARIANT, s.omCrcCmd);
        s.nKind = 3;
        s.unDelayMs = 500;
    }
    else
    {
        s.omPayload = BuildGenericPayload();
        s.nKind = 0;
    }
    m_asSteps.push_back(s);
    RefreshList();
    m_omList.SetItemState((int)m_asSteps.size() - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CUDSFlashWnd::OnBnClickedClear()
{
    m_asSteps.clear();
    RefreshList();
}

void CUDSFlashWnd::OnBnClickedAddDelay()
{
    CString omMs;
    GetDlgItemText(IDC_FL_DELAYMS, omMs);
    SUdsFlashStep s = {};
    s.bEnable = TRUE;
    s.nKind = 2;
    s.unDelayMs = (UINT)atoi(omMs);
    if (s.unDelayMs == 0)
    {
        s.unDelayMs = 1000;
    }
    s.omName.Format("Delay %u ms", s.unDelayMs);
    m_asSteps.push_back(s);
    RefreshList();
}

void CUDSFlashWnd::OnBnClickedNew()
{
    LoadDefaultSteps();
    RefreshList();
}

void CUDSFlashWnd::OnBnClickedOpen()
{
    OnBnClickedImport();
}

void CUDSFlashWnd::OnBnClickedSave()
{
    OnBnClickedExport();
}

void CUDSFlashWnd::OnBnClickedExport2()
{
    OnBnClickedExport();
}

void CUDSFlashWnd::OnBnClickedSaveEnc()
{
    OnBnClickedExport();
}

void CUDSFlashWnd::OnBnClickedApply()
{
    OnBnClickedStart();
}

void CUDSFlashWnd::OnBnClickedMore()
{
    CWnd* pMain = AfxGetMainWnd();
    DIL_UDS_ShowSettingWnd(pMain != nullptr ? pMain->m_hWnd : m_hWnd);
}

void CUDSFlashWnd::OnBnClickedSvcEdit()
{
    CWnd* pMain = AfxGetMainWnd();
    LPARAM lParam = 1;
    DIL_UDS_ShowWnd(pMain != nullptr ? pMain->m_hWnd : m_hWnd, (int)lParam);
}
