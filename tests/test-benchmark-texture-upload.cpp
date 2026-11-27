// Texture upload benchmark.
//
// Measures the cost of uploading texture data through the different paths offered by slang-rhi:
// - createTexture() with init data
// - ICommandEncoder::uploadTextureData() into an existing texture
// and compares against baselines (host memcpy, uploadBufferData of the same byte count).
//
// The benchmark is skipped unless SLANG_RHI_RUN_BENCHMARKS is set in the environment.
// Run it from a Release build (Debug builds enable validation layers, which distort timings):
//
//   set SLANG_RHI_RUN_BENCHMARKS=1
//   build/Release/slang-rhi-tests -tc="benchmark-texture-upload*"

#include "testing.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

bool benchmarksEnabled()
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* value = std::getenv("SLANG_RHI_RUN_BENCHMARKS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return value && value[0] != '\0' && value[0] != '0';
}

using Clock = std::chrono::high_resolution_clock;

double msSince(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    return (n % 2) ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

double gbps(size_t bytes, double ms)
{
    return ms > 0.0 ? (double(bytes) / (1024.0 * 1024.0 * 1024.0)) / (ms / 1000.0) : 0.0;
}

struct BenchCase
{
    const char* name;
    Format format;
    uint32_t bytesPerTexel;
    uint32_t size;
    bool mips;
    uint32_t textureCount; // > 1: upload many textures in a single command encoder
};

// Host-side data for one texture (all mips, single layer).
struct HostTexture
{
    TextureDesc desc;
    std::vector<uint8_t> data;
    std::vector<SubresourceData> subresources;
    size_t byteCount = 0;
};

void initHostTexture(HostTexture& t, const BenchCase& c, uint32_t seed)
{
    t.desc = {};
    t.desc.type = TextureType::Texture2D;
    t.desc.format = c.format;
    t.desc.size = {c.size, c.size, 1};
    t.desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination | TextureUsage::CopySource;
    t.desc.defaultState = ResourceState::ShaderResource;

    uint32_t mipCount = 1;
    if (c.mips)
    {
        for (uint32_t s = c.size; s > 1; s >>= 1)
            mipCount++;
    }
    t.desc.mipCount = mipCount;

    size_t total = 0;
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        uint32_t s = std::max(1u, c.size >> mip);
        total += size_t(s) * s * c.bytesPerTexel;
    }
    t.data.resize(total);
    // Touch every page so source page faults are not part of the measurement.
    uint32_t state = seed * 2654435761u + 1;
    uint32_t* words = reinterpret_cast<uint32_t*>(t.data.data());
    for (size_t i = 0; i < total / 4; ++i)
    {
        state = state * 1664525u + 1013904223u;
        words[i] = state;
    }
    t.byteCount = total;

    t.subresources.clear();
    size_t offset = 0;
    for (uint32_t mip = 0; mip < mipCount; ++mip)
    {
        uint32_t s = std::max(1u, c.size >> mip);
        SubresourceData sr;
        sr.data = t.data.data() + offset;
        sr.rowPitch = size_t(s) * c.bytesPerTexel;
        sr.slicePitch = sr.rowPitch * s;
        t.subresources.push_back(sr);
        offset += sr.slicePitch;
    }
}

uint32_t iterationsFor(size_t bytes)
{
    // Aim for ~1 GB of traffic per measurement, clamped to [5, 50] iterations.
    size_t target = size_t(1) << 30;
    size_t n = bytes ? target / bytes : 50;
    return uint32_t(std::clamp<size_t>(n, 5, 50));
}

} // namespace

GPU_TEST_CASE("benchmark-texture-upload", D3D12 | Vulkan | CUDA)
{
    if (!benchmarksEnabled())
        SKIP("set SLANG_RHI_RUN_BENCHMARKS=1 to run");

    const BenchCase cases[] = {
        {"rgba8 256", Format::RGBA8Unorm, 4, 256, false, 1},
        {"rgba8 1024", Format::RGBA8Unorm, 4, 1024, false, 1},
        {"rgba8 1024 mips", Format::RGBA8Unorm, 4, 1024, true, 1},
        {"rgba8 2048", Format::RGBA8Unorm, 4, 2048, false, 1},
        {"rgba8 4096", Format::RGBA8Unorm, 4, 4096, false, 1},
        {"rgba8 4096 mips", Format::RGBA8Unorm, 4, 4096, true, 1},
        {"rgba32f 2048", Format::RGBA32Float, 16, 2048, false, 1},
        {"rgba32f 4096", Format::RGBA32Float, 16, 4096, false, 1},
        {"64x rgba8 256", Format::RGBA8Unorm, 4, 256, false, 64},
        {"64x rgba8 512 mips", Format::RGBA8Unorm, 4, 512, true, 64},
    };

    auto queue = device->getQueue(QueueType::Graphics);
    const char* deviceName = deviceTypeToString(device->getDeviceType());

    bool hasTimestamps = device->hasFeature(Feature::TimestampQuery);
    ComPtr<IQueryPool> queryPool;
    if (hasTimestamps)
    {
        QueryPoolDesc qpDesc = {};
        qpDesc.type = QueryType::Timestamp;
        qpDesc.count = 2;
        if (SLANG_FAILED(device->createQueryPool(qpDesc, queryPool.writeRef())))
            hasTimestamps = false;
    }
    double timestampFrequency = double(device->getInfo().timestampFrequency);

    fprintf(
        stdout,
        "\n=== texture upload benchmark: %s (%s) ===\n"
        "all times are medians in ms; GB/s computed against the payload size\n"
        "  create      = createTexture(nullptr)\n"
        "  create+init = createTexture(initData) + waitOnHost\n"
        "  upload      = uploadTextureData + submit + waitOnHost (existing texture)\n"
        "    encode    = createCommandEncoder + uploadTextureData (CPU: staging alloc + copy)\n"
        "    submit    = finish + submit\n"
        "    wait      = waitOnHost\n"
        "    gpu       = timestamp delta around the copy\n"
        "  memcpy      = host memcpy of the same bytes (CPU baseline)\n"
        "  buf upload  = uploadBufferData of the same bytes + submit + wait\n\n",
        deviceName,
        device->getInfo().adapterName
    );
    fprintf(
        stdout,
        "%-20s %9s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
        "case",
        "MB",
        "create",
        "cr+init",
        "GB/s",
        "upload",
        "GB/s",
        "encode",
        "submit",
        "wait",
        "gpu",
        "memcpy",
        "bufupl"
    );
    fflush(stdout);

    for (const BenchCase& c : cases)
    {
        // Prepare host data for all textures in the case.
        std::vector<HostTexture> hostTextures(c.textureCount);
        size_t totalBytes = 0;
        for (uint32_t i = 0; i < c.textureCount; ++i)
        {
            initHostTexture(hostTextures[i], c, i + 1);
            totalBytes += hostTextures[i].byteCount;
        }

        FormatSupport formatSupport = FormatSupport::None;
        device->getFormatSupport(c.format, &formatSupport);
        if (!is_set(formatSupport, FormatSupport::CopyDestination))
            continue;

        uint32_t iterations = iterationsFor(totalBytes);
        uint32_t warmup = 2;

        // --- createTexture(nullptr) ---
        std::vector<double> createTimes;
        for (uint32_t it = 0; it < warmup + iterations; ++it)
        {
            std::vector<ComPtr<ITexture>> textures(c.textureCount);
            queue->waitOnHost();
            auto t0 = Clock::now();
            for (uint32_t i = 0; i < c.textureCount; ++i)
                REQUIRE_CALL(device->createTexture(hostTextures[i].desc, nullptr, textures[i].writeRef()));
            queue->waitOnHost();
            auto t1 = Clock::now();
            if (it >= warmup)
                createTimes.push_back(msSince(t0, t1));
        }

        // --- createTexture(initData) ---
        std::vector<double> createInitTimes;
        for (uint32_t it = 0; it < warmup + iterations; ++it)
        {
            std::vector<ComPtr<ITexture>> textures(c.textureCount);
            queue->waitOnHost();
            auto t0 = Clock::now();
            for (uint32_t i = 0; i < c.textureCount; ++i)
                REQUIRE_CALL(device->createTexture(
                    hostTextures[i].desc,
                    hostTextures[i].subresources.data(),
                    textures[i].writeRef()
                ));
            queue->waitOnHost();
            auto t1 = Clock::now();
            if (it >= warmup)
                createInitTimes.push_back(msSince(t0, t1));
        }

        // --- uploadTextureData into existing textures ---
        std::vector<ComPtr<ITexture>> textures(c.textureCount);
        for (uint32_t i = 0; i < c.textureCount; ++i)
            REQUIRE_CALL(device->createTexture(hostTextures[i].desc, nullptr, textures[i].writeRef()));

        std::vector<double> uploadTimes, encodeTimes, submitTimes, waitTimes, gpuTimes;
        for (uint32_t it = 0; it < warmup + iterations; ++it)
        {
            queue->waitOnHost();
            auto t0 = Clock::now();
            auto encoder = queue->createCommandEncoder();
            if (hasTimestamps)
                encoder->writeTimestamp(queryPool, 0);
            for (uint32_t i = 0; i < c.textureCount; ++i)
            {
                const HostTexture& ht = hostTextures[i];
                REQUIRE_CALL(encoder->uploadTextureData(
                    textures[i],
                    {0, 1, 0, ht.desc.mipCount},
                    {0, 0, 0},
                    Extent3D::kWholeTexture,
                    ht.subresources.data(),
                    uint32_t(ht.subresources.size())
                ));
            }
            if (hasTimestamps)
                encoder->writeTimestamp(queryPool, 1);
            auto t1 = Clock::now();
            REQUIRE_CALL(queue->submit(encoder->finish()));
            auto t2 = Clock::now();
            REQUIRE_CALL(queue->waitOnHost());
            auto t3 = Clock::now();
            if (it >= warmup)
            {
                encodeTimes.push_back(msSince(t0, t1));
                submitTimes.push_back(msSince(t1, t2));
                waitTimes.push_back(msSince(t2, t3));
                uploadTimes.push_back(msSince(t0, t3));
                if (hasTimestamps)
                {
                    uint64_t ts[2] = {};
                    if (SLANG_SUCCEEDED(queryPool->getResult(0, 2, ts)) && timestampFrequency > 0.0)
                        gpuTimes.push_back(double(ts[1] - ts[0]) * 1000.0 / timestampFrequency);
                }
            }
        }

        // --- baseline: host memcpy of the same payload ---
        std::vector<double> memcpyTimes;
        {
            std::vector<uint8_t> dst(totalBytes);
            memset(dst.data(), 0, dst.size()); // pre-fault
            for (uint32_t it = 0; it < warmup + iterations; ++it)
            {
                auto t0 = Clock::now();
                size_t offset = 0;
                for (const HostTexture& ht : hostTextures)
                {
                    memcpy(dst.data() + offset, ht.data.data(), ht.byteCount);
                    offset += ht.byteCount;
                }
                auto t1 = Clock::now();
                if (it >= warmup)
                    memcpyTimes.push_back(msSince(t0, t1));
            }
        }

        // --- baseline: uploadBufferData of the same payload ---
        std::vector<double> bufferUploadTimes;
        {
            ComPtr<IBuffer> buffer;
            BufferDesc bufDesc = {};
            bufDesc.size = totalBytes;
            bufDesc.usage = BufferUsage::CopyDestination | BufferUsage::ShaderResource;
            bufDesc.defaultState = ResourceState::ShaderResource;
            bufDesc.memoryType = MemoryType::DeviceLocal;
            REQUIRE_CALL(device->createBuffer(bufDesc, nullptr, buffer.writeRef()));
            for (uint32_t it = 0; it < warmup + iterations; ++it)
            {
                queue->waitOnHost();
                auto t0 = Clock::now();
                auto encoder = queue->createCommandEncoder();
                size_t offset = 0;
                for (const HostTexture& ht : hostTextures)
                {
                    REQUIRE_CALL(encoder->uploadBufferData(buffer, offset, ht.byteCount, ht.data.data()));
                    offset += ht.byteCount;
                }
                REQUIRE_CALL(queue->submit(encoder->finish()));
                REQUIRE_CALL(queue->waitOnHost());
                auto t1 = Clock::now();
                if (it >= warmup)
                    bufferUploadTimes.push_back(msSince(t0, t1));
            }
        }

        double create = median(createTimes);
        double createInit = median(createInitTimes);
        double upload = median(uploadTimes);
        std::string gpuStr = "n/a";
        if (!gpuTimes.empty())
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.3f", median(gpuTimes));
            gpuStr = buf;
        }

        fprintf(
            stdout,
            "%-20s %9.2f %8.3f %8.3f %8.2f %8.3f %8.2f %8.3f %8.3f %8.3f %8s %8.3f %8.3f\n",
            c.name,
            double(totalBytes) / (1024.0 * 1024.0),
            create,
            createInit,
            gbps(totalBytes, createInit - create),
            upload,
            gbps(totalBytes, upload),
            median(encodeTimes),
            median(submitTimes),
            median(waitTimes),
            gpuStr.c_str(),
            median(memcpyTimes),
            median(bufferUploadTimes)
        );
        fflush(stdout);
    }
    fprintf(stdout, "\n");
}
