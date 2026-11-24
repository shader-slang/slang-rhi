// Benchmark for creating many small bottom-level acceleration structures.
//
// Mimics what a renderer does when loading a scene with hundreds of small triangle meshes and
// reports the CPU wall time spent in each phase, so allocation cost can be told apart from
// build cost.
//
// Usage:
//   blas-benchmark --device d3d12|vulkan|cuda [--count N] [--tris N] [--strategy per-blas|batched]
//                  [--geometry shared|per-mesh] [--scratch shared|per-blas]
//                  [--no-compact] [--iterations N] [--csv file]
//
// --strategy  per-blas: one command buffer and one submit per BLAS.
//             batched:  one command buffer for all BLASes.
// --geometry  shared (default): all meshes in one vertex and one index buffer.
// --scratch   shared (default): one scratch buffer, bump-allocated per BLAS.

#include <slang-rhi.h>
#include <slang-rhi/agility-sdk.h>

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

// Use the Agility SDK runtime if enabled, which is required for D3D12 tight alignment.
SLANG_RHI_EXPORT_AGILITY_SDK

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#define BENCH_NO_INLINE __declspec(noinline)
#else
#define BENCH_NO_INLINE __attribute__((noinline))
#endif

using namespace rhi;

namespace {

struct Options
{
    DeviceType deviceType = DeviceType::D3D12;
    const char* deviceName = "d3d12";
    uint32_t count = 500;
    uint32_t tris = 512;
    bool batched = false;
    /// One big vertex and one big index buffer for all meshes (true) or one pair per mesh.
    bool sharedGeometry = true;
    /// One scratch buffer sub-allocated with a bump allocator (true) or one scratch buffer per BLAS.
    bool sharedScratch = true;
    bool compact = true;
    uint32_t iterations = 3;
    /// Number of meshes merged into each BLAS as separate geometries.
    uint32_t geometriesPerBlas = 1;
    bool fastBuild = false;
    /// Use identical geometry for every mesh.
    bool sameMesh = false;
    const char* csvPath = nullptr;
};

enum Phase
{
    kGeometry,
    kSizes,
    kCreateAS,
    kCreateScratch,
    kEncodeSubmit,
    kWaitBuild,
    kReadSizes,
    kCreateCompactAS,
    kCompactSubmit,
    kWaitCompact,
    kReleaseBlas,
    kReleaseGeometry,
    kPhaseCount,
};

const char* kPhaseNames[kPhaseCount] = {
    "geometry-buffers",
    "get-sizes",
    "create-as",
    "create-scratch",
    "encode-submit-build",
    "wait-build",
    "read-compacted-sizes",
    "create-compact-as",
    "encode-submit-compact",
    "wait-compact",
    "release-blas",
    "release-geometry",
};

/// Returns the GPU memory currently used by this process in bytes (D3D12 only, 0 otherwise).
uint64_t getGpuMemoryUsage(IDevice* device)
{
#if defined(_WIN32)
    if (device->getInfo().deviceType != DeviceType::D3D12)
        return 0;
    DeviceNativeHandles handles;
    if (SLANG_FAILED(device->getNativeDeviceHandles(&handles)) || handles.handles[0].type != NativeHandleType::D3D12Device)
        return 0;
    ID3D12Device* d3dDevice = (ID3D12Device*)handles.handles[0].value;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter3> adapter;
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.writeRef()))) ||
        FAILED(factory->EnumAdapterByLuid(d3dDevice->GetAdapterLuid(), IID_PPV_ARGS(adapter.writeRef()))) ||
        FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return 0;
    return info.CurrentUsage;
#else
    return 0;
#endif
}

/// Returns true if the D3D12 device supports tight resource alignment.
bool hasTightAlignment(IDevice* device)
{
#if defined(_WIN32) && defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 618
    if (device->getInfo().deviceType != DeviceType::D3D12)
        return false;
    DeviceNativeHandles handles;
    if (SLANG_FAILED(device->getNativeDeviceHandles(&handles)) || handles.handles[0].type != NativeHandleType::D3D12Device)
        return false;
    ID3D12Device* d3dDevice = (ID3D12Device*)handles.handles[0].value;
    D3D12_FEATURE_DATA_TIGHT_ALIGNMENT tightAlignment = {};
    return SUCCEEDED(
               d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT, &tightAlignment, sizeof(tightAlignment))
           ) &&
           tightAlignment.SupportTier >= D3D12_TIGHT_ALIGNMENT_TIER_1;
#else
    return false;
#endif
}

struct Timer
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now();
    double lap()
    {
        Clock::time_point now = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - start).count();
        start = now;
        return ms;
    }
};

struct Mesh
{
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

/// Generate a bumpy grid with roughly `tris` triangles.
Mesh makeMesh(uint32_t tris, uint32_t seed)
{
    uint32_t n = 1;
    while (2 * n * n < tris)
        n++;
    Mesh mesh;
    uint32_t state = seed * 747796405u + 2891336453u;
    for (uint32_t y = 0; y <= n; y++)
    {
        for (uint32_t x = 0; x <= n; x++)
        {
            state = state * 1664525u + 1013904223u;
            float h = float(state >> 8) / float(1 << 24);
            mesh.vertices.push_back(float(x) / float(n) + float(seed));
            mesh.vertices.push_back(h * 0.1f);
            mesh.vertices.push_back(float(y) / float(n));
        }
    }
    for (uint32_t y = 0; y < n; y++)
    {
        for (uint32_t x = 0; x < n; x++)
        {
            uint32_t i = y * (n + 1) + x;
            mesh.indices.insert(mesh.indices.end(), {i, i + 1, i + n + 1, i + 1, i + n + 2, i + n + 1});
        }
    }
    return mesh;
}

#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        Result _r = (x);                                                                                               \
        if (SLANG_FAILED(_r))                                                                                          \
        {                                                                                                              \
            fprintf(stderr, "FAILED (0x%08x): %s (line %d)\n", (unsigned)_r, #x, __LINE__);                            \
            exit(1);                                                                                                   \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

// The functions below are deliberately not inlined so that each phase shows up under its own
// name in a sampling profiler.

BENCH_NO_INLINE void phaseGeometry(
    IDevice* device,
    const Options& options,
    const std::vector<Mesh>& meshes,
    std::vector<BufferOffsetPair>& vertexBuffers,
    std::vector<BufferOffsetPair>& indexBuffers,
    std::vector<ComPtr<IBuffer>>& buffers
)
{
    BufferDesc desc = {};
    desc.usage = BufferUsage::ShaderResource | BufferUsage::AccelerationStructureBuildInput;
    desc.defaultState = ResourceState::AccelerationStructureBuildInput;
    if (options.sharedGeometry)
    {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        std::vector<uint64_t> vertexOffsets, indexOffsets;
        for (const Mesh& mesh : meshes)
        {
            vertexOffsets.push_back(vertices.size() * sizeof(float));
            indexOffsets.push_back(indices.size() * sizeof(uint32_t));
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        }
        desc.size = vertices.size() * sizeof(float);
        ComPtr<IBuffer> vb;
        CHECK(device->createBuffer(desc, vertices.data(), vb.writeRef()));
        desc.size = indices.size() * sizeof(uint32_t);
        ComPtr<IBuffer> ib;
        CHECK(device->createBuffer(desc, indices.data(), ib.writeRef()));
        buffers.push_back(vb);
        buffers.push_back(ib);
        for (size_t i = 0; i < meshes.size(); i++)
        {
            vertexBuffers.push_back(BufferOffsetPair(vb.get(), vertexOffsets[i]));
            indexBuffers.push_back(BufferOffsetPair(ib.get(), indexOffsets[i]));
        }
    }
    else
    {
        for (const Mesh& mesh : meshes)
        {
            desc.size = mesh.vertices.size() * sizeof(float);
            ComPtr<IBuffer> vb;
            CHECK(device->createBuffer(desc, mesh.vertices.data(), vb.writeRef()));
            desc.size = mesh.indices.size() * sizeof(uint32_t);
            ComPtr<IBuffer> ib;
            CHECK(device->createBuffer(desc, mesh.indices.data(), ib.writeRef()));
            buffers.push_back(vb);
            buffers.push_back(ib);
            vertexBuffers.push_back(BufferOffsetPair(vb.get()));
            indexBuffers.push_back(BufferOffsetPair(ib.get()));
        }
    }
}

BENCH_NO_INLINE void phaseSizes(
    IDevice* device,
    const std::vector<AccelerationStructureBuildDesc>& buildDescs,
    std::vector<AccelerationStructureSizes>& sizes
)
{
    for (size_t i = 0; i < buildDescs.size(); i++)
        CHECK(device->getAccelerationStructureSizes(buildDescs[i], &sizes[i]));
}

BENCH_NO_INLINE void phaseCreateAS(
    IDevice* device,
    const std::vector<uint64_t>& sizes,
    std::vector<ComPtr<IAccelerationStructure>>& result
)
{
    for (uint64_t size : sizes)
    {
        AccelerationStructureDesc desc = {};
        desc.kind = AccelerationStructureKind::BottomLevel;
        desc.size = size;
        ComPtr<IAccelerationStructure> as;
        CHECK(device->createAccelerationStructure(desc, as.writeRef()));
        result.push_back(as);
    }
}

BENCH_NO_INLINE void phaseCreateScratch(
    IDevice* device,
    const Options& options,
    const std::vector<AccelerationStructureSizes>& sizes,
    std::vector<ComPtr<IBuffer>>& scratchBuffers,
    std::vector<uint64_t>& scratchOffsets
)
{
    const uint64_t kAlignment = 256;
    BufferDesc desc = {};
    desc.usage = BufferUsage::UnorderedAccess;
    desc.defaultState = ResourceState::UnorderedAccess;
    if (options.sharedScratch)
    {
        uint64_t total = 0;
        for (const auto& s : sizes)
        {
            scratchOffsets.push_back(total);
            total += (s.scratchSize + kAlignment - 1) & ~(kAlignment - 1);
        }
        desc.size = total;
        ComPtr<IBuffer> scratch;
        CHECK(device->createBuffer(desc, nullptr, scratch.writeRef()));
        scratchBuffers.push_back(scratch);
    }
    else
    {
        for (const auto& s : sizes)
        {
            desc.size = s.scratchSize;
            ComPtr<IBuffer> scratch;
            CHECK(device->createBuffer(desc, nullptr, scratch.writeRef()));
            scratchBuffers.push_back(scratch);
            scratchOffsets.push_back(0);
        }
    }
}

BENCH_NO_INLINE void phaseEncodeSubmitBuild(
    ICommandQueue* queue,
    const Options& options,
    const std::vector<AccelerationStructureBuildDesc>& buildDescs,
    const std::vector<ComPtr<IAccelerationStructure>>& draftAS,
    const std::vector<ComPtr<IBuffer>>& scratchBuffers,
    const std::vector<uint64_t>& scratchOffsets,
    IQueryPool* queryPool
)
{
    ComPtr<ICommandEncoder> encoder;
    for (size_t i = 0; i < buildDescs.size(); i++)
    {
        if (!encoder)
            CHECK(queue->createCommandEncoder(encoder.writeRef()));
        AccelerationStructureQueryDesc queryDesc = {};
        queryDesc.queryPool = queryPool;
        queryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        queryDesc.firstQueryIndex = uint32_t(i);
        IBuffer* scratch = options.sharedScratch ? scratchBuffers[0].get() : scratchBuffers[i].get();
        encoder->buildAccelerationStructure(
            buildDescs[i],
            draftAS[i],
            nullptr,
            BufferOffsetPair(scratch, scratchOffsets[i]),
            options.compact ? 1 : 0,
            options.compact ? &queryDesc : nullptr
        );
        if (!options.batched || i + 1 == buildDescs.size())
        {
            ComPtr<ICommandBuffer> commandBuffer;
            CHECK(encoder->finish(commandBuffer.writeRef()));
            CHECK(queue->submit(commandBuffer));
            encoder = nullptr;
        }
    }
}

BENCH_NO_INLINE void phaseWait(ICommandQueue* queue)
{
    CHECK(queue->waitOnHost());
}

BENCH_NO_INLINE void phaseCompactSubmit(
    ICommandQueue* queue,
    const Options& options,
    const std::vector<ComPtr<IAccelerationStructure>>& draftAS,
    const std::vector<ComPtr<IAccelerationStructure>>& finalAS
)
{
    ComPtr<ICommandEncoder> encoder;
    for (size_t i = 0; i < draftAS.size(); i++)
    {
        if (!encoder)
            CHECK(queue->createCommandEncoder(encoder.writeRef()));
        encoder->copyAccelerationStructure(finalAS[i], draftAS[i], AccelerationStructureCopyMode::Compact);
        if (!options.batched || i + 1 == draftAS.size())
        {
            ComPtr<ICommandBuffer> commandBuffer;
            CHECK(encoder->finish(commandBuffer.writeRef()));
            CHECK(queue->submit(commandBuffer));
            encoder = nullptr;
        }
    }
}

BENCH_NO_INLINE void phaseReleaseBlas(
    ICommandQueue* queue,
    std::vector<ComPtr<IAccelerationStructure>>& draftAS,
    std::vector<ComPtr<IAccelerationStructure>>& finalAS,
    std::vector<ComPtr<IBuffer>>& scratchBuffers
)
{
    draftAS.clear();
    finalAS.clear();
    scratchBuffers.clear();
    CHECK(queue->waitOnHost());
}

BENCH_NO_INLINE void phaseReleaseGeometry(ICommandQueue* queue, std::vector<ComPtr<IBuffer>>& buffers)
{
    buffers.clear();
    CHECK(queue->waitOnHost());
}

struct IterationResult
{
    double ms[kPhaseCount] = {};
    uint64_t draftBytes = 0;
    uint64_t scratchBytes = 0;
    uint64_t compactBytes = 0;
    /// Growth in GPU memory used by the process while all BLASes are alive (D3D12 only).
    uint64_t gpuMemoryBytes = 0;
    double total() const
    {
        double t = 0;
        for (double v : ms)
            t += v;
        return t;
    }
    /// Everything except uploading and releasing the input geometry.
    double blasTotal() const { return total() - ms[kGeometry] - ms[kReleaseGeometry]; }
};

BENCH_NO_INLINE IterationResult runIteration(
    IDevice* device,
    ICommandQueue* queue,
    const Options& options,
    const std::vector<Mesh>& meshes
)
{
    IterationResult result;
    uint64_t gpuMemoryBefore = getGpuMemoryUsage(device);
    Timer timer;

    std::vector<BufferOffsetPair> vertexBuffers, indexBuffers;
    std::vector<ComPtr<IBuffer>> geometryBuffers;
    phaseGeometry(device, options, meshes, vertexBuffers, indexBuffers, geometryBuffers);
    CHECK(queue->waitOnHost());
    result.ms[kGeometry] = timer.lap();

    std::vector<AccelerationStructureBuildInput> inputs(meshes.size(), AccelerationStructureBuildInput{});
    const size_t blasCount = meshes.size() / options.geometriesPerBlas;
    std::vector<AccelerationStructureBuildDesc> buildDescs(blasCount);
    for (size_t i = 0; i < meshes.size(); i++)
    {
        AccelerationStructureBuildInput& input = inputs[i];
        input.type = AccelerationStructureBuildInputType::Triangles;
        input.triangles.vertexBuffers[0] = vertexBuffers[i];
        input.triangles.vertexBufferCount = 1;
        input.triangles.vertexFormat = Format::RGB32Float;
        input.triangles.vertexCount = uint32_t(meshes[i].vertices.size() / 3);
        input.triangles.vertexStride = 3 * sizeof(float);
        input.triangles.indexBuffer = indexBuffers[i];
        input.triangles.indexFormat = IndexFormat::Uint32;
        input.triangles.indexCount = uint32_t(meshes[i].indices.size());
        input.triangles.flags = AccelerationStructureGeometryFlags::Opaque;
    }
    for (size_t i = 0; i < blasCount; i++)
    {
        buildDescs[i].inputs = &inputs[i * options.geometriesPerBlas];
        buildDescs[i].inputCount = options.geometriesPerBlas;
        buildDescs[i].flags = options.fastBuild ? AccelerationStructureBuildFlags::PreferFastBuild
                                                : AccelerationStructureBuildFlags::PreferFastTrace;
        if (options.compact)
            buildDescs[i].flags |= AccelerationStructureBuildFlags::AllowCompaction;
    }
    timer.lap();

    std::vector<AccelerationStructureSizes> sizes(blasCount);
    phaseSizes(device, buildDescs, sizes);
    result.ms[kSizes] = timer.lap();

    std::vector<uint64_t> draftSizes;
    for (const auto& s : sizes)
    {
        draftSizes.push_back(s.accelerationStructureSize);
        result.draftBytes += s.accelerationStructureSize;
        result.scratchBytes += s.scratchSize;
    }
    std::vector<ComPtr<IAccelerationStructure>> draftAS;
    timer.lap();
    phaseCreateAS(device, draftSizes, draftAS);
    result.ms[kCreateAS] = timer.lap();

    std::vector<ComPtr<IBuffer>> scratchBuffers;
    std::vector<uint64_t> scratchOffsets;
    phaseCreateScratch(device, options, sizes, scratchBuffers, scratchOffsets);
    result.ms[kCreateScratch] = timer.lap();

    ComPtr<IQueryPool> queryPool;
    if (options.compact)
    {
        QueryPoolDesc queryPoolDesc = {};
        queryPoolDesc.count = uint32_t(blasCount);
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        CHECK(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));
        queryPool->reset();
    }
    timer.lap();

    phaseEncodeSubmitBuild(queue, options, buildDescs, draftAS, scratchBuffers, scratchOffsets, queryPool);
    result.ms[kEncodeSubmit] = timer.lap();

    phaseWait(queue);
    result.ms[kWaitBuild] = timer.lap();

    std::vector<ComPtr<IAccelerationStructure>> finalAS;
    if (options.compact)
    {
        std::vector<uint64_t> compactSizes(blasCount);
        CHECK(queryPool->getResult(0, uint32_t(blasCount), compactSizes.data()));
        for (uint64_t s : compactSizes)
            result.compactBytes += s;
        result.ms[kReadSizes] = timer.lap();

        phaseCreateAS(device, compactSizes, finalAS);
        result.ms[kCreateCompactAS] = timer.lap();

        phaseCompactSubmit(queue, options, draftAS, finalAS);
        result.ms[kCompactSubmit] = timer.lap();

        phaseWait(queue);
        result.ms[kWaitCompact] = timer.lap();
    }

    uint64_t gpuMemoryAfter = getGpuMemoryUsage(device);
    result.gpuMemoryBytes = gpuMemoryAfter > gpuMemoryBefore ? gpuMemoryAfter - gpuMemoryBefore : 0;
    timer.lap();

    // Release everything and flush deferred deletes.
    phaseReleaseBlas(queue, draftAS, finalAS, scratchBuffers);
    queryPool = nullptr;
    result.ms[kReleaseBlas] = timer.lap();

    phaseReleaseGeometry(queue, geometryBuffers);
    result.ms[kReleaseGeometry] = timer.lap();

    return result;
}

bool parseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto next = [&]() -> const char*
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing value for %s\n", arg.c_str());
                exit(1);
            }
            return argv[++i];
        };
        if (arg == "--device")
        {
            options.deviceName = next();
            std::string name = options.deviceName;
            if (name == "d3d12")
                options.deviceType = DeviceType::D3D12;
            else if (name == "vulkan")
                options.deviceType = DeviceType::Vulkan;
            else if (name == "cuda")
                options.deviceType = DeviceType::CUDA;
            else
                return false;
        }
        else if (arg == "--count")
            options.count = uint32_t(atoi(next()));
        else if (arg == "--tris")
            options.tris = uint32_t(atoi(next()));
        else if (arg == "--strategy")
        {
            std::string s = next();
            if (s == "batched")
                options.batched = true;
            else if (s == "per-blas")
                options.batched = false;
            else
                return false;
        }
        else if (arg == "--geometry")
        {
            std::string g = next();
            if (g == "shared")
                options.sharedGeometry = true;
            else if (g == "per-mesh")
                options.sharedGeometry = false;
            else
                return false;
        }
        else if (arg == "--scratch")
        {
            std::string g = next();
            if (g == "shared")
                options.sharedScratch = true;
            else if (g == "per-blas")
                options.sharedScratch = false;
            else
                return false;
        }
        else if (arg == "--no-compact")
            options.compact = false;
        else if (arg == "--geometries-per-blas")
            options.geometriesPerBlas = uint32_t(atoi(next()));
        else if (arg == "--fast-build")
            options.fastBuild = true;
        else if (arg == "--same-mesh")
            options.sameMesh = true;
        else if (arg == "--iterations")
            options.iterations = uint32_t(atoi(next()));
        else if (arg == "--csv")
            options.csvPath = next();
        else
            return false;
    }
    return options.count > 0 && options.tris > 0 && options.iterations > 0 && options.geometriesPerBlas > 0 &&
           options.count % options.geometriesPerBlas == 0;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options))
    {
        fprintf(
            stderr,
            "Usage: blas-benchmark --device d3d12|vulkan|cuda [--count N] [--tris N]\n"
            "                      [--strategy per-blas|batched] [--geometry shared|per-mesh]\n"
            "                      [--scratch shared|per-blas] [--no-compact] [--iterations N] [--csv file]\n"
        );
        return 1;
    }

    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = options.deviceType;
#ifdef SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR
    if (options.deviceType == DeviceType::CUDA)
        deviceDesc.optixDeviceHeaderRoot = SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR;
#endif
    ComPtr<IDevice> device;
    CHECK(getRHI()->createDevice(deviceDesc, device.writeRef()));
    if (!device->hasFeature(Feature::AccelerationStructure))
    {
        fprintf(stderr, "Device does not support acceleration structures\n");
        return 1;
    }
    ComPtr<ICommandQueue> queue;
    CHECK(device->getQueue(QueueType::Graphics, queue.writeRef()));

    std::vector<Mesh> meshes;
    for (uint32_t i = 0; i < options.count; i++)
        meshes.push_back(makeMesh(options.tris, options.sameMesh ? 0 : i));

    const char* strategy = options.batched ? "batched" : "per-blas";
    printf(
        "device=%s adapter=\"%s\" count=%u tris=%u strategy=%s geometry=%s scratch=%s compact=%d\n",
        options.deviceName,
        device->getInfo().adapterName,
        options.count,
        uint32_t(meshes[0].indices.size() / 3),
        strategy,
        options.sharedGeometry ? "shared" : "per-mesh",
        options.sharedScratch ? "shared" : "per-blas",
        options.compact ? 1 : 0
    );

    std::vector<IterationResult> results;
    for (uint32_t i = 0; i < options.iterations; i++)
        results.push_back(runIteration(device, queue, options, meshes));

    printf("%-24s", "phase (ms)");
    for (uint32_t i = 0; i < options.iterations; i++)
        printf("%10s%u", "iter", i);
    printf("\n");
    for (int p = 0; p < kPhaseCount; p++)
    {
        printf("%-24s", kPhaseNames[p]);
        for (const auto& r : results)
            printf("%11.2f", r.ms[p]);
        printf("\n");
    }
    printf("%-24s", "total");
    for (const auto& r : results)
        printf("%11.2f", r.total());
    printf("\n%-24s", "total-without-geometry");
    for (const auto& r : results)
        printf("%11.2f", r.blasTotal());
    printf(
        "\nmemory: draft=%.2f MB scratch=%.2f MB compacted=%.2f MB\n",
        double(results[0].draftBytes) / (1024.0 * 1024.0),
        double(results[0].scratchBytes) / (1024.0 * 1024.0),
        double(results[0].compactBytes) / (1024.0 * 1024.0)
    );

    if (options.deviceType == DeviceType::D3D12)
    {
        uint64_t maxGpuMemory = 0;
        for (const auto& r : results)
            maxGpuMemory = std::max(maxGpuMemory, r.gpuMemoryBytes);
        printf(
            "d3d12: tight-alignment=%d gpu-memory-growth=%.2f MB (max over iterations, includes geometry and scratch)\n",
            hasTightAlignment(device) ? 1 : 0,
            double(maxGpuMemory) / (1024.0 * 1024.0)
        );
    }

    if (options.csvPath)
    {
        FILE* f = fopen(options.csvPath, "a");
        if (!f)
        {
            fprintf(stderr, "Cannot open %s\n", options.csvPath);
            return 1;
        }
        for (uint32_t i = 0; i < options.iterations; i++)
        {
            for (int p = 0; p < kPhaseCount; p++)
            {
                fprintf(
                    f,
                    "%s,%s,%u,%u,%d,%u,%s,%.4f\n",
                    options.deviceName,
                    strategy,
                    options.count,
                    options.tris,
                    options.compact ? 1 : 0,
                    i,
                    kPhaseNames[p],
                    results[i].ms[p]
                );
            }
        }
        fclose(f);
    }
    return 0;
}
