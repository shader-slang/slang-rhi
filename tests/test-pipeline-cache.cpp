#include "testing.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <mutex>

using namespace rhi;
using namespace rhi::testing;

class VirtualCache : public IPersistentCache
{
public:
    struct Stats
    {
        uint32_t writeCount = 0;
        uint32_t queryCount = 0;
        uint32_t missCount = 0;
        uint32_t hitCount = 0;
        uint32_t entryCount = 0;
    };

    using Key = std::vector<uint8_t>;
    using Data = std::vector<uint8_t>;

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
        m_stats = {};
    }

    void corrupt()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_entries)
        {
            // Corrupt the data.
            if (!entry.second.empty())
            {
                for (size_t i = 0; i < entry.second.size(); i += 100)
                {
                    entry.second[i] ^= 0xff; // Flip all bits in the byte.
                }
            }
        }
    }

    Stats getStats() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stats;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key_, ISlangBlob* data_) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        Data data(
            static_cast<const uint8_t*>(data_->getBufferPointer()),
            static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize()
        );
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.writeCount++;
        m_entries[key] = data;
        m_stats.entryCount = m_entries.size();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.queryCount++;
        auto it = m_entries.find(key);
        if (it == m_entries.end())
        {
            m_stats.missCount++;
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        m_stats.hitCount++;
        *outData = OwnedBlob::create(it->second.data(), it->second.size()).detach();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == IPersistentCache::getTypeGuid())
        {
            *outObject = static_cast<IPersistentCache*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override
    {
        // The lifetime of this object is tied to the test.
        // Do not perform any reference counting.
        return 2;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        // Returning 2 is important here, because when releasing a COM pointer, it checks
        // if the ref count **was 1 before releasing** in order to free the object.
        return 2;
    }

private:
    mutable std::mutex m_mutex;
    std::map<Key, Data> m_entries;
    Stats m_stats;
};


// Base class for pipeline cache tests.
struct PipelineCacheTest
{
    GpuTestContext* ctx;
    std::filesystem::path tempDirectory;
    VirtualCache pipelineCache;
    ComPtr<IDevice> device;

    void createDevice()
    {
        DeviceExtraOptions extraOptions;
        extraOptions.persistentPipelineCache = &pipelineCache;
        device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    }

    VirtualCache::Stats getStats() { return pipelineCache.getStats(); }

    void run(GpuTestContext* ctx_, std::string tempDirectory_)
    {
        ctx = ctx_;
        tempDirectory = tempDirectory_;

        pipelineCache.clear();
        remove_all(tempDirectory);
        create_directories(tempDirectory);

        runTests();

        remove_all(tempDirectory);
    }

    virtual void runTests() = 0;
};

template<bool Corrupt>
struct PipelineCacheTestCompute : PipelineCacheTest
{
    ComPtr<IComputePipeline> computePipeline;
    ComPtr<IBuffer> buffer;

    std::string computeShaderAdd = std::string(
        R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            var input = buffer[sv_dispatchThreadID.x];
            buffer[sv_dispatchThreadID.x] = input + 1.0f;
        }
        )"
    );

    std::string computeShaderMultiply = std::string(
        R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            var input = buffer[sv_dispatchThreadID.x];
            buffer[sv_dispatchThreadID.x] = input * 2.0f;
        }
        )"
    );

    void createResources()
    {
        const int numberCount = 4;
        float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
        BufferDesc bufferDesc = {};
        bufferDesc.size = numberCount * sizeof(float);
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                           BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));
    }

    void freeResources()
    {
        buffer = nullptr;
        computePipeline = nullptr;
    }

    void createComputePipeline(std::string_view shaderSource)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, shaderSource, shaderProgram.writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, computePipeline.writeRef()));
    }

    void dispatchComputePipeline()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(computePipeline);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(4, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    bool checkOutput(const std::vector<float>& expectedOutput)
    {
        ComPtr<ISlangBlob> bufferBlob;
        device->readBuffer(buffer, 0, 4 * sizeof(float), bufferBlob.writeRef());
        REQUIRE(bufferBlob);
        REQUIRE(bufferBlob->getBufferSize() == expectedOutput.size() * sizeof(float));
        return ::memcmp(bufferBlob->getBufferPointer(), expectedOutput.data(), bufferBlob->getBufferSize()) == 0;
    }

    void runComputePipeline(std::string_view shaderSource, const std::vector<float>& expectedOutput)
    {
        createResources();
        createComputePipeline(shaderSource);
        dispatchComputePipeline();
        CHECK(checkOutput(expectedOutput));
        freeResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 2 misses for 2 different programs.
        createDevice();
        if (!device->hasFeature(Feature::PipelineCache))
            SKIP("Pipeline cache is not supported on this device type.");
        runComputePipeline(computeShaderAdd, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderMultiply, {0.f, 2.f, 4.f, 6.f});
        CHECK_EQ(getStats().writeCount, 2);
        CHECK_EQ(getStats().queryCount, 2);
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Corrupt the cache.
        if constexpr (Corrupt)
        {
            pipelineCache.corrupt();
        }

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline(computeShaderAdd, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderMultiply, {0.f, 2.f, 4.f, 6.f});
        CHECK_EQ(getStats().writeCount, Corrupt ? 4 : 2);
        CHECK_EQ(getStats().queryCount, 4);
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

template<bool Corrupt>
struct PipelineCacheTestRender : PipelineCacheTest
{
    ComPtr<IRenderPipeline> renderPipeline;
    ComPtr<ITexture> texture;

    std::string renderShaderMagenta = std::string(
        R"(
        [shader("vertex")]
        float4 vertexMain(uint vid: SV_VertexID) : SV_Position
        {
            float2 uv = float2((vid << 1) & 2, vid & 2);
            return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
        }

        // Fragment Shader

        [shader("fragment")]
        float4 fragmentMain()
            : SV_Target
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        )"
    );

    std::string renderShaderCyan = std::string(
        R"(
        [shader("vertex")]
        float4 vertexMain(uint vid: SV_VertexID) : SV_Position
        {
            float2 uv = float2((vid << 1) & 2, vid & 2);
            return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
        }

        [shader("fragment")]
        float4 fragmentMain()
            : SV_Target
        {
            return float4(0.0, 1.0, 1.0, 1.0);
        }
        )"
    );

    void createResources()
    {
        TextureDesc textureDesc = {};
        textureDesc.format = Format::RGBA32Float;
        textureDesc.size = {2, 2, 1};
        textureDesc.usage = TextureUsage::CopySource | TextureUsage::RenderTarget;
        REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
    }

    void freeResources()
    {
        texture = nullptr;
        renderPipeline = nullptr;
    }

    void createRenderPipeline(std::string_view shaderSource, bool enableBlend)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadRenderProgramFromSource(device, shaderSource, "vertexMain", "fragmentMain", shaderProgram.writeRef())
        );

        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ColorTargetDesc colorTargetDesc = {};
        colorTargetDesc.format = Format::RGBA32Float;
        colorTargetDesc.enableBlend = enableBlend;
        pipelineDesc.targetCount = 1;
        pipelineDesc.targets = &colorTargetDesc;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, renderPipeline.writeRef()));
    }

    void dispatchRenderPipeline()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        RenderPassDesc renderPass = {};
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = texture->getDefaultView();
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);
        passEncoder->bindPipeline(renderPipeline);
        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(2, 2);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(2, 2);
        renderState.scissorRectCount = 1;
        passEncoder->setRenderState(renderState);
        DrawArguments drawArgs = {};
        drawArgs.vertexCount = 3;
        passEncoder->draw(drawArgs);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    bool checkOutput(const std::vector<float>& expectedOutput)
    {
        ComPtr<ISlangBlob> textureBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(texture, 0, 0, textureBlob.writeRef(), &layout));
        return ::memcmp(
                   textureBlob->getBufferPointer(),
                   expectedOutput.data(),
                   expectedOutput.size() * sizeof(float)
               ) == 0;
    }

    void runRenderPipeline(std::string_view shaderSource, bool enableBlend, const std::vector<float>& expectedOutput)
    {
        createResources();
        createRenderPipeline(shaderSource, enableBlend);
        dispatchRenderPipeline();
        CHECK(checkOutput(expectedOutput));
        freeResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 2 misses for different programs and blend configurations.
        createDevice();
        if (!device->hasFeature(Feature::PipelineCache))
            SKIP("Pipeline cache is not supported on this device type.");
        runRenderPipeline(renderShaderMagenta, false, {1.f, 0.f, 1.f, 1.f});
        runRenderPipeline(renderShaderCyan, true, {0.f, 1.f, 1.f, 1.f});
        CHECK_EQ(getStats().writeCount, 2);
        CHECK_EQ(getStats().queryCount, 2);
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Corrupt the cache.
        if constexpr (Corrupt)
        {
            pipelineCache.corrupt();
        }

        // Cache is hot and we expect 2 hits.
        createDevice();
        runRenderPipeline(renderShaderMagenta, false, {1.f, 0.f, 1.f, 1.f});
        runRenderPipeline(renderShaderCyan, true, {0.f, 1.f, 1.f, 1.f});
        CHECK_EQ(getStats().writeCount, Corrupt ? 4 : 2);
        CHECK_EQ(getStats().queryCount, 4);
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

template<bool Corrupt>
struct PipelineCacheTestRayTracing : PipelineCacheTest
{
    ComPtr<IRayTracingPipeline> rayTracingPipeline;

    void runRayTracingPipeline(
        const char* entryPointName,
        uint32_t value,
        uint32_t maxRecursion,
        RayTracingPipelineFlags flags,
        const std::array<uint32_t, 4>& expectedOutput
    )
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadProgram(device, "test-ray-tracing-raygen-entrypoint", entryPointName, shaderProgram.writeRef())
        );

        RayTracingPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        pipelineDesc.maxRecursion = maxRecursion;
        pipelineDesc.flags = flags;
        REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, rayTracingPipeline.writeRef()));

        ComPtr<IShaderTable> shaderTable;
        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = shaderProgram;
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &entryPointName;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

        BufferDesc bufferDesc = {};
        bufferDesc.size = expectedOutput.size() * sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        ComPtr<IBuffer> outputBuffer;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, outputBuffer.writeRef()));

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(rayTracingPipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData(value);
        passEncoder->dispatchRays(0, 2, 2, 1);
        passEncoder->end();
        REQUIRE_CALL(queue->submit(commandEncoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, outputBuffer, expectedOutput);

        rayTracingPipeline = nullptr;
    }

    void runTests()
    {
        // Cache is cold and we expect 3 misses for different programs and pipeline configurations.
        createDevice();
        if (!device->hasFeature(Feature::PipelineCache))
            SKIP("Pipeline cache is not supported on this device type.");
        if (!device->hasFeature(Feature::RayTracing))
            SKIP("Ray tracing is not supported on this device type.");
        runRayTracingPipeline("rayGenA", 1, 1, RayTracingPipelineFlags::None, {1, 2, 3, 4});
        runRayTracingPipeline("rayGenB", 10, 1, RayTracingPipelineFlags::None, {10, 12, 14, 16});
        runRayTracingPipeline("rayGenA", 100, 2, RayTracingPipelineFlags::None, {100, 101, 102, 103});
        CHECK_EQ(getStats().writeCount, 3);
        CHECK_EQ(getStats().queryCount, 3);
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Corrupt the cache.
        if constexpr (Corrupt)
        {
            pipelineCache.corrupt();
        }

        // Cache is hot and we expect 3 hits.
        createDevice();
        runRayTracingPipeline("rayGenA", 1, 1, RayTracingPipelineFlags::None, {1, 2, 3, 4});
        runRayTracingPipeline("rayGenB", 10, 1, RayTracingPipelineFlags::None, {10, 12, 14, 16});
        runRayTracingPipeline("rayGenA", 100, 2, RayTracingPipelineFlags::None, {100, 101, 102, 103});
        CHECK_EQ(getStats().writeCount, Corrupt ? 6 : 3);
        CHECK_EQ(getStats().queryCount, 6);
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 3);
    }
};

template<typename T>
void runTest(GpuTestContext* ctx)
{
    std::string tempDirectory = getCaseTempDirectory();
    T test;
    test.run(ctx, tempDirectory);
}

GPU_TEST_CASE("pipeline-cache-compute", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestCompute<false>>(ctx);
}

#if 0
// TODO: D3D12 does fail in debug layers and not return an error correctly.
GPU_TEST_CASE("pipeline-cache-compute-corrupt", Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestCompute<true>>(ctx);
}
#endif

GPU_TEST_CASE("pipeline-cache-render", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestRender<false>>(ctx);
}

GPU_TEST_CASE("pipeline-cache-ray-tracing", Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestRayTracing<false>>(ctx);
}

GPU_TEST_CASE("pipeline-cache-ray-tracing-corrupt", Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestRayTracing<true>>(ctx);
}


#if 0
// TODO: D3D12 does fail in debug layers and not return an error correctly.
GPU_TEST_CASE("pipeline-cache-render-corrupt", Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestRender<true>>(ctx);
}
#endif


#if SLANG_RHI_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include "core/short_vector.h"

namespace rhi::vk {
// Declared here rather than via vk-pipeline.h, which pulls in the full Vulkan API loader.
Result parsePipelineCacheBlob(
    const void* blobData,
    size_t blobSize,
    short_vector<VkPipelineBinaryKeyKHR>& outKeys,
    short_vector<VkPipelineBinaryDataKHR>& outData
);
} // namespace rhi::vk

// A cache entry whose header passes the magic/version check but carries an out-of-range length or
// offset must be rejected rather than trusted: the key size drives a copy into a fixed-size array,
// and the data offset becomes a pointer handed to the driver.
//
// The parser is exercised directly because reaching it through the cache requires a device
// supporting VK_KHR_pipeline_binary, which the pipeline-cache-* tests above skip without.
// VirtualCache::corrupt() cannot reach these checks either: its first flipped byte falls in the magic
// field, so a corrupted entry is rejected before any record is examined.
TEST_CASE("pipeline-cache-blob-validation")
{
    // Mirrors the layout written by serializePipelineBinaries.
    struct Header
    {
        uint32_t magic = 0x12345678;
        uint32_t version = 1;
        uint32_t binaryCount = 1;
    };
    struct Record
    {
        uint32_t keySize = VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
        uint8_t key[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR] = {};
        uint32_t dataSize = 16;
        uint32_t dataOffset = 56;
    };
    // Must match the structs the Vulkan backend writes; asserted there too.
    static_assert(sizeof(Header) == 12);
    static_assert(sizeof(Record) == 44);

    auto build = [](const Header& header, const Record& record)
    {
        std::vector<uint8_t> blob(sizeof(Header) + sizeof(Record) + 16, 0xab);
        std::memcpy(blob.data(), &header, sizeof(header));
        std::memcpy(blob.data() + sizeof(Header), &record, sizeof(record));
        return blob;
    };
    auto parse = [](const std::vector<uint8_t>& blob)
    {
        short_vector<VkPipelineBinaryKeyKHR> keys;
        short_vector<VkPipelineBinaryDataKHR> data;
        return rhi::vk::parsePipelineCacheBlob(blob.data(), blob.size(), keys, data);
    };

    SUBCASE("blob at the limits of the valid range is accepted")
    {
        // The rejection cases below are only meaningful if these pass, and each sits exactly on a
        // bound: data starting at the first byte after the record table, and an empty payload at the
        // very end of the blob.
        Record atTableEnd;
        atTableEnd.dataOffset = sizeof(Header) + sizeof(Record);
        atTableEnd.dataSize = 16;
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, atTableEnd))));

        Record emptyAtBlobEnd;
        emptyAtBlobEnd.dataOffset = sizeof(Header) + sizeof(Record) + 16;
        emptyAtBlobEnd.dataSize = 0;
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, emptyAtBlobEnd))));

        Record maximumKey;
        maximumKey.keySize = VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, maximumKey))));
    }

    SUBCASE("blob with two records is accepted")
    {
        // Covers the per-record walk across the table, which a single-record blob cannot exercise.
        Header header;
        header.binaryCount = 2;
        const size_t tableEnd = sizeof(Header) + 2 * sizeof(Record);
        std::vector<uint8_t> blob(tableEnd + 32, 0xab);
        std::memcpy(blob.data(), &header, sizeof(header));
        for (uint32_t i = 0; i < 2; ++i)
        {
            Record record;
            record.dataSize = 16;
            record.dataOffset = (uint32_t)(tableEnd + i * 16);
            std::memcpy(blob.data() + sizeof(Header) + i * sizeof(Record), &record, sizeof(record));
        }
        CHECK(SLANG_SUCCEEDED(parse(blob)));
    }

    SUBCASE("well-formed blob is accepted")
    {
        // Guards against the rejection cases below passing for the wrong reason.
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, Record{}))));

        Record emptyKey;
        emptyKey.keySize = 0;
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, emptyKey))));

        Record noPayload;
        noPayload.dataSize = 0;
        CHECK(SLANG_SUCCEEDED(parse(build(Header{}, noPayload))));
    }

    SUBCASE("key size larger than the key array is rejected")
    {
        Record record;
        record.keySize = VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR + 1;
        CHECK(SLANG_FAILED(parse(build(Header{}, record))));

        record.keySize = 0xffffffff;
        CHECK(SLANG_FAILED(parse(build(Header{}, record))));
    }

    SUBCASE("data offset outside the payload region is rejected")
    {
        Record past;
        past.dataOffset = 0xffffffff;
        CHECK(SLANG_FAILED(parse(build(Header{}, past))));

        // Memory-safe, but binary data must not point back into the header or record table.
        Record intoTable;
        intoTable.dataOffset = 0;
        CHECK(SLANG_FAILED(parse(build(Header{}, intoTable))));
    }

    SUBCASE("data size running past the end of the blob is rejected")
    {
        Record record;
        record.dataSize = 0xffffffff;
        CHECK(SLANG_FAILED(parse(build(Header{}, record))));
    }

    SUBCASE("binary count inconsistent with the blob is rejected")
    {
        Header tooMany;
        tooMany.binaryCount = 0xffffffff;
        CHECK(SLANG_FAILED(parse(build(tooMany, Record{}))));

        Header none;
        none.binaryCount = 0;
        CHECK(SLANG_FAILED(parse(build(none, Record{}))));
    }

    SUBCASE("truncated blob is rejected")
    {
        std::vector<uint8_t> shorterThanHeader(sizeof(Header) - 1, 0);
        CHECK(SLANG_FAILED(parse(shorterThanHeader)));

        // Header claims a record that the blob does not contain.
        std::vector<uint8_t> headerOnly(sizeof(Header), 0);
        Header header;
        std::memcpy(headerOnly.data(), &header, sizeof(header));
        CHECK(SLANG_FAILED(parse(headerOnly)));
    }
}
#endif // SLANG_RHI_ENABLE_VULKAN
