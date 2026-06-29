#pragma once

#include <slang-rhi.h>

#include <cstdint>
#include <string_view>

namespace rhi::testing {

struct ProcessMemoryUsage
{
    uint64_t residentBytes = 0;
    uint64_t peakResidentBytes = 0;
    // Windows reports commit/private usage here; Linux and macOS report virtual address space.
    uint64_t commitOrVirtualBytes = 0;
    uint64_t peakCommitOrVirtualBytes = 0;
};

Result getProcessMemoryUsage(ProcessMemoryUsage* outUsage);

void resetMemoryReport();
void sampleMemoryReport(std::string_view label);
void printMemoryReport();
void writeMemoryReport();

} // namespace rhi::testing
