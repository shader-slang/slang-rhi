// WebGPU browser smoke test for initialization, bindings, and asynchronous wait paths.

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <atomic>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace rhi;

int main()
{
    struct DebugCallback : IDebugCallback
    {
        std::atomic<bool> failed{false};

        void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char* message) override
        {
            if (type == DebugMessageType::Error)
            {
                failed = true;
                printf("FAILED: %s\n", message);
            }
        }
    } callback;
    // Keep test callers and their lambdas covered by the preset's *main* pattern.
    auto testBindings = [](IDevice* device, bool graphics, bool deferred) -> Result
    {
        const char* source = R"(
            RWStructuredBuffer<uint> result;
            [shader("compute")]
            [numthreads(1, 1, 1)]
            void computeMain() { result[0] = 42; }
            [shader("vertex")]
            float4 vertexMain(uint vertexID : SV_VertexID) : SV_Position
            {
                float2 positions[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
                return float4(positions[vertexID], 0, 1);
            }
            [shader("fragment")]
            float4 fragmentMain() : SV_Target
            {
                result[0] = 43;
                return float4(0, 1, 0, 1);
            }
        )";
        auto session = device->getSlangSession();
        ComPtr<slang::IBlob> diagnostics;
        auto module = session->loadModuleFromSourceString("bindings", "bindings.slang", source, diagnostics.writeRef());
        if (!module)
        {
            if (diagnostics)
                printf("%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
            return SLANG_FAIL;
        }
        ComPtr<slang::IEntryPoint> entryPoints[2];
        SLANG_RETURN_ON_FAIL(
            module->findEntryPointByName(graphics ? "vertexMain" : "computeMain", entryPoints[0].writeRef())
        );
        if (graphics)
            SLANG_RETURN_ON_FAIL(module->findEntryPointByName("fragmentMain", entryPoints[1].writeRef()));
        slang::IComponentType* components[] = {entryPoints[0], entryPoints[1]};
        ShaderProgramDesc programDesc = {};
        programDesc.slangGlobalScope = module;
        programDesc.slangEntryPoints = components;
        programDesc.slangEntryPointCount = graphics ? 2 : 1;
        programDesc.linkingStyle = LinkingStyle::SingleProgram;
        ComPtr<IShaderProgram> program;
        SLANG_RETURN_ON_FAIL(device->createShaderProgram(programDesc, program.writeRef()));

        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(uint32_t);
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        auto buffer = device->createBuffer(bufferDesc);
        if (!buffer)
            return SLANG_FAIL;
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        ComPtr<ISurface> surface;
        if (graphics)
        {
            ColorTargetDesc target = {};
#ifdef __EMSCRIPTEN__
            if (!EM_ASM_INT({ return document.querySelector('#canvas') ? 1 : 0; }))
            {
                printf("FAILED: canvas element is missing\n");
                return SLANG_FAIL;
            }
            EM_ASM({
                const canvas = document.querySelector('#canvas');
                canvas.width = 1;
                canvas.height = 1;
            });
            surface = device->createSurface(WindowHandle::fromWGPUCanvas("#canvas"));
            if (!surface)
                return SLANG_FAIL;
            SurfaceConfig config = {};
            config.width = config.height = 1;
            config.usage = TextureUsage::RenderTarget;
            SLANG_RETURN_ON_FAIL(surface->configure(config));
            target.format = surface->getConfig()->format;
#else
            target.format = Format::RGBA8Unorm;
#endif
            RenderPipelineDesc pipelineDesc = {};
            pipelineDesc.program = program;
            pipelineDesc.targetCount = 1;
            pipelineDesc.targets = &target;
            auto pipeline = device->createRenderPipeline(pipelineDesc);
            if (!pipeline)
                return SLANG_FAIL;
            ComPtr<ITexture> texture;
#ifdef __EMSCRIPTEN__
            // Even successful binding creation must not yield after canvas acquisition.
            EM_ASM({
                Module['rhiBindingYielded'] = false;
                setTimeout(function() { Module['rhiBindingYielded'] = true; }, 0);
            });
            SLANG_RETURN_ON_FAIL(surface->acquireNextImage(texture.writeRef()));
#else
            TextureDesc textureDesc = {};
            textureDesc.size = {1, 1, 1};
            textureDesc.format = target.format;
            textureDesc.usage = TextureUsage::RenderTarget;
            texture = device->createTexture(textureDesc);
#endif
            if (!texture)
                return SLANG_FAIL;
            RenderPassColorAttachment attachment = {};
            attachment.view = texture->getDefaultView();
            RenderPassDesc passDesc = {};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &attachment;
            auto pass = encoder->beginRenderPass(passDesc);
            SLANG_RETURN_ON_FAIL(ShaderCursor(pass->bindPipeline(pipeline))["result"].setBinding(buffer));
            RenderState state = {};
            state.viewports[0] = Viewport::fromSize(1, 1);
            state.viewportCount = 1;
            state.scissorRects[0] = ScissorRect::fromSize(1, 1);
            state.scissorRectCount = 1;
            pass->setRenderState(state);
            DrawArguments draw = {};
            draw.vertexCount = 3;
            pass->draw(draw);
            pass->draw(draw);
            pass->end();
        }
        else
        {
            ComputePipelineDesc pipelineDesc = {};
            pipelineDesc.program = program;
            // A deferred pipeline is created while finish() resolves pipelines.
            if (deferred)
                pipelineDesc.compilationPolicy = PipelineCompilationPolicy::Deferred;
            auto pipeline = device->createComputePipeline(pipelineDesc);
            if (!pipeline)
                return SLANG_FAIL;
            auto pass = encoder->beginComputePass();
            SLANG_RETURN_ON_FAIL(ShaderCursor(pass->bindPipeline(pipeline))["result"].setBinding(buffer));
            pass->dispatchCompute(1, 1, 1);
            pass->end();
#ifdef __EMSCRIPTEN__
            // Resolving deferred pipelines in finish() must not yield either.
            if (deferred)
                EM_ASM({
                    Module['rhiBindingYielded'] = false;
                    setTimeout(function() { Module['rhiBindingYielded'] = true; }, 0);
                });
#endif
        }
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(queue->submit(commands));
#ifdef __EMSCRIPTEN__
        if ((graphics || deferred) && EM_ASM_INT({ return Module['rhiBindingYielded'] ? 1 : 0; }))
        {
            printf("FAILED: %s yielded before submission\n", graphics ? "binding recording" : "pipeline resolution");
            return SLANG_FAIL;
        }
#endif
        if (surface)
            SLANG_RETURN_ON_FAIL(surface->present());
        SLANG_RETURN_ON_FAIL(queue->waitOnHost());
        ComPtr<ISlangBlob> data;
        SLANG_RETURN_ON_FAIL(device->readBuffer(buffer, 0, sizeof(uint32_t), data.writeRef()));
        return *static_cast<const uint32_t*>(data->getBufferPointer()) == (graphics ? 43u : 42u) ? SLANG_OK
                                                                                                 : SLANG_FAIL;
    };

    auto testUniformBindings = [](IDevice* device) -> Result
    {
        const char* source = R"(
            struct Values { uint4 value; };
            struct Scalar { uint value; };
            struct Parameters
            {
                ParameterBlock<Values> inner;
                ParameterBlock<Scalar> scalar;
            };
            uniform uint globalValue;
            ConstantBuffer<Values> constants;
            ParameterBlock<Parameters> parameters;
            [format("rgba8")] WTexture2D<float4> output;
            void writeOutput(uint value)
            {
                output.Store(uint2(0), float4(float(value) / 255.0, 0.0, 1.0, 1.0));
            }
            [shader("compute")][numthreads(1, 1, 1)]
            void globalMain() { writeOutput(globalValue); }
            [shader("compute")][numthreads(1, 1, 1)]
            void constantMain() { writeOutput(constants.value.x); }
            [shader("compute")][numthreads(1, 1, 1)]
            void nestedMain() { writeOutput(parameters.inner.value.x); }
            [shader("compute")][numthreads(1, 1, 1)]
            void entryMain(uniform uint entryValue) { writeOutput(entryValue); }
            [shader("compute")][numthreads(1, 1, 1)]
            void scalarMain() { writeOutput(parameters.scalar.value); }
        )";
        auto session = device->getSlangSession();
        ComPtr<slang::IBlob> diagnostics;
        auto module = session->loadModuleFromSourceString("uniforms", "uniforms.slang", source, diagnostics.writeRef());
        if (!module)
        {
            if (diagnostics)
                printf("%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
            return SLANG_FAIL;
        }
        const char* entryNames[] = {"globalMain", "constantMain", "nestedMain", "entryMain", "scalarMain"};
        for (uint32_t i = 0; i < 5; ++i)
        {
            printf("Testing uniform bindings: %s...\n", entryNames[i]);
            ComPtr<slang::IEntryPoint> entryPoint;
            SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryNames[i], entryPoint.writeRef()));
            slang::IComponentType* entryPoints[] = {entryPoint};
            ShaderProgramDesc programDesc = {};
            programDesc.slangGlobalScope = module;
            programDesc.slangEntryPoints = entryPoints;
            programDesc.slangEntryPointCount = 1;
            ComPtr<IShaderProgram> program;
            SLANG_RETURN_ON_FAIL(device->createShaderProgram(programDesc, program.writeRef()));
            ComputePipelineDesc pipelineDesc = {};
            pipelineDesc.program = program;
            ComPtr<IComputePipeline> pipeline;
            SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

            TextureDesc textureDesc = {};
            textureDesc.size = {1, 1, 1};
            textureDesc.format = Format::RGBA8Unorm;
            textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
            ComPtr<ITexture> texture;
            SLANG_RETURN_ON_FAIL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
            auto queue = device->getQueue(QueueType::Graphics);
            // Each encoder starts with fresh constant-buffer pages, so each binding
            // path must survive mapping rather than reuse a previously mapped page.
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginComputePass();
            auto root = pass->bindPipeline(pipeline);
            ShaderCursor cursor(root);
            SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(texture));
            uint32_t value = 40 + i;
            uint32_t blockValue[] = {value, 0, 0, 0};
            switch (i)
            {
            case 0:
                SLANG_RETURN_ON_FAIL(cursor["globalValue"].setData(value));
                break;
            case 1:
                SLANG_RETURN_ON_FAIL(cursor["constants"]["value"].setData(blockValue));
                break;
            case 2:
                SLANG_RETURN_ON_FAIL(cursor["parameters"]["inner"]["value"].setData(blockValue));
                break;
            case 3:
                SLANG_RETURN_ON_FAIL(ShaderCursor(root->getEntryPoint(0))["entryValue"].setData(value));
                break;
            case 4:
                SLANG_RETURN_ON_FAIL(cursor["parameters"]["scalar"]["value"].setData(value));
                break;
            }
            pass->dispatchCompute(1, 1, 1);
            pass->end();
            ComPtr<ICommandBuffer> commands;
            SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
            SLANG_RETURN_ON_FAIL(queue->submit(commands));

            ComPtr<ISlangBlob> data;
            SLANG_RETURN_ON_FAIL(device->readTexture(texture, 0, 0, data.writeRef(), nullptr));
            auto texel = static_cast<const uint8_t*>(data->getBufferPointer());
            if (texel[0] != value || texel[1] != 0 || texel[2] != 255 || texel[3] != 255)
                return SLANG_FAIL;
            printf("OK: %s uniform bindings and texture readback verified\n", entryNames[i]);
        }
        return SLANG_OK;
    };

    printf("=== slang-rhi WASM Test (Local Build Verification) ===\n");
    printf("Creating RHI instance...\n");

    IRHI* rhi = getRHI();
    if (!rhi)
    {
        printf("FAILED: Could not get RHI instance\n");
        return 1;
    }
    printf("OK: RHI instance created\n");

    // Check if WebGPU device type is supported
    printf("Checking WebGPU support...\n");
    bool wgpuSupported = rhi->isDeviceTypeSupported(DeviceType::WGPU);
    printf("WebGPU device type supported: %s\n", wgpuSupported ? "yes" : "no");

    if (wgpuSupported)
    {
        // Enumerate before device creation so the uncached call_once path is exercised.
        printf("Testing WebGPU adapter enumeration...\n");
        auto adapter = rhi->getAdapter(DeviceType::WGPU, 0);
        if (!adapter || rhi->getAdapter(DeviceType::WGPU, 0) != adapter)
        {
            printf("FAILED: WebGPU adapter enumeration\n");
            return 1;
        }
        printf("OK: WebGPU adapter enumeration verified\n");
        printf("Creating WebGPU device...\n");
        IDevice* device = nullptr;
        DeviceDesc desc = {};
        desc.deviceType = DeviceType::WGPU;
        desc.debugCallback = &callback;

        Result res = rhi->createDevice(desc, &device);

        if (SLANG_SUCCEEDED(res) && device)
        {
            printf("OK: WebGPU device created successfully!\n");

            // Create a queue
            printf("Testing Queue creation...\n");
            ICommandQueue* queue = nullptr;
            device->getQueue(QueueType::Graphics, &queue);

            if (queue)
            {
                printf("OK: Queue created\n");
                queue->release();
            }

            // Test buffer validation logic
            printf("Testing Buffer creation validation...\n");
            IBuffer* invalidBuffer = nullptr;
            NativeHandle invalidHandle = {};
            invalidHandle.type = NativeHandleType::WGPUBuffer;
            invalidHandle.value = 0;
            Result invalidRes = device->createBufferFromNativeHandle(invalidHandle, {}, &invalidBuffer);
            if (invalidRes == SLANG_E_INVALID_HANDLE && invalidBuffer == nullptr)
            {
                printf("OK: Invalid handle validation passed\n");
            }
            else
            {
                printf(
                    "FAILED: Invalid handle validation failed (Result: %x, Buffer: %p)\n",
                    invalidRes,
                    invalidBuffer
                );
            }

            // Test buffer creation and mapping (triggers wgpu::wait)
            printf("Testing Buffer creation and mapping...\n");
            IBuffer* buffer = nullptr;
            BufferDesc bufferDesc = {};
            bufferDesc.size = 1024;
            bufferDesc.usage = BufferUsage::ShaderResource;
            bufferDesc.memoryType = MemoryType::ReadBack;

            uint32_t initData[256];
            for (int i = 0; i < 256; ++i)
                initData[i] = i;

            res = device->createBuffer(bufferDesc, initData, &buffer);
            if (SLANG_SUCCEEDED(res) && buffer)
            {
                printf("OK: Buffer created and initialized\n");

                void* mappedData = nullptr;
                res = device->mapBuffer(buffer, CpuAccessMode::Read, &mappedData);
                if (SLANG_SUCCEEDED(res) && mappedData)
                {
                    printf("OK: Buffer mapped successfully\n");
                    uint32_t* data = (uint32_t*)mappedData;
                    if (data[128] == 128)
                    {
                        printf("OK: Buffer data integrity verified\n");
                    }
                    else
                    {
                        printf("FAILED: Buffer data integrity check failed (data[128] = %u)\n", data[128]);
                    }
                    device->unmapBuffer(buffer);
                }
                else
                {
                    printf("FAILED: Buffer mapping failed (Result: %x)\n", res);
                }
                buffer->release();
            }
            else
            {
                printf("FAILED: Buffer creation failed (Result: %x)\n", res);
            }

            // Exercise graphics, compute and deferred compute resource bindings.
            for (int stage = 0; stage < 3; ++stage)
            {
                const char* names[] = {"compute", "graphics", "deferred compute"};
                const char* name = names[stage];
                printf("Testing %s bindings...\n", name);
                if (SLANG_FAILED(testBindings(device, stage == 1, stage == 2)))
                {
                    printf("FAILED: %s bindings\n", name);
                    device->release();
                    return 1;
                }
                printf("OK: %s bindings and output verified\n", name);
            }
            res = testUniformBindings(device);
            if (SLANG_FAILED(res))
            {
                printf("FAILED: uniform bindings or texture readback (Result: %x)\n", res);
                device->release();
                return 1;
            }
            device->release();
        }
        else
        {
            printf("FAILED: Failed to create WebGPU device (Result: %x)\n", res);
        }
    }

    printf("\n=== RHI initialization test finished ===\n");
    return callback.failed ? 1 : 0;
}
