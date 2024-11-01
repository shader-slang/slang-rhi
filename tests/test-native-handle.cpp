#include "testing.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

void testNativeHandleBuffer(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    if (isSwiftShaderDevice(device))
        SKIP("not supported with swiftshader");

    const int numberCount = 1;
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    NativeHandle handle;
    REQUIRE_CALL(buffer->getNativeHandle(&handle));
    switch (deviceType)
    {
    case DeviceType::D3D12:
    {
        CHECK_EQ(handle.type, NativeHandleType::D3D12Resource);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Resource = (ID3D12Resource*)handle.value;
        ComPtr<IUnknown> testHandle1;
        REQUIRE_CALL(d3d12Resource->QueryInterface<IUnknown>(testHandle1.writeRef()));
        ComPtr<ID3D12Resource> testHandle2;
        REQUIRE_CALL(testHandle1->QueryInterface<ID3D12Resource>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Resource, testHandle2.get());
#endif
        break;
    }
    case DeviceType::Vulkan:
    {
        CHECK_EQ(handle.type, NativeHandleType::VkBuffer);
        CHECK_NE(handle.value, 0);
        break;
    }
    case DeviceType::Metal:
    {
        CHECK_EQ(handle.type, NativeHandleType::MTLBuffer);
        CHECK_NE(handle.value, 0);
        break;
    }
    }
}

void testNativeHandleTexture(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    if (isSwiftShaderDevice(device))
        SKIP("not supported with swiftshader");

    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.mipLevelCount = 1;
    desc.size.width = 1;
    desc.size.height = 1;
    desc.size.depth = 1;
    desc.usage = TextureUsage::UnorderedAccess;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.format = Format::R16G16B16A16_FLOAT;

    ComPtr<ITexture> texture;
    texture = device->createTexture(desc);

    NativeHandle handle;
    REQUIRE_CALL(texture->getNativeHandle(&handle));
    switch (deviceType)
    {
    case DeviceType::D3D12:
    {
        CHECK_EQ(handle.type, NativeHandleType::D3D12Resource);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Resource = (ID3D12Resource*)handle.value;
        ComPtr<IUnknown> testHandle1;
        REQUIRE_CALL(d3d12Resource->QueryInterface<IUnknown>(testHandle1.writeRef()));
        ComPtr<ID3D12Resource> testHandle2;
        REQUIRE_CALL(testHandle1->QueryInterface<ID3D12Resource>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Resource, testHandle2.get());
#endif
        break;
    }
    case DeviceType::Vulkan:
    {
        CHECK_EQ(handle.type, NativeHandleType::VkImage);
        CHECK_NE(handle.value, 0);
        break;
    }
    case DeviceType::Metal:
    {
        CHECK_EQ(handle.type, NativeHandleType::MTLTexture);
        CHECK_NE(handle.value, 0);
        break;
    }
    }
}

void testNativeHandleCommandQueue(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    if (isSwiftShaderDevice(device))
        SKIP("not supported with swiftshader");

    auto queue = device->getQueue(QueueType::Graphics);
    NativeHandle handle;
    REQUIRE_CALL(queue->getNativeHandle(&handle));
    switch (deviceType)
    {
    case DeviceType::D3D12:
    {
        CHECK_EQ(handle.type, NativeHandleType::D3D12CommandQueue);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Queue = (ID3D12CommandQueue*)handle.value;
        ComPtr<IUnknown> testHandle1;
        REQUIRE_CALL(d3d12Queue->QueryInterface<IUnknown>(testHandle1.writeRef()));
        ComPtr<ID3D12CommandQueue> testHandle2;
        REQUIRE_CALL(testHandle1->QueryInterface<ID3D12CommandQueue>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Queue, testHandle2.get());
#endif
        break;
    }
    case DeviceType::Vulkan:
    {
        CHECK_EQ(handle.type, NativeHandleType::VkQueue);
        CHECK_NE(handle.value, 0);
        break;
    }
    case DeviceType::Metal:
    {
        CHECK_EQ(handle.type, NativeHandleType::MTLCommandQueue);
        CHECK_NE(handle.value, 0);
        break;
    }
    }
}

void testNativeHandleCommandBuffer(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    if (isSwiftShaderDevice(device))
        SKIP("not supported with swiftshader");

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto commandBuffer = commandEncoder->finish();
    NativeHandle handle;
    REQUIRE_CALL(commandBuffer->getNativeHandle(&handle));
    switch (deviceType)
    {
    case DeviceType::D3D12:
    {
        CHECK_EQ(handle.type, NativeHandleType::D3D12GraphicsCommandList);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Handle = (ID3D12GraphicsCommandList*)handle.value;
        ComPtr<IUnknown> testHandle1;
        REQUIRE_CALL(d3d12Handle->QueryInterface<IUnknown>(testHandle1.writeRef()));
        ComPtr<ID3D12GraphicsCommandList> testHandle2;
        REQUIRE_CALL(d3d12Handle->QueryInterface<ID3D12GraphicsCommandList>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Handle, testHandle2.get());
#endif
        break;
    }
    case DeviceType::Vulkan:
    {
        CHECK_EQ(handle.type, NativeHandleType::VkCommandBuffer);
        CHECK_NE(handle.value, 0);
        break;
    }
    case DeviceType::Metal:
    {
        CHECK_EQ(handle.type, NativeHandleType::MTLCommandBuffer);
        CHECK_NE(handle.value, 0);
        break;
    }
    }
}

TEST_CASE("native-handle-buffer")
{
    runGpuTests(
        testNativeHandleBuffer,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}

TEST_CASE("native-handle-texture")
{
    runGpuTests(
        testNativeHandleTexture,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}

TEST_CASE("native-handle-command-queue")
{
    runGpuTests(
        testNativeHandleCommandQueue,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}

TEST_CASE("native-handle-command-buffer")
{
    runGpuTests(
        testNativeHandleCommandBuffer,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}
