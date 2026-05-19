#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texture-from-native-handle", D3D12 | Vulkan | Metal)
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

    // D3D12 and Metal can check the texture descriptor from the native handle.
    // Vulkan cannot, so we skip this check.
    if (device->getDeviceType() == DeviceType::D3D12 || device->getDeviceType() == DeviceType::Metal)
    {
        TextureDesc invalidDesc = desc;
        invalidDesc.size.width++;

        ComPtr<ITexture> invalidTexture;
        CHECK(
            device->createTextureFromNativeHandle(handle, invalidDesc, invalidTexture.writeRef()) == SLANG_E_INVALID_ARG
        );
    }

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTextureFromNativeHandle(handle, desc, texture.writeRef()));

    NativeHandle wrappedHandle = {};
    REQUIRE_CALL(texture->getNativeHandle(&wrappedHandle));
    CHECK_EQ(wrappedHandle.type, handle.type);
    CHECK_EQ(wrappedHandle.value, handle.value);

    // D3D12 and Metal have internal reference counting for resources create from native handles,
    // so we can release the original texture and still use the new one.
    // Vulkan does not have internal reference counting, so we need to keep the original texture alive.
    auto queue = device->getQueue(QueueType::Graphics);
    if (device->getDeviceType() == DeviceType::D3D12 || device->getDeviceType() == DeviceType::Metal)
    {
        originalTexture = nullptr;
        REQUIRE_CALL(queue->waitOnHost());
    }

    compareComputeResult(device, texture, 0, 0, initialData);

    auto commandEncoder = queue->createCommandEncoder();

    float clearValue[4] = {8.f, 9.f, 10.f, 11.f};
    commandEncoder->clearTextureFloat(texture, kEntireTexture, clearValue);

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    compareComputeResult(device, texture, 0, 0, makeArray<float>(8.f, 9.f, 10.f, 11.f, 8.f, 9.f, 10.f, 11.f));
}
