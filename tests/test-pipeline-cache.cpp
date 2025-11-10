#include "testing.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>

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

    std::map<Key, Data> entries;
    Stats stats;

    void clear()
    {
        entries.clear();
        stats = {};
    }

    void corrupt()
    {
        for (auto& entry : entries)
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

    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key_, ISlangBlob* data_) override
    {
        stats.writeCount++;
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        Data data(
            static_cast<const uint8_t*>(data_->getBufferPointer()),
            static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize()
        );
        entries[key] = data;
        stats.entryCount = entries.size();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        stats.queryCount++;
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        auto it = entries.find(key);
        if (it == entries.end())
        {
            stats.missCount++;
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        stats.hitCount++;
        *outData = UnownedBlob::create(it->second.data(), it->second.size()).detach();
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

    VirtualCache::Stats getStats() { return pipelineCache.stats; }

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

    std::string computeShader = std::string(
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
        // Cache is cold and we expect 1 miss.
        createDevice();
        if (!device->hasFeature(Feature::PipelineCache))
            SKIP("Pipeline cache is not supported on this device type.");
        runComputePipeline(computeShader, {1.f, 2.f, 3.f, 4.f});
        CHECK_EQ(getStats().writeCount, 1);
        CHECK_EQ(getStats().queryCount, 1);
        CHECK_EQ(getStats().missCount, 1);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 1);

        // Corrupt the cache.
        if constexpr (Corrupt)
        {
            pipelineCache.corrupt();
        }

        // Cache is hot and we expect 1 hit.
        createDevice();
        runComputePipeline(computeShader, {1.f, 2.f, 3.f, 4.f});
        CHECK_EQ(getStats().writeCount, Corrupt ? 2 : 1);
        CHECK_EQ(getStats().queryCount, 2);
        CHECK_EQ(getStats().missCount, 1);
        CHECK_EQ(getStats().hitCount, 1);
        CHECK_EQ(getStats().entryCount, 1);
    }
};

template<bool Corrupt>
struct PipelineCacheTestRender : PipelineCacheTest
{
    ComPtr<IRenderPipeline> renderPipeline;
    ComPtr<ITexture> texture;

    std::string renderShader = std::string(
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

    void createRenderPipeline(std::string_view shaderSource)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadRenderProgramFromSource(device, shaderSource, "vertexMain", "fragmentMain", shaderProgram.writeRef())
        );

        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ColorTargetDesc colorTargetDesc = {};
        colorTargetDesc.format = Format::RGBA32Float;
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

    void runRenderPipeline(std::string_view shaderSource, const std::vector<float>& expectedOutput)
    {
        createResources();
        createRenderPipeline(shaderSource);
        dispatchRenderPipeline();
        CHECK(checkOutput(expectedOutput));
        freeResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 1 miss.
        createDevice();
        if (!device->hasFeature(Feature::PipelineCache))
            SKIP("Pipeline cache is not supported on this device type.");
        runRenderPipeline(renderShader, {1.f, 0.f, 1.f, 1.f});
        CHECK_EQ(getStats().writeCount, 1);
        CHECK_EQ(getStats().queryCount, 1);
        CHECK_EQ(getStats().missCount, 1);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 1);

        // Corrupt the cache.
        if constexpr (Corrupt)
        {
            pipelineCache.corrupt();
        }

        // Cache is hot and we expect 1 hit.
        createDevice();
        runRenderPipeline(renderShader, {1.f, 0.f, 1.f, 1.f});
        CHECK_EQ(getStats().writeCount, Corrupt ? 2 : 1);
        CHECK_EQ(getStats().queryCount, 2);
        CHECK_EQ(getStats().missCount, 1);
        CHECK_EQ(getStats().hitCount, 1);
        CHECK_EQ(getStats().entryCount, 1);
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

#if 0
// TODO: D3D12 does fail in debug layers and not return an error correctly.
GPU_TEST_CASE("pipeline-cache-render-corrupt", Vulkan | DontCreateDevice)
{
    runTest<PipelineCacheTestRender<true>>(ctx);
}
#endif
