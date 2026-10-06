#include "testing.h"
#include "barrier.h"
#include "slang-rhi-config.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-device.h"
#include "vulkan/vk-buffer.h"
#include "vulkan/vk-acceleration-structure.h"
#endif

#include <thread>
#include <utility>

using namespace rhi;
using namespace rhi::testing;

namespace {

// Race the first lookup, then exercise cached lookups while other threads may still
// be publishing. Check results after joining so test failures cannot unwind a worker.
template<typename GetValue, typename CheckValue>
void checkConcurrentValues(IDevice* device, GetValue getValue, CheckValue checkValue)
{
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t lookupCount = 32;
    using Value = decltype(getValue(uint32_t(0)));
    std::array<std::array<Value, lookupCount>, threadCount> values{};
    Barrier start(threadCount);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                DeviceScope scope(device);
                start.arriveAndWait();
                for (auto& value : values[i])
                    value = getValue(i);
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    for (const auto& threadValues : values)
        for (const auto& value : threadValues)
            checkValue(value, values[0][0]);
}

} // namespace

GPU_TEST_CASE("resource-thread-safety-texture-shared-handle", D3D12)
{
    for (uint32_t round = 0; round < 8; ++round)
    {
        TextureDesc desc = {};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::ShaderResource | TextureUsage::Shared;
        auto texture = device->createTexture(desc);
        REQUIRE(texture);

        checkConcurrentValues(
            device,
            [&](uint32_t)
            {
                NativeHandle handle;
                Result result = texture->getSharedHandle(&handle);
                return std::make_pair(result, handle);
            },
            [](const auto& value, const auto& expected)
            {
                CHECK(SLANG_SUCCEEDED(value.first));
                CHECK(value.second.type == NativeHandleType::Win32);
                CHECK_NE(value.second.value, 0);
                CHECK_EQ(value.second.value, expected.second.value);
            }
        );
    }
}

#if SLANG_RHI_ENABLE_VULKAN
namespace {

std::atomic<uint32_t> addressQueryCount{0};

VKAPI_ATTR VkDeviceAddress VKAPI_CALL queryBufferAddress(VkDevice, const VkBufferDeviceAddressInfo*)
{
    ++addressQueryCount;
    std::this_thread::yield();
    return 0x1000;
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
queryAccelerationStructureAddress(VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR*)
{
    ++addressQueryCount;
    std::this_thread::yield();
    return 0x2000;
}

VKAPI_ATTR void VKAPI_CALL
destroyAccelerationStructure(VkDevice, VkAccelerationStructureKHR, const VkAllocationCallbacks*)
{
}

} // namespace

// Stub only the driver queries so the real cache code can run under TSAN without
// requiring GPU address or ray-tracing support. Stack objects own no native storage.
TEST_CASE("resource-thread-safety-buffer-device-address")
{
    vk::DeviceImpl device;
    device.m_api.vkGetBufferDeviceAddress = queryBufferAddress;

    for (uint32_t round = 0; round < 8; ++round)
    {
        addressQueryCount = 0;
        BufferDesc desc = {};
        desc.size = 256;
        desc.usage = BufferUsage::ShaderResource;
        vk::BufferImpl buffer(&device, desc);

        checkConcurrentValues(
            &device,
            [&](uint32_t)
            {
                return buffer.getDeviceAddress();
            },
            [](DeviceAddress value, DeviceAddress expected)
            {
                CHECK_EQ(value, 0x1000);
                CHECK_EQ(value, expected);
            }
        );
        CHECK_EQ(addressQueryCount.load(), 1);
    }
}

TEST_CASE("resource-thread-safety-acceleration-structure-device-address")
{
    vk::DeviceImpl device;
    device.m_api.vkGetAccelerationStructureDeviceAddressKHR = queryAccelerationStructureAddress;
    device.m_api.vkDestroyAccelerationStructureKHR = destroyAccelerationStructure;

    for (uint32_t round = 0; round < 8; ++round)
    {
        addressQueryCount = 0;
        AccelerationStructureDesc desc = {};
        desc.kind = AccelerationStructureKind::BottomLevel;
        desc.size = 1024;
        vk::AccelerationStructureImpl accelerationStructure(&device, desc);

        checkConcurrentValues(
            &device,
            [&](uint32_t i)
            {
                return i % 2 ? accelerationStructure.getHandle().value : accelerationStructure.getDeviceAddress();
            },
            [](DeviceAddress value, DeviceAddress expected)
            {
                CHECK_EQ(value, 0x2000);
                CHECK_EQ(value, expected);
            }
        );
        CHECK_EQ(addressQueryCount.load(), 1);
    }
}
#endif

GPU_TEST_CASE("resource-thread-safety-texture-descriptor-handle", CUDA)
{
    if (!device->hasFeature(Feature::Bindless))
        SKIP("Bindless is not supported");

    for (uint32_t round = 0; round < 8; ++round)
    {
        TextureDesc desc = {};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
        auto texture = device->createTexture(desc);
        REQUIRE(texture);
        auto view = texture->createView({});
        REQUIRE(view);

        // Read and combined handles share the CUDA texture-object cache; writable
        // handles use the separate surface-object cache.
        checkConcurrentValues(
            device,
            [&](uint32_t i)
            {
                std::array<DescriptorHandle, 2> handles{};
                Result readResult = i % 2 ? view->getCombinedTextureSamplerDescriptorHandle(&handles[0])
                                          : view->getDescriptorHandle(DescriptorHandleAccess::Read, &handles[0]);
                Result writeResult = view->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &handles[1]);
                return std::make_pair(SLANG_FAILED(readResult) ? readResult : writeResult, handles);
            },
            [](const auto& value, const auto& expected)
            {
                CHECK(SLANG_SUCCEEDED(value.first));
                CHECK(
                    (value.second[0].type == DescriptorHandleType::Texture ||
                     value.second[0].type == DescriptorHandleType::CombinedTextureSampler)
                );
                CHECK(value.second[1].type == DescriptorHandleType::RWTexture);
                for (uint32_t i = 0; i < 2; ++i)
                {
                    CHECK_NE(value.second[i].value, 0);
                    CHECK_EQ(value.second[i].value, expected.second[i].value);
                }
            }
        );
    }
}
