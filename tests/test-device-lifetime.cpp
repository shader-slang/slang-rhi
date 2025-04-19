#include "testing.h"
#include <slang-rhi.h>
#include "core/smart-pointer.h"
#include "rhi-shared.h"
#include "device.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("device-lifetime", ALL)
{
    // Create a device
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = device->getDeviceType();
    ComPtr<IDevice> testDevice;
    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, testDevice.writeRef()));

    Device* devicePtr = static_cast<Device*>(testDevice.get());

    // Create a buffer
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024;
    bufferDesc.usage = BufferUsage::ShaderResource;
    REQUIRE_CALL(testDevice->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    uint64_t deviceRefCountBuffer = devicePtr->debugGetReferenceCount();

    // Create a texture
    ComPtr<ITexture> texture;
    TextureDesc textureDesc = {};
    textureDesc.format = Format::RGBA32Float;
    textureDesc.usage = TextureUsage::ShaderResource;
    REQUIRE_CALL(testDevice->createTexture(textureDesc, nullptr, texture.writeRef()));
    uint64_t deviceRefCountTexture = devicePtr->debugGetReferenceCount();

    // Create a sampler
    ComPtr<ISampler> sampler;
    SamplerDesc samplerDesc = {};
    REQUIRE_CALL(testDevice->createSampler(samplerDesc, sampler.writeRef()));
    uint64_t deviceRefCountSampler = devicePtr->debugGetReferenceCount();

    // Create acceleration structure
    ComPtr<IAccelerationStructure> accelerationStructure;
    if (testDevice->hasFeature(Feature::AccelerationStructure))
    {
        AccelerationStructureDesc accelerationStructureDesc = {};
        accelerationStructureDesc.size = 1024;
        REQUIRE_CALL(
            testDevice->createAccelerationStructure(accelerationStructureDesc, accelerationStructure.writeRef())
        );
    }
    uint64_t deviceRefCountAccelerationStructure = devicePtr->debugGetReferenceCount();

    // Create fence
    ComPtr<IFence> fence;
    if (testDevice->getDeviceType() != DeviceType::D3D11)
    {
        FenceDesc fenceDesc = {};
        REQUIRE_CALL(testDevice->createFence(fenceDesc, fence.writeRef()));
    }
    uint64_t deviceRefCountFence = devicePtr->debugGetReferenceCount();

    testDevice.setNull();

    CHECK(devicePtr->debugGetReferenceCount() == deviceRefCountFence - 1);
    fence.setNull();

    CHECK(devicePtr->debugGetReferenceCount() == deviceRefCountAccelerationStructure - 1);
    accelerationStructure.setNull();

    CHECK(devicePtr->debugGetReferenceCount() == deviceRefCountSampler - 1);
    sampler.setNull();

    CHECK(devicePtr->debugGetReferenceCount() == deviceRefCountTexture - 1);
    texture.setNull();

    CHECK(devicePtr->debugGetReferenceCount() == deviceRefCountBuffer - 1);
    buffer.setNull();
}
