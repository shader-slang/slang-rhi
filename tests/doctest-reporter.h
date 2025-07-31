#include <doctest.h>

#include <mutex>
#include <cstdarg>

#include "testing.h"

#include <windows.h>


#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <string>

#pragma comment(lib, "ntdll.lib")

typedef struct _SYSTEM_HANDLE
{
    ULONG ProcessId;
    BYTE ObjectTypeNumber;
    BYTE Flags;
    USHORT Handle;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;
typedef struct _SYSTEM_HANDLE_INFORMATION
{
    ULONG HandleCount;
    SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

SYSTEM_INFORMATION_CLASS SystemInformationClass = SYSTEM_INFORMATION_CLASS(16); // SystemHandleInformation

std::wstring GetHandleType(HANDLE handle)
{
    BYTE buffer[1024];
    ULONG retLen = 0;
    NTSTATUS status = NtQueryObject(handle, ObjectTypeInformation, buffer, sizeof(buffer), &retLen);
    if (NT_SUCCESS(status))
    {
        UNICODE_STRING* typeName = (UNICODE_STRING*)buffer;
        return std::wstring(typeName->Buffer, typeName->Length / sizeof(WCHAR));
    }
    return L"";
}

struct HandleInfo
{
    HANDLE handle;
    std::wstring type;
    std::wstring path;
};

std::vector<HandleInfo> EnumerateHandles()
{
    std::vector<HandleInfo> infos;

    ULONG len = 0x10000;
    std::vector<BYTE> buffer(len);
    ULONG returnLen = 0;

    NTSTATUS status = NtQuerySystemInformation(SystemInformationClass, buffer.data(), len, &returnLen);
    if (status == STATUS_INFO_LENGTH_MISMATCH)
    {
        buffer.resize(returnLen);
        status = NtQuerySystemInformation(SystemInformationClass, buffer.data(), returnLen, &returnLen);
    }

    if (!NT_SUCCESS(status))
    {
        std::cerr << "NtQuerySystemInformation failed: " << std::hex << status << "\n";
        return {};
    }

    auto* handleInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION>(buffer.data());
    DWORD myPid = GetCurrentProcessId();

    for (ULONG i = 0; i < handleInfo->HandleCount; ++i)
    {
        const SYSTEM_HANDLE& sh = handleInfo->Handles[i];
        if (sh.ProcessId != myPid)
            continue;


        HANDLE dup = nullptr;
        HANDLE hProc = GetCurrentProcess();
        if (!DuplicateHandle(hProc, (HANDLE)(uintptr_t)sh.Handle, hProc, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
            continue;

        HandleInfo info;
        info.handle = (HANDLE)(uintptr_t)sh.Handle;
        info.type = GetHandleType(dup);
        // std::wcout << L"[Type] " << info.type;

        if (info.type == L"File")
        {
            WCHAR path[MAX_PATH];
            if (GetFinalPathNameByHandleW(dup, path, MAX_PATH, 0))
            {
                info.path = path;
                // std::wcout << L" [Path] " << info.path;
            }
        }

        // std::wcout << std::endl;
        CloseHandle(dup);

        infos.push_back(info);
    }

    return infos;
}

void dumpHandleCount(const char* msg)
{
    DWORD handleCount = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handleCount);
    printf("%s: handle count = %lu\n", msg, handleCount);

#if 1
    static std::map<HANDLE, HandleInfo> prevHandles;
    static std::map<HANDLE, HandleInfo> currentHandles;
    auto handles = EnumerateHandles();
    currentHandles.clear();
    for (const auto& info : handles)
    {
        if (prevHandles.find(info.handle) == prevHandles.end())
        {
            // New handle
            printf("new handle: %p, type: %ls\n", info.handle, info.type.c_str());
            if (info.path.size() > 0)
            {
                printf("  path: %ls\n", info.path.c_str());
            }
        }
        currentHandles[info.handle] = info;
    }
    prevHandles = currentHandles;
#endif
}

namespace doctest {

#define LOCK() std::lock_guard<std::mutex> lock(mutex);

struct CustomReporter : public IReporter
{
    // caching pointers/references to objects of these types - safe to do
    std::ostream& stream;
    const ContextOptions& opt;
    ConsoleReporter consoleReporter;
    const TestCaseData* tc;
    const SubcaseSignature* sc;
    std::mutex mutex;
    Timer timer;
    int cursorPos = 0;

    const int resultPos = 64;
    const char* indent = "    ";

    // constructor has to accept the ContextOptions by ref as a single argument
    CustomReporter(const ContextOptions& in)
        : stream(*in.cout)
        , opt(in)
        , consoleReporter(in)
    {
    }

    void report_query(const QueryData& in) override { consoleReporter.report_query(in); }

    void test_run_start() override
    {
        LOCK();
        stream << Color::None;
        consoleReporter.test_run_start();

        if (rhi::testing::options().checkDevices)
        {
            checkDevices();
        }
    }

    void test_run_end(const TestRunStats& in) override
    {
        dumpHandleCount("test_run_end");
        LOCK();
        stream << Color::None;
        consoleReporter.test_run_end(in);
    }

    void test_case_start(const TestCaseData& in) override
    {
        dumpHandleCount("test_case_start");
        LOCK();
        ensure_newline();
        consoleReporter.test_case_start(in);
        tc = &in;
        color(Color::Grey);
        fill(79, '-');
        printf("\n");
        color(Color::None);
        printf("%s\n", tc->m_name);
    }

    // called when a test case is reentered because of unfinished subcases
    void test_case_reenter(const TestCaseData& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_reenter(in);
    }

    void test_case_end(const CurrentTestCaseStats& in) override
    {
        dumpHandleCount("test_case_end");
        LOCK();
        ensure_newline();
        consoleReporter.test_case_end(in);
        color(Color::None);
        printf("%s", tc->m_name);
        fill(resultPos);
        if (in.failure_flags)
        {
            color(Color::Red);
            print("FAILED");
        }
        else
        {
            color(Color::Green);
            printf("PASSED");
        }
        color(Color::LightGrey);
        printf(" (%.2fs)\n", in.seconds);
    }

    void test_case_exception(const TestCaseException& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.test_case_exception(in);
    }

    void subcase_start(const SubcaseSignature& in) override
    {
        LOCK();
        sc = &in;
        timer.start();
        stream << Color::LightGrey;
        printf("%s (%s)", tc->m_name, sc->m_name.c_str());
    }

    void subcase_end() override
    {
        LOCK();
        double seconds = timer.getElapsedSeconds();
        stream << Color::LightGrey;
        if (cursorPos == 0)
            printf("%s (%s)", tc->m_name, sc->m_name.c_str());
        fill(resultPos);
        printf("  DONE (%.2fs)\n", seconds);
    }

    void log_assert(const AssertData& in) override
    {
        // don't include successful asserts by default - this is done here
        // instead of in the framework itself because doctest doesn't know
        // if/when a reporter/listener cares about successful results
        if (!in.m_failed && !opt.success)
            return;

        LOCK();
        ensure_newline();
        consoleReporter.log_assert(in);
    }

    void log_message(const MessageData& in) override
    {
        LOCK();
        ensure_newline();
        consoleReporter.log_message(in);
    }

    void test_case_skipped(const TestCaseData& /*in*/) override {}

private:
    void color(Color::Enum c)
    {
        if (!opt.no_colors)
            stream << c;
    }

    void fill(int x, char c = ' ')
    {
        while (cursorPos < x)
        {
            stream << c;
            cursorPos++;
        }
    }

    void print(std::string_view str)
    {
        stream << str;
        stream.flush();
        for (char c : str)
            cursorPos = c == '\n' ? 0 : cursorPos + 1;
    }

    void printf(const char* fmt, ...)
    {
        std::va_list args;
        va_start(args, fmt);
        char buffer[1024];
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        print(buffer);
    }

    void println(std::string_view str)
    {
        print(str);
        print("\n");
    }

    void ensure_newline()
    {
        if (cursorPos != 0)
            print("\n");
    }

    void checkDevices()
    {
        printSeparator();
        printf("Checking for available devices:\n");
        for (rhi::DeviceType deviceType : ALL_DEVICE_TYPES)
        {
            printSeparator();
            printf("%s: ", rhi::getRHI()->getDeviceTypeName(deviceType));
            rhi::testing::DeviceAvailabilityResult result = rhi::testing::checkDeviceTypeAvailable(deviceType);
            if (result.available)
            {
                color(Color::Green);
                printf("supported\n");
                color(Color::None);
                printDeviceInfo(result.device);
            }
            else
            {
                color(Color::Yellow);
                printf("not supported (%s)\n", result.error.c_str());
                color(Color::None);
            }
            if (result.debugCallbackOutput.size() > 0)
                printf("Debug callback output: %s\n", result.debugCallbackOutput.c_str());
            if (result.diagnostics.size() > 0)
                printf("Slang diagnostics: %s\n", result.diagnostics.c_str());
        }
        printSeparator();
    }

    void printSeparator()
    {
        color(Color::Grey);
        fill(79, '-');
        printf("\n");
        color(Color::None);
    }

    void printDeviceInfo(rhi::IDevice* device)
    {
        const rhi::DeviceInfo& deviceInfo = device->getInfo();
        printf("Adapter Name: %s\n", deviceInfo.adapterName);
        printf("Adapter LUID: ");
        for (size_t i = 0; i < sizeof(rhi::AdapterLUID); i++)
            printf("%02x", deviceInfo.adapterLUID.luid[i]);
        printf("\n");
        {
            uint32_t featureCount;
            device->getFeatures(&featureCount, nullptr);
            std::vector<rhi::Feature> features(featureCount);
            device->getFeatures(&featureCount, features.data());
            printf("Features:\n");
            for (uint32_t i = 0; i < featureCount; i++)
                printf("%s ", rhi::getRHI()->getFeatureName(features[i]));
            printf("\n");
        }
        {
            uint32_t capabilityCount;
            device->getCapabilities(&capabilityCount, nullptr);
            std::vector<rhi::Capability> capabilities(capabilityCount);
            device->getCapabilities(&capabilityCount, capabilities.data());
            printf("Capabilities:\n");
            for (uint32_t i = 0; i < capabilityCount; i++)
                printf("%s ", rhi::getRHI()->getCapabilityName(capabilities[i]));
            printf("\n");
        }
        if (device->hasFeature(rhi::Feature::CooperativeVector))
        {
            uint32_t propertiesCount;
            device->getCooperativeVectorProperties(nullptr, &propertiesCount);
            std::vector<rhi::CooperativeVectorProperties> properties(propertiesCount);
            device->getCooperativeVectorProperties(properties.data(), &propertiesCount);
            printf("Cooperative Vector Properties:\n");
            printf("inputType inputInterpretation matrixInterpretation biasInterpretation resultType\n");
            for (const auto& prop : properties)
            {
                printf(
                    "%-9s %-19s %-20s %-18s %-10s\n",
                    rhi::enumToString(prop.inputType),
                    rhi::enumToString(prop.inputInterpretation),
                    rhi::enumToString(prop.matrixInterpretation),
                    rhi::enumToString(prop.biasInterpretation),
                    rhi::enumToString(prop.resultType)
                );
            }
        }
    }
};

// "1" is the priority - used for ordering when multiple reporters are used
REGISTER_REPORTER("custom", 1, CustomReporter);

} // namespace doctest
