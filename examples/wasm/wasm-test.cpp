// Minimal WebGPU device test for slang-rhi
// This test validates RHI can be initialized in WASM

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace rhi;

int main()
{
    // Keep the test caller and its lambda covered by the preset's *main* pattern.
    auto testBindings = [](IDevice* device, bool graphics) -> Result
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
            auto pipeline = device->createComputePipeline(pipelineDesc);
            if (!pipeline)
                return SLANG_FAIL;
            auto pass = encoder->beginComputePass();
            SLANG_RETURN_ON_FAIL(ShaderCursor(pass->bindPipeline(pipeline))["result"].setBinding(buffer));
            pass->dispatchCompute(1, 1, 1);
            pass->end();
        }
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(queue->submit(commands));
#ifdef __EMSCRIPTEN__
        if (graphics && EM_ASM_INT({ return Module['rhiBindingYielded'] ? 1 : 0; }))
        {
            printf("FAILED: binding recording yielded before canvas submission\n");
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
        printf("Creating WebGPU device...\n");
        IDevice* device = nullptr;
        DeviceDesc desc = {};
        desc.deviceType = DeviceType::WGPU;

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

            // Exercise graphics and compute resource bindings.
            for (int stage = 0; stage < 2; ++stage)
            {
                const char* name = stage ? "graphics" : "compute";
                printf("Testing %s bindings...\n", name);
                if (SLANG_FAILED(testBindings(device, stage != 0)))
                {
                    printf("FAILED: %s bindings\n", name);
                    device->release();
                    return 1;
                }
                printf("OK: %s bindings and output verified\n", name);
            }
            device->release();
        }
        else
        {
            printf("FAILED: Failed to create WebGPU device (Result: %x)\n", res);
        }
    }

    printf("\n=== RHI initialization test finished ===\n");
    return 0;
}
