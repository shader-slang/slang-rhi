#include "testing.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace gfx;
using namespace gfx::testing;

void testBufferNativeHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    Slang::ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    // TODO_GFX better way to skip test
    if (isSwiftShaderDevice(device))
        return;

    const int numberCount = 1;
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = numberCount * sizeof(float);
    bufferDesc.format = gfx::Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource);
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBufferResource> buffer;
    GFX_CHECK_CALL_ABORT(device->createBufferResource(
        bufferDesc,
        nullptr,
        buffer.writeRef()));

    InteropHandle handle;
    GFX_CHECK_CALL_ABORT(buffer->getNativeResourceHandle(&handle));
    if (deviceType == DeviceType::D3D12)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::D3D12);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Handle = (ID3D12Resource*)handle.handleValue;
        Slang::ComPtr<IUnknown> testHandle1;
        GFX_CHECK_CALL_ABORT(d3d12Handle->QueryInterface<IUnknown>(testHandle1.writeRef()));
        Slang::ComPtr<ID3D12Resource> testHandle2;
        GFX_CHECK_CALL_ABORT(testHandle1->QueryInterface<ID3D12Resource>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Handle, testHandle2.get());
#endif
    }
    else if (deviceType == DeviceType::Vulkan)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::Vulkan);
        CHECK_NE(handle.handleValue, 0);
    }
}

void testTextureNativeHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    Slang::ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    // TODO_GFX better way to skip test
    if (isSwiftShaderDevice(device))
        return;

    ITextureResource::Desc desc = {};
    desc.type = IResource::Type::Texture2D;
    desc.numMipLevels = 1;
    desc.size.width = 1;
    desc.size.height = 1;
    desc.size.depth = 1;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.format = Format::R16G16B16A16_FLOAT;

    Slang::ComPtr<ITextureResource> buffer;
    buffer = device->createTextureResource(desc);

    InteropHandle handle;
    GFX_CHECK_CALL_ABORT(buffer->getNativeResourceHandle(&handle));
    if (deviceType == DeviceType::D3D12)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::D3D12);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Handle = (ID3D12Resource*)handle.handleValue;
        Slang::ComPtr<IUnknown> testHandle1;
        GFX_CHECK_CALL_ABORT(d3d12Handle->QueryInterface<IUnknown>(testHandle1.writeRef()));
        Slang::ComPtr<ID3D12Resource> testHandle2;
        GFX_CHECK_CALL_ABORT(testHandle1->QueryInterface<ID3D12Resource>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Handle, testHandle2.get());
#endif
    }
    else if (deviceType == DeviceType::Vulkan)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::Vulkan);
        CHECK_NE(handle.handleValue, 0);
    }
}

void testCommandQueueNativeHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    Slang::ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    // TODO_GFX better way to skip test
    if (isSwiftShaderDevice(device))
        return;

    ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
    auto queue = device->createCommandQueue(queueDesc);
    InteropHandle handle;
    GFX_CHECK_CALL_ABORT(queue->getNativeHandle(&handle));
    if (deviceType == DeviceType::D3D12)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::D3D12);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Queue = (ID3D12CommandQueue*)handle.handleValue;
        Slang::ComPtr<IUnknown> testHandle1;
        GFX_CHECK_CALL_ABORT(d3d12Queue->QueryInterface<IUnknown>(testHandle1.writeRef()));
        Slang::ComPtr<ID3D12CommandQueue> testHandle2;
        GFX_CHECK_CALL_ABORT(testHandle1->QueryInterface<ID3D12CommandQueue>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Queue, testHandle2.get());
#endif
    }
    else if (deviceType == DeviceType::Vulkan)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::Vulkan);
        CHECK_NE(handle.handleValue, 0);
    }
}

void testCommandBufferNativeHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    Slang::ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    // TODO_GFX better way to skip test
    if (isSwiftShaderDevice(device))
        return;

    // We need to create a transient heap in order to create a command buffer.
    Slang::ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    GFX_CHECK_CALL_ABORT(
        device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    auto commandBuffer = transientHeap->createCommandBuffer();
    struct CloseComandBufferRAII
    {
        ICommandBuffer* m_commandBuffer;
        ~CloseComandBufferRAII()
        {
            m_commandBuffer->close();
        }
    } closeCommandBufferRAII{ commandBuffer };
    InteropHandle handle = {};
    GFX_CHECK_CALL_ABORT(commandBuffer->getNativeHandle(&handle));
    if (deviceType == DeviceType::D3D12)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::D3D12);
#if SLANG_WINDOWS_FAMILY
        auto d3d12Handle = (ID3D12GraphicsCommandList*)handle.handleValue;
        Slang::ComPtr<IUnknown> testHandle1;
        GFX_CHECK_CALL_ABORT(d3d12Handle->QueryInterface<IUnknown>(testHandle1.writeRef()));
        Slang::ComPtr<ID3D12GraphicsCommandList> testHandle2;
        GFX_CHECK_CALL_ABORT(d3d12Handle->QueryInterface<ID3D12GraphicsCommandList>(testHandle2.writeRef()));
        CHECK_EQ(d3d12Handle, testHandle2.get());
#endif
    }
    else if (deviceType == DeviceType::Vulkan)
    {
        CHECK_EQ(handle.api, InteropHandleAPI::Vulkan);
        CHECK_NE(handle.handleValue, 0);
    }
}

TEST_CASE("BufferNativeHandle")
{
    runGpuTests(testBufferNativeHandle, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("TextureNativeHandle")
{
    runGpuTests(testTextureNativeHandle, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("CommandQueueNativeHandle")
{
    runGpuTests(testCommandQueueNativeHandle, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("CommandBufferNativeHandle")
{
    runGpuTests(testCommandBufferNativeHandle, {DeviceType::D3D12, DeviceType::Vulkan});
}
