#include "testing.h"
#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

// Test that multiple pipelines with deferred compilation are correctly resolved
// when dispatched in a single command encoder. This exercises the parallel
// pipeline resolution path in PipelineResolver.
GPU_TEST_CASE("parallel-pipeline-creation", ALL)
{
    const int elementCount = 4;

    // Create a buffer with initial data.
    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = elementCount * sizeof(float);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    // Load 4 separate compute programs, each with a different entry point.
    // Using loadProgram (not loadAndLinkProgram) to get unlinked programs.
    const char* entryPoints[] = {"computeAdd", "computeMul", "computeSub", "computeNeg"};
    ComPtr<IShaderProgram> programs[4];
    ComPtr<IComputePipeline> pipelines[4];

    for (int i = 0; i < 4; i++)
    {
        REQUIRE_CALL(loadProgram(device, "test-parallel-pipeline-creation", entryPoints[i], programs[i].writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = programs[i].get();
        pipelineDesc.deferTargetCompilation = true;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipelines[i].writeRef()));
    }

    // Optionally create a render pipeline (if rasterization is supported).
    ComPtr<IRenderPipeline> renderPipeline;
    ComPtr<ITexture> colorBuffer;
    ComPtr<ITextureView> colorBufferView;

    bool hasRasterization = false; // TODO: enable when D3D12 draw tests pass
    // bool hasRasterization = device->hasFeature(Feature::Rasterization);
    if (hasRasterization)
    {
        ComPtr<IShaderProgram> renderProgram;
        REQUIRE_CALL(loadProgram(
            device,
            "test-parallel-pipeline-creation",
            {"vertexMain", "fragmentMain"},
            renderProgram.writeRef()
        ));

        ColorTargetDesc colorTarget;
        colorTarget.format = Format::RGBA8Unorm;

        RenderPipelineDesc rpDesc = {};
        rpDesc.program = renderProgram.get();
        rpDesc.targets = &colorTarget;
        rpDesc.targetCount = 1;
        rpDesc.depthStencil.depthTestEnable = false;
        rpDesc.deferTargetCompilation = true;
        REQUIRE_CALL(device->createRenderPipeline(rpDesc, renderPipeline.writeRef()));

        TextureDesc colorBufferDesc = {};
        colorBufferDesc.type = TextureType::Texture2D;
        colorBufferDesc.size.width = 4;
        colorBufferDesc.size.height = 4;
        colorBufferDesc.size.depth = 1;
        colorBufferDesc.mipCount = 1;
        colorBufferDesc.format = Format::RGBA8Unorm;
        colorBufferDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        colorBufferDesc.defaultState = ResourceState::RenderTarget;
        colorBuffer = device->createTexture(colorBufferDesc, nullptr);
        REQUIRE(colorBuffer != nullptr);

        TextureViewDesc viewDesc = {};
        viewDesc.format = Format::RGBA8Unorm;
        REQUIRE_CALL(device->createTextureView(colorBuffer, viewDesc, colorBufferView.writeRef()));
    }

    // Optionally create a ray tracing pipeline (if ray tracing is supported).
    ComPtr<IRayTracingPipeline> rtPipeline;
    ComPtr<IShaderTable> shaderTable;
    ComPtr<IBuffer> rtResultBuffer;
    ComPtr<IAccelerationStructure> blasAS;
    ComPtr<IAccelerationStructure> tlasAS;

    // bool hasRayTracing = false; // device->hasFeature(Feature::RayTracing);
    bool hasRayTracing = device->hasFeature(Feature::RayTracing);
    if (hasRayTracing)
    {
        auto queue = device->getQueue(QueueType::Graphics);

        SingleTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);
        blasAS = blas.blas;
        tlasAS = tlas.tlas;

        ComPtr<IShaderProgram> rtProgram;
        REQUIRE_CALL(loadProgram(
            device,
            "test-parallel-pipeline-creation-rt",
            {"rayGenShader", "closestHitShader", "missShader"},
            rtProgram.writeRef()
        ));

        HitGroupDesc hitGroup = {};
        hitGroup.hitGroupName = "hitgroup";
        hitGroup.closestHitEntryPoint = "closestHitShader";

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rtProgram.get();
        rtpDesc.hitGroupCount = 1;
        rtpDesc.hitGroups = &hitGroup;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 1;
        rtpDesc.deferTargetCompilation = true;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, rtPipeline.writeRef()));

        const char* hitGroupName = "hitgroup";
        const char* rayGenName = "rayGenShader";
        const char* missName = "missShader";

        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rtProgram.get();
        shaderTableDesc.hitGroupCount = 1;
        shaderTableDesc.hitGroupNames = &hitGroupName;
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &rayGenName;
        shaderTableDesc.missShaderCount = 1;
        shaderTableDesc.missShaderEntryPointNames = &missName;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

        BufferDesc rtBufDesc = {};
        rtBufDesc.size = sizeof(float);
        rtBufDesc.elementSize = sizeof(float);
        rtBufDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        rtBufDesc.defaultState = ResourceState::UnorderedAccess;
        rtBufDesc.memoryType = MemoryType::DeviceLocal;
        float zero = 0.0f;
        REQUIRE_CALL(device->createBuffer(rtBufDesc, &zero, rtResultBuffer.writeRef()));
    }

    // Record all dispatches in a single command encoder.
    // This triggers resolvePipelines() with multiple work items across different pipeline types.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();

        // Compute passes.
        for (int i = 0; i < 4; i++)
        {
            auto pass = encoder->beginComputePass();
            auto rootObject = pass->bindPipeline(pipelines[i]);
            ShaderCursor(rootObject)["buffer"].setBinding(buffer);
            pass->dispatchCompute(1, 1, 1);
            pass->end();
        }

        // Render pass (if supported).
        if (hasRasterization)
        {
            RenderPassColorAttachment colorAttachment;
            colorAttachment.view = colorBufferView;
            colorAttachment.loadOp = LoadOp::Clear;
            colorAttachment.storeOp = StoreOp::Store;

            RenderPassDesc renderPass;
            renderPass.colorAttachments = &colorAttachment;
            renderPass.colorAttachmentCount = 1;

            auto pass = encoder->beginRenderPass(renderPass);
            pass->bindPipeline(renderPipeline);

            RenderState state;
            state.viewports[0] = Viewport::fromSize(4, 4);
            state.viewportCount = 1;
            state.scissorRects[0] = ScissorRect::fromSize(4, 4);
            state.scissorRectCount = 1;
            pass->setRenderState(state);

            DrawArguments args;
            args.vertexCount = 3;
            pass->draw(args);
            pass->end();
        }

        // Ray tracing pass (if supported).
        if (hasRayTracing)
        {
            auto pass = encoder->beginRayTracingPass();
            auto rootObject = pass->bindPipeline(rtPipeline, shaderTable);
            auto cursor = ShaderCursor(rootObject);
            cursor["rtResultBuffer"].setBinding(rtResultBuffer);
            cursor["sceneBVH"].setBinding(tlasAS);
            pass->dispatchRays(0, 1, 1, 1);
            pass->end();
        }

        ComPtr<ICommandBuffer> cmdBuffer;
        REQUIRE_CALL(encoder->finish(cmdBuffer.writeRef()));
        REQUIRE_CALL(queue->submit(cmdBuffer));
        REQUIRE_CALL(queue->waitOnHost());
    }

    // Verify compute results.
    // Starting with {1, 2, 3, 4}:
    //   computeAdd: +1  -> {2, 3, 4, 5}
    //   computeMul: *2  -> {4, 6, 8, 10}
    //   computeSub: -0.5 -> {3.5, 5.5, 7.5, 9.5}
    //   computeNeg: neg -> {-3.5, -5.5, -7.5, -9.5}
    compareComputeResult(device, buffer, makeArray<float>(-3.5f, -5.5f, -7.5f, -9.5f));

    // Verify ray tracing result: the ray should hit the triangle, so result should be 1.0.
    if (hasRayTracing)
    {
        compareComputeResult(device, rtResultBuffer, makeArray<float>(1.0f));
    }
}
