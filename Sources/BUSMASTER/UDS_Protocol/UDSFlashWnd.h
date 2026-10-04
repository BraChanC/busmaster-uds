#pragma once

#include <afxcmn.h>
#include <vector>
#include "UDS_Resource.h"

struct SFwSeg
{
    UINT64 unAddr;
    std::vector<BYTE> aby;
};

struct SFwImage
{
    SFwImage() : bHasEntry(FALSE), unEntry(0), unTotal(0) {}
    std::vector<SFwSeg> asSeg;
    BOOL bHasEntry;
    UINT64 unEntry;
    UINT unTotal;
};

struct SUdsFlashStep
{
    BOOL bEnable;
    CString omName;
    CString omPayload;
    UINT unDelayMs;
    BOOL bWaitRsp;
    CString omFile;
    int nKind;
    int nAddrMode;
    int nErase;
    BYTE byDfi;
    UINT unBlkDelay;
    int nAddrSize;
    int nLenSize;
    CString omStartAddr;
    int nCrcType;
    DWORD dwPoly;
    DWORD dwInit;
    DWORD dwXor;
    BOOL bRefIn;
    BOOL bRefOut;
    int nCrcMode;
    CString omCrcCmd;
};

class CUDSFlashWnd : public CDialog
{
    DECLARE_DYNAMIC(CUDSFlashWnd)
public:
    CUDSFlashWnd(CWnd* pParent = nullptr);
    enum { IDD = IDD_UDS_FLASH };

protected:
    virtual BOOL OnInitDialog();
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual void OnCancel();
    virtual void OnOK();
    DECLARE_MESSAGE_MAP()

    afx_msg void OnClose();
    afx_msg void OnBnClickedAdd();
    afx_msg void OnBnClickedDel();
    afx_msg void OnBnClickedUp();
    afx_msg void OnBnClickedDown();
    afx_msg void OnBnClickedDefault();
    afx_msg void OnBnClickedImport();
    afx_msg void OnBnClickedExport();
    afx_msg void OnBnClickedStart();
    afx_msg void OnBnClickedStop();
    afx_msg void OnBnClickedInsert();
    afx_msg void OnBnClickedBrowse();
    afx_msg void OnBnClickedDlBrowse();
    afx_msg void OnBnClickedDlAdd();
    afx_msg void OnBnClickedSecBrowse();
    afx_msg void OnBnClickedAddTo();
    afx_msg void OnBnClickedClear();
    afx_msg void OnBnClickedAddDelay();
    afx_msg void OnBnClickedNew();
    afx_msg void OnBnClickedOpen();
    afx_msg void OnBnClickedSave();
    afx_msg void OnBnClickedExport2();
    afx_msg void OnBnClickedSaveEnc();
    afx_msg void OnBnClickedApply();
    afx_msg void OnBnClickedMore();
    afx_msg void OnBnClickedSvcEdit();
    afx_msg void OnLbnSelchangeService();
    afx_msg void OnCbnSelchangeSf();
    afx_msg void OnLvnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnEnChangeDetail();

    void LoadDefaultSteps();
    void RefreshList();
    void SaveEditorsToSel();
    void LoadEditorsFromSel();
    int GetSel() const;
    void PumpWait(UINT unMs);
    CString CleanHex(CString omHex) const;
    BOOL LoadFileBytes(const CString& omPath, CString& omHexOut, UINT& unSize);
    BOOL ParseFirmware(const CString& omPath, SFwImage& omImg);
    void MergeFwSegs(SFwImage& omImg) const;
    void ShowFwInfo(const SFwImage& omImg);
    BOOL ApplyFirmwarePath(const CString& omPath);
    CString BytesToHex(const BYTE* pby, UINT unLen) const;
    void SendPayload(const CString& omHex);
    void InitDlPanel();
    void ReadDlPanel(SUdsFlashStep& s);
    void WriteDlPanel(const SUdsFlashStep& s);
    void InitFileDownloadStep(SUdsFlashStep& s);
    BOOL RunFileDownload(const SUdsFlashStep& s);
    CString BeHex(UINT64 unVal, int nBytes) const;
    DWORD ParseHexU32(CString omText) const;
    DWORD Crc32(const BYTE* pby, UINT unLen, DWORD dwPoly, DWORD dwInit, DWORD dwXor, BOOL bRefIn, BOOL bRefOut) const;
    DWORD CrcReflect(DWORD dwVal, int nBits) const;
    BOOL HexToBytes(const CString& omHex, std::vector<BYTE>& aby) const;
    void FillServiceList();
    void UpdateServicePanel();
    CString BuildGenericPayload();
    CString WideToAcp(const wchar_t* pch) const;
    void ShowFileDownload(BOOL bShow);
    void ShowSecurity(BOOL bShow);
    void ShowGroup(UINT unGrpId, BOOL bShow);
    void ExportToPath(const CString& omPath);
    BOOL ImportFromPath(const CString& omPath);
    const struct UdsServiceDef* FindService(BYTE bySid) const;

    CListCtrl m_omList;
    CComboBox m_omComboTpl;
    CListBox m_omSvcList;
    CListCtrl m_omDlBlocks;
    SFwImage m_omFw;
    std::vector<SUdsFlashStep> m_asSteps;
    BOOL m_bUpdating;
    BOOL m_bRunning;
};
