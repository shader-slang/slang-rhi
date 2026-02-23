// Minimal WebGPU device test for slang-rhi
// This test validates RHI can be initialized in WASM

#include <slang-rhi.h>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace rhi;

int main()
{
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
                printf("FAILED: Invalid handle validation failed (Result: %x, Buffer: %p)\n", invalidRes, invalidBuffer);
            }

            // Test buffer creation and mapping (triggers wgpu::wait)
            printf("Testing Buffer creation and mapping...\n");
            IBuffer* buffer = nullptr;
            BufferDesc bufferDesc = {};
            bufferDesc.size = 1024;
            bufferDesc.usage = BufferUsage::ShaderResource;
            bufferDesc.memoryType = MemoryType::ReadBack;
            
            uint32_t initData[256];
            for (int i = 0; i < 256; ++i) initData[i] = i;

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
