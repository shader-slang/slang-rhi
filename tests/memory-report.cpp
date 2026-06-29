#include "memory-report.h"

#include "testing.h"
#include "core/platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if SLANG_WINDOWS_FAMILY
#include <windows.h>
#include <psapi.h>
#elif SLANG_LINUX_FAMILY
#include <sys/resource.h>
#elif SLANG_APPLE_FAMILY
#include <mach/mach.h>
#include <sys/resource.h>
#endif

namespace rhi::testing {

struct MemoryReportSnapshot
{
    std::string label;
    ProcessMemoryUsage usage;
};

static std::vector<MemoryReportSnapshot> gMemoryReportSnapshots;

static std::string jsonEscape(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2);
    for (char c : value)
    {
        switch (c)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}

static std::string formatBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KiB", "MiB", "GiB"};
    static constexpr size_t unitCount = sizeof(units) / sizeof(units[0]);
    double value = double(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < unitCount)
    {
        value /= 1024.0;
        unit++;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
    return buffer;
}

static void writeJsonUsageFields(std::ostream& stream, const ProcessMemoryUsage& usage, const char* indent)
{
    stream << indent << "\"residentBytes\": " << usage.residentBytes << ",\n";
    stream << indent << "\"peakResidentBytes\": " << usage.peakResidentBytes << ",\n";
    stream << indent << "\"commitOrVirtualBytes\": " << usage.commitOrVirtualBytes << ",\n";
    stream << indent << "\"peakCommitOrVirtualBytes\": " << usage.peakCommitOrVirtualBytes << "\n";
}

static void writeJsonUsage(std::ostream& stream, const char* name, const ProcessMemoryUsage& usage, bool trailingComma)
{
    stream << "  \"" << name << "\": {\n";
    writeJsonUsageFields(stream, usage, "    ");
    stream << "  }" << (trailingComma ? ",\n" : "\n");
}

static void writeJsonNull(std::ostream& stream, const char* name, bool trailingComma)
{
    stream << "  \"" << name << "\": null" << (trailingComma ? ",\n" : "\n");
}

static void writeJsonSnapshotFields(std::ostream& stream, const MemoryReportSnapshot& snapshot, const char* indent)
{
    const std::string usageIndent = std::string(indent) + "  ";
    stream << indent << "\"label\": \"" << jsonEscape(snapshot.label) << "\",\n";
    stream << indent << "\"usage\": {\n";
    writeJsonUsageFields(stream, snapshot.usage, usageIndent.c_str());
    stream << indent << "}\n";
}

static void writeJsonSnapshotValue(std::ostream& stream, const MemoryReportSnapshot& snapshot, const char* indent)
{
    const std::string fieldIndent = std::string(indent) + "  ";
    stream << indent << "{\n";
    writeJsonSnapshotFields(stream, snapshot, fieldIndent.c_str());
    stream << indent << "}";
}

static void writeJsonSnapshot(
    std::ostream& stream,
    const char* name,
    const MemoryReportSnapshot* snapshot,
    bool trailingComma
)
{
    if (!snapshot)
    {
        writeJsonNull(stream, name, trailingComma);
        return;
    }

    stream << "  \"" << name << "\": {\n";
    writeJsonSnapshotFields(stream, *snapshot, "    ");
    stream << "  }" << (trailingComma ? ",\n" : "\n");
}

#if SLANG_LINUX_FAMILY
static uint64_t parseStatusBytes(const std::string& line, const char* key)
{
    size_t keyLen = std::strlen(key);
    if (line.rfind(key, 0) != 0)
        return 0;
    const char* ptr = line.c_str() + keyLen;
    while (*ptr == ' ' || *ptr == '\t' || *ptr == ':')
        ptr++;
    char* end = nullptr;
    uint64_t value = std::strtoull(ptr, &end, 10);
    return value * 1024;
}
#endif

static ProcessMemoryUsage maxObservedUsage(const ProcessMemoryUsage& usage)
{
    ProcessMemoryUsage result = usage;
    result.residentBytes = std::max(usage.residentBytes, usage.peakResidentBytes);
    result.commitOrVirtualBytes = std::max(usage.commitOrVirtualBytes, usage.peakCommitOrVirtualBytes);
    return result;
}

static const MemoryReportSnapshot* getSampledPeakSnapshot(const std::vector<MemoryReportSnapshot>& snapshots)
{
    if (snapshots.empty())
        return nullptr;

    const MemoryReportSnapshot* result = &snapshots.front();
    for (const MemoryReportSnapshot& snapshot : snapshots)
    {
        if (snapshot.usage.residentBytes >= result->usage.residentBytes)
            result = &snapshot;
    }
    return result;
}

static void printMemorySnapshot(const char* label, const ProcessMemoryUsage& usage)
{
    std::printf(
        "  %-29s resident=%s, os-peak-resident=%s, commit/virtual=%s, peak-commit/virtual=%s\n",
        label,
        formatBytes(usage.residentBytes).c_str(),
        formatBytes(usage.peakResidentBytes).c_str(),
        formatBytes(usage.commitOrVirtualBytes).c_str(),
        formatBytes(usage.peakCommitOrVirtualBytes).c_str()
    );
}

static void writeMemoryReportToStream(
    std::ostream& stream,
    const std::vector<MemoryReportSnapshot>& memoryReportSnapshots
)
{
    stream << "{\n";
    stream << "  \"sampleCount\": " << memoryReportSnapshots.size() << ",\n";

    const MemoryReportSnapshot* sampledPeakSnapshot = getSampledPeakSnapshot(memoryReportSnapshots);
    writeJsonSnapshot(stream, "sampledPeak", sampledPeakSnapshot, true);

    const MemoryReportSnapshot* lastSnapshot = memoryReportSnapshots.empty() ? nullptr : &memoryReportSnapshots.back();
    writeJsonSnapshot(stream, "last", lastSnapshot, true);

    if (lastSnapshot)
    {
        ProcessMemoryUsage observedPeak = maxObservedUsage(lastSnapshot->usage);
        writeJsonUsage(stream, "osObservedPeak", observedPeak, true);
    }
    else
    {
        writeJsonNull(stream, "osObservedPeak", true);
    }

    stream << "  \"snapshots\": [\n";
    for (size_t i = 0; i < memoryReportSnapshots.size(); ++i)
    {
        writeJsonSnapshotValue(stream, memoryReportSnapshots[i], "    ");
        stream << (i + 1 < memoryReportSnapshots.size() ? ",\n" : "\n");
    }
    stream << "  ]\n";
    stream << "}\n";
}

Result getProcessMemoryUsage(ProcessMemoryUsage* outUsage)
{
    if (!outUsage)
        return SLANG_E_INVALID_ARG;

    ProcessMemoryUsage usage = {};

#if SLANG_WINDOWS_FAMILY
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)
        ))
    {
        return SLANG_FAIL;
    }
    usage.residentBytes = uint64_t(counters.WorkingSetSize);
    usage.peakResidentBytes = uint64_t(counters.PeakWorkingSetSize);
    usage.commitOrVirtualBytes = uint64_t(counters.PrivateUsage);
    usage.peakCommitOrVirtualBytes = uint64_t(counters.PeakPagefileUsage);
#elif SLANG_LINUX_FAMILY
    std::ifstream status("/proc/self/status");
    if (!status.is_open())
        return SLANG_FAIL;
    std::string line;
    while (std::getline(status, line))
    {
        if (uint64_t vmRss = parseStatusBytes(line, "VmRSS:"))
            usage.residentBytes = vmRss;
        else if (uint64_t vmHwm = parseStatusBytes(line, "VmHWM:"))
            usage.peakResidentBytes = vmHwm;
        else if (uint64_t vmSize = parseStatusBytes(line, "VmSize:"))
            usage.commitOrVirtualBytes = vmSize;
        else if (uint64_t vmPeak = parseStatusBytes(line, "VmPeak:"))
            usage.peakCommitOrVirtualBytes = vmPeak;
    }
    rusage resourceUsage = {};
    if (getrusage(RUSAGE_SELF, &resourceUsage) == 0 && usage.peakResidentBytes == 0)
        usage.peakResidentBytes = uint64_t(resourceUsage.ru_maxrss) * 1024;
#elif SLANG_APPLE_FAMILY
    mach_task_basic_info_data_t info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return SLANG_FAIL;
    usage.residentBytes = uint64_t(info.resident_size);
    usage.commitOrVirtualBytes = uint64_t(info.virtual_size);
    rusage resourceUsage = {};
    if (getrusage(RUSAGE_SELF, &resourceUsage) == 0)
        usage.peakResidentBytes = uint64_t(resourceUsage.ru_maxrss);
#else
    return SLANG_E_NOT_AVAILABLE;
#endif

    usage.peakResidentBytes = std::max(usage.peakResidentBytes, usage.residentBytes);
    usage.peakCommitOrVirtualBytes = std::max(usage.peakCommitOrVirtualBytes, usage.commitOrVirtualBytes);
    *outUsage = usage;
    return SLANG_OK;
}

void resetMemoryReport()
{
    gMemoryReportSnapshots = {};
}

void sampleMemoryReport(std::string_view label)
{
    if (!options().memoryReport)
        return;

    ProcessMemoryUsage usage;
    if (SLANG_FAILED(getProcessMemoryUsage(&usage)))
        return;

    gMemoryReportSnapshots.push_back({std::string(label), usage});
}

void printMemoryReport()
{
    if (!options().printMemoryReport)
        return;

    std::printf("\nMemory report:\n");
    std::printf("  samples: %llu\n", static_cast<unsigned long long>(gMemoryReportSnapshots.size()));

    if (gMemoryReportSnapshots.empty())
    {
        std::printf("  no process memory measurements available\n");
        return;
    }

    const MemoryReportSnapshot* sampledPeakSnapshot = getSampledPeakSnapshot(gMemoryReportSnapshots);
    if (sampledPeakSnapshot)
        printMemorySnapshot("sampled peak", sampledPeakSnapshot->usage);

    ProcessMemoryUsage observedPeak = maxObservedUsage(gMemoryReportSnapshots.back().usage);
    std::printf(
        "  %-29s resident=%s, commit/virtual=%s\n",
        "os observed peak",
        formatBytes(observedPeak.residentBytes).c_str(),
        formatBytes(observedPeak.commitOrVirtualBytes).c_str()
    );
    if (sampledPeakSnapshot && !sampledPeakSnapshot->label.empty())
    {
        std::printf("  highest sampled point: %s\n", sampledPeakSnapshot->label.c_str());
    }

    for (const MemoryReportSnapshot& snapshot : gMemoryReportSnapshots)
    {
        printMemorySnapshot(snapshot.label.c_str(), snapshot.usage);
    }
}

void writeMemoryReport()
{
    const std::string& path = options().memoryReportFile;
    if (!options().memoryReport || path.empty())
        return;

    std::filesystem::path reportPath(path);
    std::error_code errorCode;
    if (reportPath.has_parent_path())
    {
        std::filesystem::create_directories(reportPath.parent_path(), errorCode);
        if (errorCode)
        {
            std::fprintf(
                stderr,
                "Failed to create memory report directory: %s (%s)\n",
                path.c_str(),
                errorCode.message().c_str()
            );
            return;
        }
    }

    std::ofstream stream(reportPath);
    if (!stream.is_open())
    {
        std::fprintf(stderr, "Failed to write memory report file: %s\n", path.c_str());
        return;
    }

    writeMemoryReportToStream(stream, gMemoryReportSnapshots);
}

} // namespace rhi::testing
