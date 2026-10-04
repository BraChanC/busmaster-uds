#include "stdafx.h"
#include "VectorSeedKey.h"

typedef int (__cdecl* PFN_GenerateKeyExCdecl)(
    const unsigned char* ipSeedArray,
    unsigned int iSeedArraySize,
    const unsigned int iSecurityLevel,
    const char* ipVariant,
    unsigned char* iopKeyArray,
    unsigned int iMaxKeyArraySize,
    unsigned int* oActualKeyArraySize);

typedef int (__cdecl* PFN_GenerateKeyExOptCdecl)(
    const unsigned char* ipSeedArray,
    unsigned int iSeedArraySize,
    const unsigned int iSecurityLevel,
    const char* ipVariant,
    const char* ipOptions,
    unsigned char* iopKeyArray,
    unsigned int iMaxKeyArraySize,
    unsigned int* oActualKeyArraySize);

typedef int (__stdcall* PFN_GenerateKeyExStd)(
    const unsigned char* ipSeedArray,
    unsigned int iSeedArraySize,
    const unsigned int iSecurityLevel,
    const char* ipVariant,
    unsigned char* iopKeyArray,
    unsigned int iMaxKeyArraySize,
    unsigned int* oActualKeyArraySize);

typedef int (__stdcall* PFN_GenerateKeyExOptStd)(
    const unsigned char* ipSeedArray,
    unsigned int iSeedArraySize,
    const unsigned int iSecurityLevel,
    const char* ipVariant,
    const char* ipOptions,
    unsigned char* iopKeyArray,
    unsigned int iMaxKeyArraySize,
    unsigned int* oActualKeyArraySize);

typedef int (__cdecl* PFN_GenerateKeyLegacy)(
    unsigned char* ipSeedArray,
    unsigned short iSeedArraySize,
    unsigned char* iopKeyArray,
    unsigned short iMaxKeyArraySize,
    unsigned short* oActualKeyArraySize);

static CString VectorResultText(int nResult)
{
    switch (nResult)
    {
        case KGRE_Ok:
            return "KGRE_Ok";
        case KGRE_BufferToSmall:
            return "KGRE_BufferToSmall";
        case KGRE_SecurityLevelInvalid:
            return "KGRE_SecurityLevelInvalid";
        case KGRE_VariantInvalid:
            return "KGRE_VariantInvalid";
        case KGRE_UnspecifiedError:
            return "KGRE_UnspecifiedError";
        default:
        {
            CString om;
            om.Format("result=%d", nResult);
            return om;
        }
    }
}

CVectorSeedKeyDll::CVectorSeedKeyDll()
    : m_hDll(nullptr)
    , m_pfnExCdecl(nullptr)
    , m_pfnExOptCdecl(nullptr)
    , m_pfnExStd(nullptr)
    , m_pfnExOptStd(nullptr)
    , m_pfnLegacy(nullptr)
{
}

CVectorSeedKeyDll::~CVectorSeedKeyDll()
{
    Unload();
}

void CVectorSeedKeyDll::Unload()
{
    if (m_hDll != nullptr)
    {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
    }
    m_omPath.Empty();
    m_pfnExCdecl = nullptr;
    m_pfnExOptCdecl = nullptr;
    m_pfnExStd = nullptr;
    m_pfnExOptStd = nullptr;
    m_pfnLegacy = nullptr;
}

BOOL CVectorSeedKeyDll::IsLoaded() const
{
    return m_hDll != nullptr;
}

CString CVectorSeedKeyDll::GetPath() const
{
    return m_omPath;
}

BOOL CVectorSeedKeyDll::BindExports(CString& omError)
{
    m_pfnExOptCdecl = GetProcAddress(m_hDll, "GenerateKeyExOpt");
    m_pfnExCdecl = GetProcAddress(m_hDll, "GenerateKeyEx");
    m_pfnExOptStd = GetProcAddress(m_hDll, "_GenerateKeyExOpt@32");
    m_pfnExStd = GetProcAddress(m_hDll, "_GenerateKeyEx@28");
    m_pfnLegacy = GetProcAddress(m_hDll, "GenerateKey");
    if (m_pfnExOptCdecl == nullptr && m_pfnExCdecl == nullptr &&
        m_pfnExOptStd == nullptr && m_pfnExStd == nullptr && m_pfnLegacy == nullptr)
    {
        omError = "DLL has no Vector Seed&Key export (GenerateKeyEx / GenerateKeyExOpt / GenerateKey)";
        return FALSE;
    }
    return TRUE;
}

BOOL CVectorSeedKeyDll::Load(const CString& omPath, CString& omError)
{
    omError.Empty();
    if (omPath.IsEmpty())
    {
        omError = "Seed&Key DLL path is empty";
        return FALSE;
    }
    if (m_hDll != nullptr && m_omPath.CompareNoCase(omPath) == 0)
    {
        return TRUE;
    }
    Unload();
    m_hDll = LoadLibrary(omPath);
    if (m_hDll == nullptr)
    {
        omError.Format("LoadLibrary failed, GetLastError=%u", GetLastError());
        return FALSE;
    }
    if (!BindExports(omError))
    {
        Unload();
        return FALSE;
    }
    m_omPath = omPath;
    return TRUE;
}

BOOL CVectorSeedKeyDll::GenerateKey(const BYTE* pbySeed, UINT unSeedLen,
                                    UINT unSecurityLevel, const char* pchVariant,
                                    BYTE* pbyKey, UINT unMaxKeyLen, UINT& unKeyLen,
                                    CString& omError)
{
    unKeyLen = 0;
    omError.Empty();
    if (pbySeed == nullptr || unSeedLen == 0 || pbyKey == nullptr || unMaxKeyLen == 0)
    {
        omError = "invalid seed/key buffer";
        return FALSE;
    }
    const char* pchVar = (pchVariant != nullptr) ? pchVariant : "";
    int nResult = KGRE_UnspecifiedError;

    if (m_pfnExOptCdecl != nullptr)
    {
        nResult = ((PFN_GenerateKeyExOptCdecl)m_pfnExOptCdecl)(
                      pbySeed, unSeedLen, unSecurityLevel, pchVar, "",
                      pbyKey, unMaxKeyLen, &unKeyLen);
    }
    else if (m_pfnExCdecl != nullptr)
    {
        nResult = ((PFN_GenerateKeyExCdecl)m_pfnExCdecl)(
                      pbySeed, unSeedLen, unSecurityLevel, pchVar,
                      pbyKey, unMaxKeyLen, &unKeyLen);
    }
    else if (m_pfnExOptStd != nullptr)
    {
        nResult = ((PFN_GenerateKeyExOptStd)m_pfnExOptStd)(
                      pbySeed, unSeedLen, unSecurityLevel, pchVar, "",
                      pbyKey, unMaxKeyLen, &unKeyLen);
    }
    else if (m_pfnExStd != nullptr)
    {
        nResult = ((PFN_GenerateKeyExStd)m_pfnExStd)(
                      pbySeed, unSeedLen, unSecurityLevel, pchVar,
                      pbyKey, unMaxKeyLen, &unKeyLen);
    }
    else if (m_pfnLegacy != nullptr)
    {
        unsigned short usKey = 0;
        nResult = ((PFN_GenerateKeyLegacy)m_pfnLegacy)(
                      (unsigned char*)pbySeed, (unsigned short)unSeedLen,
                      pbyKey, (unsigned short)unMaxKeyLen, &usKey);
        unKeyLen = usKey;
    }
    else
    {
        omError = "Seed&Key DLL not loaded";
        return FALSE;
    }

    if (nResult != KGRE_Ok)
    {
        omError = "GenerateKeyEx failed: " + VectorResultText(nResult);
        return FALSE;
    }
    if (unKeyLen == 0)
    {
        unKeyLen = unSeedLen;
    }
    if (unKeyLen > unMaxKeyLen)
    {
        omError = "GenerateKeyEx failed: KGRE_BufferToSmall";
        return FALSE;
    }
    return TRUE;
}
