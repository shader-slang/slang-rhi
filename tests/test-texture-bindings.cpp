#include "testing.h"
#include "shader-cache.h"
#include "device.h"
#include "shader.h"

#include <atomic>
#include <future>

using namespace rhi;
using namespace rhi::testing;

namespace {

ComPtr<IComputePipeline> createPipeline(IDevice* device, const char* entryPoint)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-texture-bindings", entryPoint, program.writeRef()));
    ComputePipelineDesc desc = {};
    desc.program = program;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(desc, pipeline.writeRef()));
    return pipeline;
}

ComPtr<IBuffer> createResults(IDevice* device, size_t count)
{
    BufferDesc desc = {};
    desc.size = count * sizeof(float);
    desc.elementSize = 4 * sizeof(float);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto buffer = device->createBuffer(desc);
    REQUIRE(buffer);
    return buffer;
}

ComPtr<ITexture> createColorInput(IDevice* device, const uint8_t (&color)[4])
{
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
    SubresourceData data = {color, sizeof(color), sizeof(color)};
    auto texture = device->createTexture(desc, &data);
    REQUIRE(texture);
    return texture;
}

void checkResults(IDevice* device, IBuffer* buffer, std::span<const float> expected)
{
    ComPtr<ISlangBlob> data;
    REQUIRE_CALL(device->readBuffer(buffer, 0, expected.size_bytes(), data.writeRef()));
    const float* values = static_cast<const float*>(data->getBufferPointer());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        CAPTURE(i);
        CHECK(values[i] == expected[i]);
    }
}

} // namespace

GPU_TEST_CASE("texture-depth-comparison-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    auto pipeline = createPipeline(device, "compareDepth");
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::D32Float;
    desc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
    auto depth = device->createTexture(desc);
    REQUIRE(depth);
    auto depthView = depth->getDefaultView();
    REQUIRE(depthView);
    SamplerDesc samplerDesc = {};
    samplerDesc.reductionOp = TextureReductionOp::Comparison;
    samplerDesc.comparisonFunc = ComparisonFunc::Less;
    auto sampler = device->createSampler(samplerDesc);
    REQUIRE(sampler);
    auto results = createResults(device, 4);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    RenderPassDepthStencilAttachment attachment = {};
    attachment.view = depthView;
    attachment.depthClearValue = 0.5f;
    RenderPassDesc renderPassDesc = {};
    renderPassDesc.depthStencilAttachment = &attachment;
    auto renderPass = encoder->beginRenderPass(renderPassDesc);
    renderPass->end();

    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["depthInput"].setBinding(depth));
    REQUIRE_CALL(cursor["comparisonSampler"].setBinding(sampler));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {1.f, 0.f, 0.f, 0.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-storage-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    auto writePipeline = createPipeline(device, "writeTextures");
    auto readPipeline = createPipeline(device, "readTextures");
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::RGBA16Float;
    desc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource;
    auto hdr = device->createTexture(desc);
    REQUIRE(hdr);
    desc.type = TextureType::Texture3D;
    desc.size.depth = 2;
    desc.format = Format::RGBA8Unorm;
    auto volume = device->createTexture(desc);
    REQUIRE(volume);
    desc.type = TextureType::Texture2D;
    desc.size.depth = 1;
    desc.format = Format::R32Uint;
    desc.usage |= TextureUsage::CopyDestination;
    uint32_t initialCounter = 7;
    SubresourceData initialData = {&initialCounter, sizeof(initialCounter), sizeof(initialCounter)};
    auto counter = device->createTexture(desc, &initialData);
    REQUIRE(counter);
    auto results = createResults(device, 12);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor write(pass->bindPipeline(writePipeline));
    REQUIRE_CALL(write["storage"]["hdr"].setBinding(hdr));
    REQUIRE_CALL(write["storage"]["volume"].setBinding(volume));
    REQUIRE_CALL(write["storage"]["counter"].setBinding(counter));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    pass = encoder->beginComputePass();
    ShaderCursor read(pass->bindPipeline(readPipeline));
    REQUIRE_CALL(read["hdrInput"].setBinding(hdr));
    REQUIRE_CALL(read["volumeInput"].setBinding(volume));
    REQUIRE_CALL(read["counterInput"].setBinding(counter));
    REQUIRE_CALL(read["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {0.25f, 0.5f, 2.f, 1.f, 0.f, 1.f, 0.f, 1.f, 10.f, 0.f, 0.f, 0.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-integer-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    auto pipeline = createPipeline(device, "readIntegers");
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
    uint8_t unsignedValues[] = {1, 2, 3, 4};
    SubresourceData initialData = {unsignedValues, sizeof(unsignedValues), sizeof(unsignedValues)};
    auto unsignedTexture = device->createTexture(desc, &initialData);
    REQUIRE(unsignedTexture);
    desc.format = Format::RGBA8Sint;
    int8_t signedValues[] = {-1, -2, 3, -4};
    initialData.data = signedValues;
    auto signedTexture = device->createTexture(desc, &initialData);
    REQUIRE(signedTexture);
    auto results = createResults(device, 8);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["uintInput"].setBinding(unsignedTexture));
    REQUIRE_CALL(cursor["intInput"].setBinding(signedTexture));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {1.f, 2.f, 3.f, 4.f, -1.f, -2.f, 3.f, -4.f};
    checkResults(device, results, expected);
}

static void runPartialParameterBlockBindings(IDevice* device)
{
    auto pipeline = createPipeline(device, "readPartialBlock");
    auto texture = createColorInput(device, {0, 255, 0, 255});
    auto results = createResults(device, 4);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    // The other fields, including the block's ordinary data, are unused.
    REQUIRE_CALL(cursor["partial"]["usedTexture"].setBinding(texture));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {0.f, 1.f, 0.f, 1.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-partial-parameter-block-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    runPartialParameterBlockBindings(device);
}

static void runSpecializedBindings(IDevice* device)
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-texture-bindings", "readSpecialized", program.writeRef(), &reflection)
    );
    ComputePipelineDesc desc = {};
    desc.program = program;
    auto pipeline = device->createComputePipeline(desc);
    REQUIRE(pipeline);
    auto textureA = createColorInput(device, {255, 0, 0, 255});
    auto textureB = createColorInput(device, {0, 255, 0, 255});
    auto results = createResults(device, 12);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    for (uint32_t i = 0; i < 3; ++i)
    {
        // A, B, A also checks that cached specializations retain distinct layouts.
        bool useA = i != 1;
        auto type = reflection->findTypeByName(useA ? "TextureReaderA" : "TextureReaderB");
        REQUIRE(type);
        ComPtr<IShaderObject> reader;
        REQUIRE_CALL(device->createShaderObject(type, ShaderObjectContainerType::None, reader.writeRef()));
        auto pass = encoder->beginComputePass();
        auto root = pass->bindPipeline(pipeline);
        ShaderCursor cursor(root);
        REQUIRE_CALL(
            cursor[useA ? "specializationInputA" : "specializationInputB"].setBinding(useA ? textureA : textureB)
        );
        REQUIRE_CALL(cursor["results"].setBinding(results));
        ShaderCursor entry(root->getEntryPoint(0));
        REQUIRE_CALL(entry["reader"].setObject(reader));
        REQUIRE_CALL(entry["outputIndex"].setData(i));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
    }
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-specialized-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    runSpecializedBindings(device);
}

static void runGraphicsStageBindings(IDevice* device, bool compileOnWorkers = false, ITexture* invalidTexture = nullptr)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(
        loadProgram(device, "test-texture-bindings", {"bindingVertex", "bindingFragment"}, program.writeRef())
    );
    if (compileOnWorkers)
    {
        // Exercise the resolver's prepare/compile/install sequence directly:
        // WebGPU draw recording currently compiles shaders before resolution.
        auto impl = checked_cast<ShaderProgram*>(program.get());
        auto deviceImpl = getUnderlyingDevice(device);
        std::lock_guard<std::mutex> lock(impl->m_compileMutex);
        std::vector<CompiledEntryPoint> entries;
        REQUIRE_CALL(impl->prepareEntryPointCompilation(deviceImpl, entries));
        REQUIRE(entries.size() == 2);
        std::vector<std::future<Result>> futures;
        for (auto& entry : entries)
        {
            futures.push_back(
                std::async(
                    std::launch::async,
                    [impl, deviceImpl, entry = &entry]()
                    {
                        return impl->compileEntryPoint(deviceImpl, *entry, false);
                    }
                )
            );
        }
        for (size_t i = 0; i < entries.size(); ++i)
        {
            entries[i].result = futures[i].get();
            impl->reportEntryPointCompilation(deviceImpl, entries[i]);
            REQUIRE_CALL(entries[i].result);
        }
        REQUIRE_CALL(impl->installCompiledEntryPoints(entries));
    }
    ColorTargetDesc target = {};
    target.format = Format::RGBA8Unorm;
    RenderPipelineDesc desc = {};
    desc.program = program;
    desc.targetCount = 1;
    desc.targets = &target;
    auto pipeline = device->createRenderPipeline(desc);
    REQUIRE(pipeline);

    const float positions[] = {-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(positions);
    bufferDesc.elementSize = 4 * sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource;
    auto vertices = device->createBuffer(bufferDesc, positions);
    REQUIRE(vertices);
    auto texture = createColorInput(device, {0, 255, 0, 255});
    auto results = createResults(device, 4);
    TextureDesc textureDesc = {};
    textureDesc.size = {1, 1, 1};
    textureDesc.format = target.format;
    textureDesc.usage = TextureUsage::RenderTarget;
    auto colorTarget = device->createTexture(textureDesc);
    REQUIRE(colorTarget);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    RenderPassColorAttachment attachment = {};
    attachment.view = colorTarget->getDefaultView();
    RenderPassDesc passDesc = {};
    passDesc.colorAttachments = &attachment;
    passDesc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(passDesc);
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["vertexPositions"].setBinding(vertices));
    REQUIRE_CALL(cursor["fragmentInput"].setBinding(texture));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(1, 1);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(1, 1);
    state.scissorRectCount = 1;
    pass->setRenderState(state);
    DrawArguments draw = {};
    draw.vertexCount = 3;
    pass->draw(draw);
    if (invalidTexture)
    {
        // A failed state update must not let this draw reuse the previous bindings.
        REQUIRE_CALL(cursor["fragmentInput"].setBinding(invalidTexture));
        pass->draw(draw);
        // Repairing the binding must not clear the encoder's earlier failure.
        REQUIRE_CALL(cursor["fragmentInput"].setBinding(texture));
        pass->draw(draw);
        pass->end();
        ComPtr<ICommandBuffer> commandBuffer;
        CHECK(SLANG_FAILED(encoder->finish(commandBuffer.writeRef())));
        CHECK(!commandBuffer);
        return;
    }
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {0.f, 1.f, 0.f, 1.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-graphics-stage-bindings", D3D12 | Vulkan | Metal | WGPU)
{
    runGraphicsStageBindings(device);
}

GPU_TEST_CASE("texture-binding-error-scopes", WGPU | DontCreateDevice)
{
    struct DebugCallback : IDebugCallback
    {
        std::vector<std::string> errors;

        void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char* message) override
        {
            if (type == DebugMessageType::Error)
                errors.emplace_back(message);
        }
    } callback;
    DeviceExtraOptions options;
    options.debugCallback = &callback;
    // Keep the callback alive until this device and all its resources are released.
    auto testDevice = createTestingDevice(ctx, DeviceType::WGPU, false, &options);
    REQUIRE(testDevice);
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(testDevice, "test-texture-bindings", "readPartialBlock", program.writeRef()));
    ComputePipelineDesc desc = {};
    desc.program = program;
    desc.compilationPolicy = PipelineCompilationPolicy::Immediate;
    auto pipeline = testDevice->createComputePipeline(desc);
    REQUIRE(pipeline);
    auto texture = createColorInput(testDevice, {0, 255, 0, 255});
    auto results = createResults(testDevice, 4);
    auto queue = testDevice->getQueue(QueueType::Graphics);
    CHECK(callback.errors.empty());

    {
        TextureDesc invalidDesc = {};
        invalidDesc.size = {1, 1, 1};
        invalidDesc.format = Format::RGBA8Uint;
        invalidDesc.usage = TextureUsage::ShaderResource;
        auto invalidTexture = testDevice->createTexture(invalidDesc);
        REQUIRE(invalidTexture);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["partial"]["usedTexture"].setBinding(texture));
        REQUIRE_CALL(cursor["results"].setBinding(results));
        pass->dispatchCompute(1, 1, 1);
        // An integer view cannot satisfy the shader's float texture binding.
        // Start with valid state so the failing dispatch could otherwise reuse it.
        REQUIRE_CALL(cursor["partial"]["usedTexture"].setBinding(invalidTexture));
        pass->dispatchCompute(1, 1, 1);
        REQUIRE_CALL(cursor["partial"]["usedTexture"].setBinding(texture));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE(!callback.errors.empty());
        CHECK(callback.errors.front().find("resource creation") != std::string::npos);
        ComPtr<ICommandBuffer> commandBuffer;
        CHECK(SLANG_FAILED(encoder->finish(commandBuffer.writeRef())));
        CHECK(!commandBuffer);

        runGraphicsStageBindings(testDevice, false, invalidTexture);
    }

    // A separate native operation leaves an unrelated uncaptured validation error.
    // It must not make the next valid bind group fail.
    size_t errorCount = callback.errors.size();
    SamplerDesc invalidSampler = {};
    invalidSampler.minLOD = 2.f;
    invalidSampler.maxLOD = 1.f;
    auto sampler = testDevice->createSampler(invalidSampler);
    REQUIRE(callback.errors.size() > errorCount);
    errorCount = callback.errors.size();
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["partial"]["usedTexture"].setBinding(texture));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    CHECK(callback.errors.size() == errorCount);
    const float expected[] = {0.f, 1.f, 0.f, 1.f};
    checkResults(testDevice, results, expected);
}

GPU_TEST_CASE("texture-binding-shader-cache", WGPU | DontCreateDevice)
{
    struct ArtifactCache : rhi::testing::ShaderCache
    {
        enum class ReadMode
        {
            Normal,
            CodeOnly,
            Truncated,
            UnsupportedVersion,
        };
        ReadMode readMode = ReadMode::Normal;
        std::atomic<uint32_t> hits{0};
        std::atomic<uint32_t> writes{0};

        Result SLANG_MCALL queryCache(ISlangBlob* key, ISlangBlob** outData) override
        {
            ComPtr<ISlangBlob> data;
            Result result = ShaderCache::queryCache(key, data.writeRef());
            if (SLANG_FAILED(result))
                return result;
            ++hits;
            if (readMode == ReadMode::CodeOnly)
            {
                const char wgsl[] = "@compute @workgroup_size(1) fn main() {}";
                data = OwnedBlob::create(wgsl, sizeof(wgsl) - 1);
            }
            else if (readMode == ReadMode::Truncated)
            {
                data = OwnedBlob::create(data->getBufferPointer(), data->getBufferSize() - 1);
            }
            else if (readMode == ReadMode::UnsupportedVersion)
            {
                auto bytes = static_cast<const uint8_t*>(data->getBufferPointer());
                std::vector<uint8_t> modified(bytes, bytes + data->getBufferSize());
                modified[4] = 0xff;
                data = OwnedBlob::create(modified.data(), modified.size());
            }
            *outData = data.detach();
            return SLANG_OK;
        }

        Result SLANG_MCALL writeCache(ISlangBlob* key, ISlangBlob* data) override
        {
            ++writes;
            return ShaderCache::writeCache(key, data);
        }
    } cache;

    auto run = [&](PipelineCompilationMode mode, bool expectCached)
    {
        DeviceExtraOptions options;
        options.persistentShaderCache = &cache;
        options.enableCompilationReports = true;
        options.pipelineCompilationMode = mode;
        // Each run must use a fresh Slang session, not its in-memory compiled artifacts.
        auto testDevice = createTestingDevice(ctx, DeviceType::WGPU, false, &options);
        REQUIRE(testDevice);
        auto session = testDevice->getSlangSession()->getGlobalSession();
        double totalBefore, downstreamBefore, totalAfter, downstreamAfter;
        session->getCompilerElapsedTime(&totalBefore, &downstreamBefore);
        uint32_t writesBefore = cache.writes;

        runPartialParameterBlockBindings(testDevice);
        runSpecializedBindings(testDevice);
        runGraphicsStageBindings(testDevice, mode == PipelineCompilationMode::Parallel);

        session->getCompilerElapsedTime(&totalAfter, &downstreamAfter);
        if (expectCached)
        {
            // Cache-hit reports alone would miss codegen caused by a late metadata query.
            CHECK(totalAfter == totalBefore);
            CHECK(downstreamAfter == downstreamBefore);
            CHECK(cache.writes == writesBefore);
        }
        else
        {
            CHECK(totalAfter > totalBefore);
            CHECK(cache.writes == writesBefore + 5);
        }

        ComPtr<ISlangBlob> reportsBlob;
        REQUIRE_CALL(testDevice->getCompilationReportList(reportsBlob.writeRef()));
        const auto* reports = static_cast<const CompilationReportList*>(reportsBlob->getBufferPointer());
        uint32_t entryPointCount = 0;
        for (uint32_t i = 0; i < reports->reportCount; ++i)
        {
            const auto& report = reports->reports[i];
            for (uint32_t j = 0; j < report.entryPointReportCount; ++j)
            {
                CHECK(report.entryPointReports[j].isCached == expectCached);
                ++entryPointCount;
            }
        }
        CHECK(entryPointCount == 5);
    };

    run(PipelineCompilationMode::Serial, false);
    CHECK(cache.hits == 0);
    run(PipelineCompilationMode::Serial, true);
    CHECK(cache.hits == 5);
    run(PipelineCompilationMode::Parallel, true);
    CHECK(cache.hits == 10);

    for (auto readMode :
         {ArtifactCache::ReadMode::CodeOnly,
          ArtifactCache::ReadMode::Truncated,
          ArtifactCache::ReadMode::UnsupportedVersion})
    {
        CAPTURE(int(readMode));
        cache.readMode = readMode;
        // Invalid artifacts are recompiled and reported as misses, then usable again.
        run(PipelineCompilationMode::Parallel, false);
        cache.readMode = ArtifactCache::ReadMode::Normal;
        run(PipelineCompilationMode::Parallel, true);
    }
}
