#include "testing.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("native-handle-buffer", D3D12 | Vulkan | Metal | CUDA)
{
    const int numberCount = 1;
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    NativeHandle handle;
    REQUIRE_CALL(buffer->getNativeHandle(&handle));
    switch (device->getDeviceType())
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
    case DeviceType::CUDA:
    {
        CHECK_EQ(handle.type, NativeHandleType::CUdeviceptr);
        CHECK_NE(handle.value, 0);
        break;
    }
    default:
        break;
    }
}

GPU_TEST_CASE("native-handle-texture", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.mipCount = 1;
    desc.size.width = 1;
    desc.size.height = 1;
    desc.size.depth = 1;
    desc.usage = TextureUsage::UnorderedAccess;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.format = Format::RGBA16Float;

    ComPtr<ITexture> texture;
    texture = device->createTexture(desc);

    NativeHandle handle;
    REQUIRE_CALL(texture->getNativeHandle(&handle));
    switch (device->getDeviceType())
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
    case DeviceType::CUDA:
    {
        CHECK_EQ(handle.type, NativeHandleType::CUarray);
        CHECK_NE(handle.value, 0);
        break;
    }
    default:
        break;
    }
}

GPU_TEST_CASE("texture-from-native-handle", D3D12 | Metal)
{
    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.mipCount = 1;
    desc.size.width = 2;
    desc.size.height = 1;
    desc.size.depth = 1;
    desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource | TextureUsage::CopyDestination;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.format = Format::RGBA32Float;

    auto initialData = makeArray<float>(0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f);

    SubresourceData subresourceData = {};
    subresourceData.data = initialData.data();
    subresourceData.rowPitch = sizeof(float) * 4 * desc.size.width;
    subresourceData.slicePitch = subresourceData.rowPitch * desc.size.height;

    ComPtr<ITexture> originalTexture;
    REQUIRE_CALL(device->createTexture(desc, &subresourceData, originalTexture.writeRef()));

    NativeHandle handle = {};
    REQUIRE_CALL(originalTexture->getNativeHandle(&handle));

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTextureFromNativeHandle(handle, desc, texture.writeRef()));

    NativeHandle wrappedHandle = {};
    REQUIRE_CALL(texture->getNativeHandle(&wrappedHandle));
    CHECK_EQ(wrappedHandle.type, handle.type);
    CHECK_EQ(wrappedHandle.value, handle.value);
    compareComputeResult(device, texture, 0, 0, initialData);

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();

    float clearValue[4] = {8.f, 9.f, 10.f, 11.f};
    commandEncoder->clearTextureFloat(texture, kEntireTexture, clearValue);

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    compareComputeResult(device, texture, 0, 0, makeArray<float>(8.f, 9.f, 10.f, 11.f, 8.f, 9.f, 10.f, 11.f));
}

GPU_TEST_CASE("native-handle-command-queue", D3D12 | Vulkan | Metal | CUDA)
{
    auto queue = device->getQueue(QueueType::Graphics);
    NativeHandle handle;
    REQUIRE_CALL(queue->getNativeHandle(&handle));
    switch (device->getDeviceType())
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
    case DeviceType::CUDA:
    {
        CHECK_EQ(handle.type, NativeHandleType::CUstream);
        // CHECK_NE(handle.value, 0); (Null is valid - it's the default stream)
        break;
    }
    default:
        break;
    }
}

GPU_TEST_CASE("native-handle-command-buffer", D3D12 | Vulkan | Metal)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto commandBuffer = commandEncoder->finish();
    NativeHandle handle;
    REQUIRE_CALL(commandBuffer->getNativeHandle(&handle));
    switch (device->getDeviceType())
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
    default:
        break;
    }
}
