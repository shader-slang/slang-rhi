#pragma once

#include <slang-rhi.h>

#include <cstdint>
#include <string_view>

namespace rhi::testing {

struct ProcessMemoryUsage
{
    uint64_t residentBytes = 0;
    uint64_t peakResidentBytes = 0;
    uint64_t commitBytes = 0;
    uint64_t peakCommitBytes = 0;
};

Result getProcessMemoryUsage(ProcessMemoryUsage* outUsage);

void resetMemoryReport();
void sampleMemoryReport(std::string_view label);
void printMemoryReport();
void writeMemoryReport();

} // namespace rhi::testing
