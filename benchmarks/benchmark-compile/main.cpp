#include "synthetic-modules.h"
#include "thread-pool.h"

#include "core/blob.h"

#include <slang-rhi.h>
#include <slang.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace rhi;

// ---------------------------------------------------------------------------
// Debug callback — prints all RHI/driver messages to stderr.
// ---------------------------------------------------------------------------

class StderrDebugCallback : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        const char* typeStr = "info";
        if (type == DebugMessageType::Warning)
            typeStr = "warning";
        if (type == DebugMessageType::Error)
            typeStr = "error";

        const char* sourceStr = "unknown";
        if (source == DebugMessageSource::Layer)
            sourceStr = "layer";
        if (source == DebugMessageSource::Driver)
            sourceStr = "driver";
        if (source == DebugMessageSource::Slang)
            sourceStr = "slang";

        fprintf(stderr, "[%s/%s] %s\n", sourceStr, typeStr, message);
    }
};

static StderrDebugCallback g_debugCallback;

// ---------------------------------------------------------------------------
// Global configuration — set by CLI parsing.
// ---------------------------------------------------------------------------

struct BenchmarkConfig
{
    int iterations = 5;
    bool verbose = false;

    // Pinning — nullopt means auto-vary.
    std::optional<DeviceType> pinnedDeviceType;
    std::optional<int> pinnedModuleCount;
    std::optional<SizeLevel> pinnedSizeLevel;
    std::optional<uint32_t> pinnedThreadCount; // nullopt = auto-vary; 0 = serial (no pool)
};

static BenchmarkConfig g_config;

// Global seed counter — incremented for every iteration across all configurations
// to ensure no two iterations ever share a seed (and thus module/function names).
static int globalSeedCounter = 0;

// Device types to benchmark (ray tracing capable).
static const DeviceType kAllDeviceTypes[] = {
    DeviceType::Vulkan,
    DeviceType::D3D12,
    DeviceType::CUDA,
};
static const int kAllDeviceTypeCount = sizeof(kAllDeviceTypes) / sizeof(kAllDeviceTypes[0]);

// ---------------------------------------------------------------------------
// Helper: compile synthetic Slang modules into a linked IShaderProgram
// ---------------------------------------------------------------------------

static ComPtr<IShaderProgram> compileModules(IDevice* device, const std::vector<SyntheticModuleDesc>& modules)
{
    ComPtr<slang::ISession> slangSession;
    device->getSlangSession(slangSession.writeRef());
    if (!slangSession)
    {
        fprintf(stderr, "Error: failed to get Slang session from device\n");
        return nullptr;
    }

    std::vector<slang::IComponentType*> componentTypes;
    // Keep modules and entry points alive for the duration of linking.
    std::vector<ComPtr<slang::IModule>> loadedModules;
    std::vector<ComPtr<slang::IEntryPoint>> entryPoints;

    for (size_t i = 0; i < modules.size(); i++)
    {
        const auto& mod = modules[i];
        ComPtr<slang::IBlob> diagnosticsBlob;

        // Use the entry point name as the module name — it already includes the
        // per-iteration seed, so Slang's module cache won't return stale modules.
        std::string moduleName = "module_" + mod.entryPointName;
        auto srcBlob = UnownedBlob::create(mod.source.data(), mod.source.size());

        slang::IModule* slangModule =
            slangSession
                ->loadModuleFromSource(moduleName.c_str(), moduleName.c_str(), srcBlob, diagnosticsBlob.writeRef());
        if (!slangModule)
        {
            if (diagnosticsBlob)
                fprintf(
                    stderr,
                    "Slang error (module %zu): %s\n",
                    i,
                    static_cast<const char*>(diagnosticsBlob->getBufferPointer())
                );
            else
                fprintf(stderr, "Slang error: failed to load module %zu\n", i);
            return nullptr;
        }

        loadedModules.push_back(ComPtr<slang::IModule>(slangModule));
        componentTypes.push_back(slangModule);

        // Find the entry point in this module.
        ComPtr<slang::IEntryPoint> entryPoint;
        slangModule->findEntryPointByName(mod.entryPointName.c_str(), entryPoint.writeRef());
        if (!entryPoint)
        {
            fprintf(stderr, "Error: entry point '%s' not found in module %zu\n", mod.entryPointName.c_str(), i);
            return nullptr;
        }
        entryPoints.push_back(entryPoint);
        componentTypes.push_back(entryPoint);
    }

    // Compose all modules and entry points into a single component type.
    // Note: do NOT call link() here. The RHI's ShaderProgram::init() handles
    // linking internally. Passing a pre-linked program causes issues with
    // D3D12's entry point resolution.
    ComPtr<slang::IComponentType> composedProgram;
    {
        ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = slangSession->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef()
        );
        if (SLANG_FAILED(result))
        {
            if (diagnosticsBlob)
                fprintf(
                    stderr,
                    "Slang compose error: %s\n",
                    static_cast<const char*>(diagnosticsBlob->getBufferPointer())
                );
            return nullptr;
        }
    }

    // Force Slang to link/optimize the composite program now (via getLayout).
    // Without this, linking is deferred into createRayTracingPipeline (via
    // compileShaders -> linkedProgram->getLayout()), hiding seconds of IR work
    // inside the pipeline creation timing.
    auto layout = composedProgram->getLayout();

    if (g_config.verbose)
    {
        fprintf(stderr, "[verbose] Composed program: %u entry points\n", (unsigned)layout->getEntryPointCount());
        for (uint32_t i = 0; i < (uint32_t)layout->getEntryPointCount(); i++)
        {
            auto ep = layout->getEntryPointByIndex(i);
            fprintf(
                stderr,
                "  [%u] name=\"%s\" nameOverride=\"%s\" stage=%d\n",
                i,
                ep->getName() ? ep->getName() : "(null)",
                ep->getNameOverride() ? ep->getNameOverride() : "(null)",
                (int)ep->getStage()
            );
        }
    }

    // Create the RHI shader program.
    ShaderProgramDesc desc = {};
    desc.slangGlobalScope = composedProgram;
    ComPtr<IShaderProgram> program;
    ComPtr<ISlangBlob> diagnosticsBlob;
    SlangResult result = device->createShaderProgram(desc, program.writeRef(), diagnosticsBlob.writeRef());
    if (SLANG_FAILED(result))
    {
        fprintf(stderr, "createShaderProgram error (0x%08x)\n", static_cast<int>(result));
        if (diagnosticsBlob)
            fprintf(stderr, "  diagnostics: %s\n", static_cast<const char*>(diagnosticsBlob->getBufferPointer()));
        return nullptr;
    }

    return program;
}

// ---------------------------------------------------------------------------
// Helper: create a ray tracing pipeline from the compiled program
// ---------------------------------------------------------------------------

static ComPtr<IRayTracingPipeline> createRayTracingPipeline(
    IDevice* device,
    IShaderProgram* program,
    const std::vector<SyntheticModuleDesc>& modules
)
{
    // Build hit group descriptors from closesthit modules.
    std::vector<HitGroupDesc> hitGroups;
    std::vector<std::string> hitGroupNames; // keep strings alive

    for (const auto& mod : modules)
    {
        if (mod.stage == SLANG_STAGE_CLOSEST_HIT)
        {
            std::string name = "hitgroup_" + mod.entryPointName;
            hitGroupNames.push_back(name);

            HitGroupDesc hg = {};
            hg.hitGroupName = hitGroupNames.back().c_str();
            hg.closestHitEntryPoint = mod.entryPointName.c_str();
            hitGroups.push_back(hg);
        }
    }

    RayTracingPipelineDesc rtDesc = {};
    rtDesc.program = program;
    rtDesc.hitGroupCount = static_cast<uint32_t>(hitGroups.size());
    rtDesc.hitGroups = hitGroups.data();
    rtDesc.maxRecursion = 1;
    rtDesc.maxRayPayloadSize = 16; // sizeof(float4) for RayPayload
    rtDesc.maxAttributeSizeInBytes = 8;

    if (g_config.verbose)
    {
        fprintf(
            stderr,
            "[verbose] createRayTracingPipeline: %u hit groups, maxRecursion=%d, "
            "maxPayload=%u, maxAttribs=%u\n",
            rtDesc.hitGroupCount,
            rtDesc.maxRecursion,
            (unsigned)rtDesc.maxRayPayloadSize,
            (unsigned)rtDesc.maxAttributeSizeInBytes
        );
        for (uint32_t i = 0; i < rtDesc.hitGroupCount; i++)
        {
            fprintf(
                stderr,
                "  hitGroup[%u]: name=\"%s\" closestHit=\"%s\"\n",
                i,
                hitGroups[i].hitGroupName ? hitGroups[i].hitGroupName : "(null)",
                hitGroups[i].closestHitEntryPoint ? hitGroups[i].closestHitEntryPoint : "(null)"
            );
        }
    }

    ComPtr<IRayTracingPipeline> pipeline;
    Result r = device->createRayTracingPipeline(rtDesc, pipeline.writeRef());
    if (SLANG_FAILED(r))
    {
        fprintf(stderr, "Error: createRayTracingPipeline failed (0x%08x)\n", static_cast<int>(r));
        return nullptr;
    }
    return pipeline;
}

// ---------------------------------------------------------------------------
// Statistics helpers
// ---------------------------------------------------------------------------

struct BenchmarkStats
{
    double minMs;
    double maxMs;
    double meanMs;
    double stddevMs;
};

static BenchmarkStats computeStats(const std::vector<double>& durationsMs)
{
    BenchmarkStats stats = {};

    if (durationsMs.empty())
        return stats;

    stats.minMs = *std::min_element(durationsMs.begin(), durationsMs.end());
    stats.maxMs = *std::max_element(durationsMs.begin(), durationsMs.end());
    stats.meanMs = std::accumulate(durationsMs.begin(), durationsMs.end(), 0.0) / durationsMs.size();

    if (durationsMs.size() > 1)
    {
        double sumSqDiff = 0.0;
        for (double d : durationsMs)
        {
            double diff = d - stats.meanMs;
            sumSqDiff += diff * diff;
        }
        stats.stddevMs = std::sqrt(sumSqDiff / (durationsMs.size() - 1));
    }

    return stats;
}

// ---------------------------------------------------------------------------
// Result table
// ---------------------------------------------------------------------------

struct BenchmarkRow
{
    const char* deviceTypeName;
    uint32_t threadCount; // 0 = serial (no task pool)
    int moduleCount;
    SizeLevel sizeLevel;
    BenchmarkStats frontendStats;   // Slang frontend: parse, type-check, link/optimize IR
    BenchmarkStats codegenStats;    // Slang backend codegen: IR → target source (SPIR-V, CUDA, HLSL)
    BenchmarkStats downstreamStats; // Downstream compiler: NVRTC (CUDA→PTX), DXC (HLSL→DXIL), or N/A
    BenchmarkStats driverStats;     // Driver pipeline creation: optixModuleCreate, vkCreateRTPipeline, etc.
    BenchmarkStats totalStats;      // Wall-clock total (frontend + codegen + downstream + driver)
};

static void printResultTable(const std::vector<BenchmarkRow>& rows)
{
    // Header
    printf(
        "%-13s| %-8s| %-6s| %-8s| %12s | %12s | %12s | %12s | %12s |\n",
        "Device Type",
        "Threads",
        "# Mods",
        "Size",
        "Frontend(ms)",
        "Codegen(ms)",
        "Downstrm(ms)",
        "Driver (ms)",
        "Total (ms)"
    );

    // Separator
    printf(
        "%-13s| %-8s| %-6s| %-8s| %12s | %12s | %12s | %12s | %12s |\n",
        "-------------",
        "--------",
        "------",
        "--------",
        "------------",
        "------------",
        "------------",
        "------------",
        "------------"
    );

    // Rows
    for (const auto& row : rows)
    {
        char threadsStr[16];
        if (row.threadCount == 0)
            snprintf(threadsStr, sizeof(threadsStr), "serial");
        else
            snprintf(threadsStr, sizeof(threadsStr), "%u", row.threadCount);

        printf(
            "%-13s| %-8s| %-6d| %-8s| %12.2f | %12.2f | %12.2f | %12.2f | %12.2f |\n",
            row.deviceTypeName,
            threadsStr,
            row.moduleCount,
            sizeLevelName(row.sizeLevel),
            row.frontendStats.meanMs,
            row.codegenStats.meanMs,
            row.downstreamStats.meanMs,
            row.driverStats.meanMs,
            row.totalStats.meanMs
        );
    }
}

// Print legend explaining each column.
static void printLegend()
{
    printf("\nColumn legend:\n");
    printf("  Frontend = Slang frontend: parse, type-check, link/optimize IR (compileModules)\n");
    printf("  Codegen  = Slang backend: IR -> target code (SPIR-V / CUDA / HLSL)\n");
    printf("  Downstrm = Downstream compiler (NVRTC for CUDA, DXC for D3D12, N/A for Vulkan)\n");
    printf("  Driver   = Driver pipeline creation (optixModuleCreate / vkCreateRTPipeline / etc.)\n");
    printf("  Total    = Frontend + pipeline creation wall-clock time\n");
}

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------

static DeviceType parseDeviceType(const char* str)
{
    if (strcmp(str, "vulkan") == 0 || strcmp(str, "vk") == 0)
        return DeviceType::Vulkan;
    if (strcmp(str, "d3d12") == 0 || strcmp(str, "dx12") == 0)
        return DeviceType::D3D12;
    if (strcmp(str, "cuda") == 0)
        return DeviceType::CUDA;
    fprintf(stderr, "Warning: unknown device type '%s', defaulting to vulkan\n", str);
    return DeviceType::Vulkan;
}

static SizeLevel parseSizeLevel(const char* str)
{
    if (strcmp(str, "simple") == 0)
        return SizeLevel::Simple;
    if (strcmp(str, "complex") == 0)
        return SizeLevel::Complex;
    fprintf(stderr, "Warning: unknown size level '%s', defaulting to simple\n", str);
    return SizeLevel::Simple;
}

static void parseArgs(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
        {
            g_config.pinnedDeviceType = parseDeviceType(argv[++i]);
        }
        else if (strcmp(argv[i], "--modules") == 0 && i + 1 < argc)
        {
            g_config.pinnedModuleCount = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            g_config.pinnedSizeLevel = parseSizeLevel(argv[++i]);
        }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
        {
            g_config.pinnedThreadCount = static_cast<uint32_t>(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            g_config.iterations = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--serial") == 0)
        {
            g_config.pinnedThreadCount = 0; // 0 = serial (no task pool)
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            g_config.verbose = true;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            printf("Usage: benchmark-compile [options]\n");
            printf("Options:\n");
            printf("  --device <type>      Pin device type: vulkan, d3d12, cuda (default: all)\n");
            printf("  --modules <n>        Pin module count (default: auto-vary 1,2,4,8,16)\n");
            printf("  --size <level>       Pin module size: simple, complex (default: auto-vary)\n");
            printf("  --threads <n>        Pin thread count (default: auto-vary 1,2,4,...,hwThreads)\n");
            printf("  --iterations <n>     Iterations per configuration (default: 5)\n");
            printf("  --serial             Pin to serial mode (no task pool)\n");
            printf("  --verbose, -v        Enable debug callbacks and validation\n");

            printf("  --help               Show this help message\n");
            exit(0);
        }
        else
        {
            fprintf(stderr, "Warning: unknown argument '%s'\n", argv[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------

/// Build the list of thread counts to benchmark.
static std::vector<uint32_t> buildThreadCountList()
{
    if (g_config.pinnedThreadCount)
    {
        return {*g_config.pinnedThreadCount};
    }

    // Auto-vary: serial (0), then powers of 2 from 1 up to hardware_concurrency.
    uint32_t hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0)
        hwThreads = 1;

    std::vector<uint32_t> counts;
    counts.push_back(0); // serial baseline
    for (uint32_t t = 1; t <= hwThreads; t *= 2)
        counts.push_back(t);
    // Always include the actual hardware thread count if it isn't a power of 2.
    if (counts.back() != hwThreads)
        counts.push_back(hwThreads);

    return counts;
}

static int runBenchmarks(IRHI* rhi, ThreadPool* pool)
{
    // Build the configuration axes.
    std::vector<DeviceType> deviceTypes;
    if (g_config.pinnedDeviceType)
    {
        deviceTypes.push_back(*g_config.pinnedDeviceType);
    }
    else
    {
        for (int i = 0; i < kAllDeviceTypeCount; ++i)
            deviceTypes.push_back(kAllDeviceTypes[i]);
    }

    std::vector<int> moduleCounts =
        g_config.pinnedModuleCount ? std::vector<int>{*g_config.pinnedModuleCount} : std::vector<int>{1, 2, 4, 8, 16};

    std::vector<SizeLevel> sizeLevels = g_config.pinnedSizeLevel
                                            ? std::vector<SizeLevel>{*g_config.pinnedSizeLevel}
                                            : std::vector<SizeLevel>{SizeLevel::Simple, SizeLevel::Complex};

    std::vector<uint32_t> threadCounts = buildThreadCountList();

    std::vector<BenchmarkRow> results;
    int failures = 0;

    // Loop order: device → modules → size → threads
    // This groups thread-count variations together in the output table,
    // making it easy to see the effect of parallelism for each configuration.

    for (auto deviceType : deviceTypes)
    {
        const char* deviceTypeName = rhi->getDeviceTypeName(deviceType);

        // Try to create the device; skip if unsupported.
        if (!rhi->isDeviceTypeSupported(deviceType))
        {
            printf("Skipping %s (not supported on this platform)\n", deviceTypeName);
            continue;
        }

        ComPtr<IDevice> device;
        {
            DeviceDesc deviceDesc = {};
            deviceDesc.deviceType = deviceType;
            if (g_config.verbose)
            {
                deviceDesc.debugCallback = &g_debugCallback;
                deviceDesc.enableValidation = true;
            }

            Result r = rhi->createDevice(deviceDesc, device.writeRef());
            if (SLANG_FAILED(r) || !device)
            {
                printf("Skipping %s (device creation failed: 0x%08x)\n", deviceTypeName, static_cast<int>(r));
                continue;
            }
        }

        printf("Benchmarking %s ...\n", deviceTypeName);

        // Get the Slang global session for compiler elapsed time queries.
        ComPtr<slang::ISession> slangSession;
        device->getSlangSession(slangSession.writeRef());
        slang::IGlobalSession* globalSession = slangSession ? slangSession->getGlobalSession() : nullptr;

        for (auto moduleCount : moduleCounts)
        {
            for (auto sizeLevel : sizeLevels)
            {
                for (auto threadCount : threadCounts)
                {
                    // Resize the pool for this thread count (0 = serial).
                    pool->setThreadCount(threadCount);

                    // Each iteration uses a unique seed to defeat all caching:
                    // - RHI level: new ShaderProgram object → compileShaders() runs fresh
                    // - Driver level: unique entry point names → different binary code
                    std::vector<double> frontendMs;   // compileModules: parse, type-check, link/optimize IR
                    std::vector<double> codegenMs;    // Slang backend codegen (IR → target source)
                    std::vector<double> downstreamMs; // Downstream compiler (NVRTC/DXC)
                    std::vector<double> driverOnlyMs; // Driver pipeline creation
                    std::vector<double> totalMs;      // End-to-end wall clock
                    frontendMs.reserve(g_config.iterations);
                    codegenMs.reserve(g_config.iterations);
                    downstreamMs.reserve(g_config.iterations);
                    driverOnlyMs.reserve(g_config.iterations);
                    totalMs.reserve(g_config.iterations);

                    for (int iter = 0; iter < g_config.iterations; ++iter)
                    {
                        // Seed must be globally unique across ALL configs (thread count,
                        // module count, size level, iteration) to prevent:
                        // - Slang module cache returning stale modules (same name, different source)
                        // - Driver shader cache reusing compiled shaders from earlier configs
                        int seed = globalSeedCounter++;

                        if (g_config.verbose)
                        {
                            fprintf(
                                stderr,
                                "[verbose] %s (threads=%u): %d mods, %s, iter %d/%d: generating...\n",
                                deviceTypeName,
                                threadCount,
                                moduleCount,
                                sizeLevelName(sizeLevel),
                                iter + 1,
                                g_config.iterations
                            );
                            fflush(stderr);
                        }

                        // Generate modules with unique names for this iteration.
                        auto modules = generateSyntheticModules({moduleCount, sizeLevel, seed});

                        // --- Timed: Slang frontend (parse, type-check, link/optimize IR) ---
                        // compileModules now forces getLayout() which triggers Slang's linker.
                        auto tFE0 = std::chrono::high_resolution_clock::now();
                        auto program = compileModules(device, modules);
                        auto tFE1 = std::chrono::high_resolution_clock::now();

                        if (!program)
                        {
                            fprintf(
                                stderr,
                                "  Error: compileModules failed for %d modules, size=%s, iter=%d\n",
                                moduleCount,
                                sizeLevelName(sizeLevel),
                                iter
                            );
                            ++failures;
                            break;
                        }

                        double feTime = std::chrono::duration<double, std::milli>(tFE1 - tFE0).count();

                        if (g_config.verbose)
                        {
                            fprintf(
                                stderr,
                                "[verbose]   compileModules done (%.2f ms), creating pipeline...\n",
                                feTime
                            );
                            fflush(stderr);
                        }

                        // Snapshot Slang's cumulative compiler timers BEFORE pipeline creation.
                        double slangTotalBefore = 0, slangDownstreamBefore = 0;
                        if (globalSession)
                            globalSession->getCompilerElapsedTime(&slangTotalBefore, &slangDownstreamBefore);

                        // --- Timed: pipeline creation (Slang codegen + driver) ---
                        // Linking is already done (getLayout called in compileModules), so this
                        // only triggers getEntryPointCode() (codegen) + createRayTracingPipeline2().
                        auto t0 = std::chrono::high_resolution_clock::now();
                        auto pipeline = createRayTracingPipeline(device, program, modules);
                        auto t1 = std::chrono::high_resolution_clock::now();

                        // Snapshot Slang's cumulative compiler timers AFTER pipeline creation.
                        double slangTotalAfter = 0, slangDownstreamAfter = 0;
                        if (globalSession)
                            globalSession->getCompilerElapsedTime(&slangTotalAfter, &slangDownstreamAfter);

                        if (!pipeline)
                        {
                            fprintf(
                                stderr,
                                "  Error: createRayTracingPipeline failed on iteration %d "
                                "for %d modules, size=%s\n",
                                iter,
                                moduleCount,
                                sizeLevelName(sizeLevel)
                            );
                            ++failures;
                            break;
                        }

                        double pipelineTime = std::chrono::duration<double, std::milli>(t1 - t0).count();

                        // Slang's getCompilerElapsedTime returns cumulative seconds.
                        // "total" = all Slang compiler time during pipeline creation.
                        // "downstream" = time in downstream compilers (NVRTC, DXC, etc.).
                        // Slang codegen = total - downstream.
                        double slangTotalDelta = (slangTotalAfter - slangTotalBefore) * 1000.0; // sec → ms
                        double downstreamDelta = (slangDownstreamAfter - slangDownstreamBefore) * 1000.0;
                        double slangCodegenTime = slangTotalDelta - downstreamDelta;

                        // Driver time = pipeline wall clock minus all Slang time.
                        // This captures the REAL first-time driver compilation cost
                        // (e.g., vkCreateRayTracingPipelinesKHR compiling SPIR-V to GPU ISA).
                        double driverTime = pipelineTime - slangTotalDelta;
                        if (driverTime < 0)
                            driverTime = 0; // Guard against timing imprecision.

                        frontendMs.push_back(feTime);
                        codegenMs.push_back(slangCodegenTime);
                        downstreamMs.push_back(downstreamDelta);
                        driverOnlyMs.push_back(driverTime);
                        totalMs.push_back(feTime + pipelineTime);

                        if (g_config.verbose)
                        {
                            fprintf(
                                stderr,
                                "[verbose]   iter %d/%d complete (fe=%.2f ms, pipe=%.2f ms)\n",
                                iter + 1,
                                g_config.iterations,
                                feTime,
                                pipelineTime
                            );
                            fflush(stderr);
                        }
                    }

                    if (g_config.verbose)
                    {
                        fprintf(
                            stderr,
                            "[verbose]   config done (threads=%u, %d mods, %s): %zu successful iterations\n",
                            threadCount,
                            moduleCount,
                            sizeLevelName(sizeLevel),
                            frontendMs.size()
                        );
                        fflush(stderr);
                    }

                    if (!frontendMs.empty())
                    {
                        BenchmarkRow row;
                        row.deviceTypeName = deviceTypeName;
                        row.threadCount = threadCount;
                        row.moduleCount = moduleCount;
                        row.sizeLevel = sizeLevel;
                        row.frontendStats = computeStats(frontendMs);
                        row.codegenStats = computeStats(codegenMs);
                        row.downstreamStats = computeStats(downstreamMs);
                        row.driverStats = computeStats(driverOnlyMs);
                        row.totalStats = computeStats(totalMs);
                        results.push_back(row);
                    }
                }
            }
        }

        // Release Slang session reference before device — the session destructor
        // may depend on device-owned resources (global session, etc.).
        slangSession.setNull();
        globalSession = nullptr;

        if (g_config.verbose)
        {
            fprintf(stderr, "[verbose] Releasing %s device...\n", deviceTypeName);
            fflush(stderr);
        }

        // Release device before moving to the next one.
        device.setNull();

        if (g_config.verbose)
        {
            fprintf(stderr, "[verbose] %s device released.\n", deviceTypeName);
            fflush(stderr);
        }
    }

    // Print results table.
    printf("\n");
    if (!results.empty())
    {
        printf("Results (%d iterations per configuration):\n\n", g_config.iterations);
        printResultTable(results);
        printLegend();
    }
    else
    {
        printf("No results collected.\n");
    }

    return failures > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Driver cache clearing
// ---------------------------------------------------------------------------

/// Clear NVIDIA's shader disk caches (DXCache, GLCache) under %LOCALAPPDATA%\NVIDIA.
/// Also clears the OptiX cache. This ensures every benchmark run measures real
/// compilation work, not cached results from a previous run.
static void clearDriverShaderCaches()
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    const char* localAppData = getenv("LOCALAPPDATA");
    if (!localAppData)
        return;

    fs::path nvidiaDir = fs::path(localAppData) / "NVIDIA";

    const char* cacheDirs[] = {"DXCache", "GLCache"};
    for (const char* dirName : cacheDirs)
    {
        fs::path cacheDir = nvidiaDir / dirName;
        std::error_code ec;
        if (!fs::exists(cacheDir, ec))
            continue;

        int count = 0;
        for (auto& entry : fs::directory_iterator(cacheDir, ec))
        {
            fs::remove_all(entry.path(), ec);
            if (!ec)
                ++count;
        }

        if (count > 0)
            fprintf(stderr, "Cleared %d entries from %s\n", count, cacheDir.string().c_str());
    }

    // Also clear the OptiX cache (typically %LOCALAPPDATA%\NVIDIA\OptixCache).
    fs::path optixCache = nvidiaDir / "OptixCache";
    {
        std::error_code ec;
        if (fs::exists(optixCache, ec))
        {
            int count = 0;
            for (auto& entry : fs::directory_iterator(optixCache, ec))
            {
                fs::remove_all(entry.path(), ec);
                if (!ec)
                    ++count;
            }
            if (count > 0)
                fprintf(stderr, "Cleared %d entries from %s\n", count, optixCache.string().c_str());
        }
    }
#else
    // On Linux, NVIDIA caches are typically in ~/.nv/ComputeCache
    const char* home = getenv("HOME");
    if (!home)
        return;

    fs::path nvCacheDir = fs::path(home) / ".nv" / "ComputeCache";
    std::error_code ec;
    if (fs::exists(nvCacheDir, ec))
    {
        int count = 0;
        for (auto& entry : fs::directory_iterator(nvCacheDir, ec))
        {
            fs::remove_all(entry.path(), ec);
            if (!ec)
                ++count;
        }
        if (count > 0)
            fprintf(stderr, "Cleared %d entries from %s\n", count, nvCacheDir.string().c_str());
    }
#endif
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Disable driver-level disk caches to prevent caching DURING the run.
    // - OPTIX_CACHE_MAXSIZE=0: disables OptiX shader cache
    // - __GL_SHADER_DISK_CACHE=0: disables NVIDIA's Vulkan/OpenGL shader disk cache
#ifdef _WIN32
    _putenv("OPTIX_CACHE_MAXSIZE=0");
#else
    putenv(const_cast<char*>("OPTIX_CACHE_MAXSIZE=0"));
#endif

    // Clear any cached shaders from previous runs.
    clearDriverShaderCaches();

    // 1. Parse CLI flags.
    parseArgs(argc, argv);

    // 2. Initialize RHI.
    IRHI* rhi = getRHI();
    if (!rhi)
    {
        fprintf(stderr, "Error: failed to get RHI instance\n");
        return 1;
    }

    // 3. Create a single thread pool and register it with the RHI once.
    //    We dynamically resize it via setThreadCount() for each benchmark config.
    ComPtr<ThreadPool> taskPool(new ThreadPool(0)); // Start in serial mode.
    {
        Result r = rhi->setTaskPool(taskPool);
        if (SLANG_FAILED(r))
        {
            fprintf(stderr, "Error: setTaskPool failed (0x%08x)\n", static_cast<int>(r));
            return 1;
        }
    }

    // 4. Print thread count plan.
    auto threadCounts = buildThreadCountList();
    printf("Thread counts to benchmark:");
    for (auto tc : threadCounts)
    {
        if (tc == 0)
            printf(" serial");
        else
            printf(" %u", tc);
    }
    printf("\n\n");

    // 5. Run benchmarks (creates/destroys devices internally, resizes pool per config).
    return runBenchmarks(rhi, taskPool.get());
}
