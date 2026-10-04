/**
 * Vector CANoe/CANalyzer Seed&Key DLL (GenerateKeyEx / GenerateKeyExOpt).
 */
#pragma once

enum VKeyGenResultEx
{
    KGRE_Ok = 0,
    KGRE_BufferToSmall = 1,
    KGRE_SecurityLevelInvalid = 2,
    KGRE_VariantInvalid = 3,
    KGRE_UnspecifiedError = 4
};

class CVectorSeedKeyDll
{
public:
    CVectorSeedKeyDll();
    ~CVectorSeedKeyDll();

    void Unload();
    BOOL Load(const CString& omPath, CString& omError);
    BOOL IsLoaded() const;
    CString GetPath() const;

    BOOL GenerateKey(const BYTE* pbySeed, UINT unSeedLen,
                     UINT unSecurityLevel, const char* pchVariant,
                     BYTE* pbyKey, UINT unMaxKeyLen, UINT& unKeyLen,
                     CString& omError);

private:
    HMODULE m_hDll;
    CString m_omPath;
    FARPROC m_pfnExCdecl;
    FARPROC m_pfnExOptCdecl;
    FARPROC m_pfnExStd;
    FARPROC m_pfnExOptStd;
    FARPROC m_pfnLegacy;

    BOOL BindExports(CString& omError);
};
