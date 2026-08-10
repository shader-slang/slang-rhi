#pragma once

#include <slang-rhi.h>

namespace rhi {

using SharedLibraryHandle = void*;

Result loadSharedLibrary(const char* path, SharedLibraryHandle& handleOut);
void unloadSharedLibrary(SharedLibraryHandle handle);

/// Given a shared library handle and a name, return the associated object.
/// Return nullptr if object is not found.
void* findSymbolAddressByName(SharedLibraryHandle handle, const char* name);

/// Given a symbol from a loaded shared library, return the library's path.
const char* findSharedLibraryPath(void* symbolAddress);

static constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;

/// Return a timestamp value from the CPU's high-resolution timer, if available.
/// The frequency of the timer can be obtained via `getCpuTimestampFrequency()`.
/// If a high-resolution timer is not available, this function will return 0.
uint64_t getCpuTimestamp();

/// Return the frequency of the CPU's high-resolution timer, if available.
/// If a high-resolution timer is not available, this function will return 0.
uint64_t getCpuTimestampFrequency();

/// Return the domain of the CPU timestamp, if available.
/// If a high-resolution timer is not available, this function will return `CpuTimestampDomain::Unknown`.
CpuTimestampDomain getCpuTimestampDomain();

uint64_t ticksToNanoseconds(uint64_t ticks, uint64_t frequency);

} // namespace rhi
