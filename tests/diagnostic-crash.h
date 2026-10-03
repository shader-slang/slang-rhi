#pragma once

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// Temporary CI diagnostics. The helper process writes the dump while the faulting
// thread keeps its original exception context alive in the vectored handler.
namespace diagnostic_crash {
static wchar_t executable[MAX_PATH];
static wchar_t directory[MAX_PATH];
static LONG capturing = 0;
static HANDLE captured;

static int writeDump(int argc, const char** argv)
{
    if (argc != 6)
        return 2;
    DWORD processId = DWORD(std::strtoul(argv[2], nullptr, 10));
    MINIDUMP_EXCEPTION_INFORMATION exception = {};
    exception.ThreadId = DWORD(std::strtoul(argv[3], nullptr, 10));
    exception.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(std::strtoull(argv[4], nullptr, 16));
    exception.ClientPointers = TRUE;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, processId);
    HANDLE file = CreateFileA(argv[5], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    auto write = dbghelp ? reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                         : nullptr;
    BOOL success = FALSE;
    if (process && file != INVALID_HANDLE_VALUE && write)
    {
        success = write(
            process,
            processId,
            file,
            MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
            &exception,
            nullptr,
            nullptr
        );
    }
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (process)
        CloseHandle(process);
    std::fprintf(stderr, "[diagnostic] dump helper success=%d error=%lu\n", success, error);
    return success ? 0 : 1;
}

static LONG CALLBACK handle(EXCEPTION_POINTERS* exception)
{
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&capturing, 1, 0) != 0)
    {
        WaitForSingleObject(captured, 60000);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD processId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
    wchar_t command[4 * MAX_PATH];
    int length = _snwprintf_s(
        command,
        _countof(command),
        _TRUNCATE,
        L"\"%s\" --diagnostic-write-dump %lu %lu %llx \"%s\\av-%lu-%lu.dmp\"",
        executable,
        processId,
        threadId,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(exception)),
        directory,
        processId,
        threadId
    );
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION helper = {};
    BOOL started = length >= 0 && CreateProcessW(
                                      executable,
                                      command,
                                      nullptr,
                                      nullptr,
                                      FALSE,
                                      CREATE_NO_WINDOW,
                                      nullptr,
                                      nullptr,
                                      &startup,
                                      &helper
                                  );
    if (started)
    {
        DWORD wait = WaitForSingleObject(helper.hProcess, 60000);
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(helper.hProcess, &exitCode);
        std::fprintf(
            stderr,
            "[diagnostic] first-chance AV at=%p thread=%lu helper-wait=%lu helper-exit=%lu\n",
            exception->ExceptionRecord->ExceptionAddress,
            threadId,
            wait,
            exitCode
        );
        CloseHandle(helper.hThread);
        CloseHandle(helper.hProcess);
    }
    else
        std::fprintf(stderr, "[diagnostic] could not start dump helper: %lu\n", GetLastError());
    std::fflush(stderr);
    SetEvent(captured);
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool install()
{
    DWORD size = GetEnvironmentVariableW(L"RHI_DIAGNOSTIC_DUMP_DIRECTORY", directory, MAX_PATH);
    if (!size)
        return true;
    DWORD executableSize = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (size >= MAX_PATH || !executableSize || executableSize >= MAX_PATH)
        return false;
    captured = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return captured && AddVectoredExceptionHandler(1, handle);
}

static int selfTest()
{
    __try
    {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 3;
    }
    return 0;
}
} // namespace diagnostic_crash
#endif
