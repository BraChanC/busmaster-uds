
#include "stdafx.h"
#include <stdio.h>
#include "BusmasterDump.h"
#include <time.h>

std::string CBusmasterDump::m_strAppName;

static void WriteCrashLog(EXCEPTION_POINTERS* pExceptionInfo, const char* dumpPath);

static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    if (pExceptionInfo == nullptr || pExceptionInfo->ExceptionRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile long s_nLogged = 0;
    if (InterlockedCompareExchange(&s_nLogged, 1, 0) != 0)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    WriteCrashLog(pExceptionInfo, "veh");
    return EXCEPTION_CONTINUE_SEARCH;
}

CBusmasterDump::CBusmasterDump( std::string strAppName )
{
    m_strAppName = strAppName;
    ::AddVectoredExceptionHandler(1, VectoredCrashHandler);
    ::SetUnhandledExceptionFilter( ExceptionFilter );
}

static void WriteCrashLog(EXCEPTION_POINTERS* pExceptionInfo, const char* dumpPath)
{
    char logPath[_MAX_PATH] = {0};
    if (!GetTempPath(_MAX_PATH, logPath))
    {
        strcpy_s(logPath, "c:\\temp\\");
    }
    strcat_s(logPath, "BUSMASTER_crash.log");

    FILE* fp = nullptr;
    fopen_s(&fp, logPath, "a");
    if (fp == nullptr)
    {
        return;
    }

    time_t now = time(nullptr);
    char timeBuf[64] = {0};
    ctime_s(timeBuf, sizeof(timeBuf), &now);

    DWORD code = 0;
    DWORD64 addr = 0;
    if (pExceptionInfo != nullptr && pExceptionInfo->ExceptionRecord != nullptr)
    {
        code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        addr = (DWORD64)pExceptionInfo->ExceptionRecord->ExceptionAddress;
    }

    char moduleName[_MAX_PATH] = "unknown";
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(ULONG_PTR)addr, &hMod) && hMod != nullptr)
    {
        GetModuleFileNameA(hMod, moduleName, _MAX_PATH);
    }

    fprintf(fp, "---- BUSMASTER crash ----\n");
    fprintf(fp, "time: %s", timeBuf);
    fprintf(fp, "exception: 0x%08X\n", code);
    fprintf(fp, "address: 0x%p\n", (void*)(ULONG_PTR)addr);
    fprintf(fp, "module: %s\n", moduleName);
    if (hMod != nullptr)
    {
        fprintf(fp, "module_base: 0x%p rva: 0x%lX\n", (void*)hMod, (unsigned long)((ULONG_PTR)addr - (ULONG_PTR)hMod));
    }
    fprintf(fp, "dump: %s\n", dumpPath != nullptr ? dumpPath : "");

    void* frames[32] = {0};
    USHORT nFrames = CaptureStackBackTrace(0, 32, frames, nullptr);
    fprintf(fp, "stack (%u):\n", nFrames);
    for (USHORT i = 0; i < nFrames; ++i)
    {
        HMODULE hFrame = nullptr;
        char frameMod[_MAX_PATH] = "unknown";
        DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (GetModuleHandleExA(flags, (LPCSTR)frames[i], &hFrame) && hFrame != nullptr)
        {
            GetModuleFileNameA(hFrame, frameMod, _MAX_PATH);
            fprintf(fp, "  [%u] %s + 0x%lX\n", i, frameMod,
                    (unsigned long)((ULONG_PTR)frames[i] - (ULONG_PTR)hFrame));
        }
        else
        {
            fprintf(fp, "  [%u] 0x%p\n", i, frames[i]);
        }
    }
    fprintf(fp, "\n");
    fclose(fp);
}

LONG CBusmasterDump::ExceptionFilter( struct _EXCEPTION_POINTERS* pExceptionInfo )
{
    LONG lRetval = EXCEPTION_CONTINUE_SEARCH;

    char szDumpPath[_MAX_PATH] = {0};
    if (!GetTempPath(_MAX_PATH, szDumpPath))
    {
        strcpy_s(szDumpPath, "c:\\temp\\");
    }
    strcat_s(szDumpPath, "BUSMASTER.dmp");
    WriteCrashLog(pExceptionInfo, szDumpPath);

    HMODULE hDll = ::LoadLibrary("DBGHELP.DLL");
    std::string strResult = "";

    if (hDll)
    {
        MINIDUMPWRITEDUMP pDump = (MINIDUMPWRITEDUMP)::GetProcAddress(hDll, "MiniDumpWriteDump");
        if (pDump)
        {
            HANDLE hFile = ::CreateFile(szDumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                _MINIDUMP_EXCEPTION_INFORMATION ExInfo;
                ExInfo.ThreadId = ::GetCurrentThreadId();
                ExInfo.ExceptionPointers = pExceptionInfo;
                ExInfo.ClientPointers = 0;
                if (pDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &ExInfo, nullptr, nullptr))
                {
                    strResult = "Saved dump file to ";
                    strResult += szDumpPath;
                    lRetval = EXCEPTION_EXECUTE_HANDLER;
                }
                else
                {
                    strResult = "Failed to save dump file to ";
                    strResult += szDumpPath;
                }
                ::CloseHandle(hFile);
            }
            else
            {
                strResult = "Failed to create dump file ";
                strResult += szDumpPath;
            }
        }
        else
        {
            strResult = "DBGHELP.DLL too old";
        }
        ::FreeLibrary(hDll);
    }
    else
    {
        strResult = "DBGHELP.DLL not found";
    }

    ::MessageBox(nullptr, strResult.c_str(), m_strAppName.c_str(), MB_OK);
    return lRetval;
}
