#include "platform.h"

#include "assert.h"

#if SLANG_WINDOWS_FAMILY
#include <windows.h>
#elif SLANG_LINUX_FAMILY || SLANG_APPLE_FAMILY
#include <dlfcn.h>
#if SLANG_LINUX_FAMILY
#include <time.h>
#endif
#if SLANG_APPLE_FAMILY
#include <mach/mach_time.h>
#endif
#elif SLANG_WASM
// nothing to include for WASM
#else
#error "Unsupported platform"
#endif

namespace rhi {

Result loadSharedLibrary(const char* path, SharedLibraryHandle& handleOut)
{
#if SLANG_WINDOWS_FAMILY
    handleOut = LoadLibraryA(path);
#elif SLANG_LINUX_FAMILY || SLANG_APPLE_FAMILY
    handleOut = dlopen(path, RTLD_LAZY);
#else
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
    return SLANG_E_NOT_IMPLEMENTED;
#endif
    if (!handleOut)
    {
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

void unloadSharedLibrary(SharedLibraryHandle handle)
{
#if SLANG_WINDOWS_FAMILY
    FreeLibrary(static_cast<HMODULE>(handle));
#elif SLANG_LINUX_FAMILY || SLANG_APPLE_FAMILY
    dlclose(handle);
#else
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
#endif
}

void* findSymbolAddressByName(SharedLibraryHandle handle, const char* name)
{
#if SLANG_WINDOWS_FAMILY
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#elif SLANG_LINUX_FAMILY || SLANG_APPLE_FAMILY
    return dlsym(handle, name);
#else
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
    return nullptr;
#endif
}

const char* findSharedLibraryPath(void* symbolAddress)
{
#if SLANG_WINDOWS_FAMILY
    HMODULE module;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)symbolAddress, &module))
    {
        return nullptr;
    }
    static char path[1024];
    GetModuleFileNameA(module, path, sizeof(path));
    return path;
#elif SLANG_LINUX_FAMILY || SLANG_APPLE_FAMILY
    Dl_info info;
    if (dladdr(symbolAddress, &info) == 0)
    {
        return nullptr;
    }
    return info.dli_fname;
#else
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
    return nullptr;
#endif
}

namespace {

struct TimeSource
{
    CpuTimestampDomain domain = CpuTimestampDomain::Unknown;
    uint64_t frequency = 0;

#if SLANG_LINUX_FAMILY
    clockid_t clockId = 0;
#endif

    TimeSource()
    {
#if SLANG_WINDOWS_FAMILY
        LARGE_INTEGER qpcFrequency = {};
        if (QueryPerformanceFrequency(&qpcFrequency))
        {
            domain = CpuTimestampDomain::QueryPerformanceCounter;
            frequency = uint64_t(qpcFrequency.QuadPart);
        }
#elif SLANG_LINUX_FAMILY
        timespec ts = {};
#if defined(CLOCK_MONOTONIC_RAW)
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0)
        {
            domain = CpuTimestampDomain::ClockMonotonicRaw;
            frequency = kNanosecondsPerSecond;
            clockId = CLOCK_MONOTONIC_RAW;
            return;
        }
#endif
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        {
            domain = CpuTimestampDomain::ClockMonotonic;
            frequency = kNanosecondsPerSecond;
            clockId = CLOCK_MONOTONIC;
            return;
        }
#elif SLANG_APPLE_FAMILY
        mach_timebase_info_data_t timebaseInfo = {};
        if (mach_timebase_info(&timebaseInfo) == KERN_SUCCESS && timebaseInfo.numer != 0)
        {
            domain = CpuTimestampDomain::MachAbsoluteTime;
            frequency = uint64_t(
                (double(timebaseInfo.denom) * double(kNanosecondsPerSecond) / double(timebaseInfo.numer)) + 0.5
            );
        }
#endif
    }

    uint64_t timestamp() const
    {
        if (domain == CpuTimestampDomain::Unknown)
        {
            return 0;
        }

#if SLANG_WINDOWS_FAMILY
        LARGE_INTEGER counter = {};
        return QueryPerformanceCounter(&counter) ? uint64_t(counter.QuadPart) : 0;
#elif SLANG_LINUX_FAMILY
        timespec ts = {};
        return clock_gettime(clockId, &ts) == 0 ? uint64_t(ts.tv_sec) * kNanosecondsPerSecond + uint64_t(ts.tv_nsec)
                                                : 0;
#elif SLANG_APPLE_FAMILY
        return mach_absolute_time();
#else
        return 0;
#endif
    }
};

static const TimeSource& getTimeSource()
{
    static TimeSource timeSource;
    return timeSource;
}

} // namespace

CpuTimestampDomain getCpuTimestampDomain()
{
    return getTimeSource().domain;
}

uint64_t getCpuTimestampFrequency()
{
    return getTimeSource().frequency;
}

uint64_t getCpuTimestamp()
{
    return getTimeSource().timestamp();
}

uint64_t ticksToNanoseconds(uint64_t ticks, uint64_t frequency)
{
    if (frequency == 0)
    {
        return 0;
    }
    if (frequency == kNanosecondsPerSecond)
    {
        return ticks;
    }

    // This assumes practical timer frequencies where remainder * 1e9 fits in uint64_t.
    // The CPU/GPU timestamp frequencies used by RHI are far below that limit.
    const uint64_t seconds = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    const uint64_t wholeNanoseconds = seconds * kNanosecondsPerSecond;
    const uint64_t fractionalNanoseconds = (remainder * kNanosecondsPerSecond + frequency / 2) / frequency;
    return wholeNanoseconds + fractionalNanoseconds;
}

} // namespace rhi
