#include "testing.h"
#include "barrier.h"
#include "slang-rhi-config.h"
#include "rhi-shared.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-device.h"
#include "vulkan/vk-buffer.h"
#include "vulkan/vk-acceleration-structure.h"
#include "vulkan/vk-bindless-descriptor-set.h"
#include "vulkan/vk-texture.h"
#include "vulkan/vk-sampler.h"
#endif

#include <thread>
#include <set>
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

TEST_CASE_TEMPLATE("resource-thread-safety-published-handle", Handle, NativeHandle, DescriptorHandle)
{
    using Type = decltype(Handle{}.type);
    // Both handle enums have a valid type at 1. A zero value is also a valid
    // descriptor slot, so validity must depend on the type rather than the value.
    const Type type = Type(1);
    for (uint64_t value : {uint64_t(0), uint64_t(0x123456789abcdef0)})
    {
        for (uint32_t round = 0; round < 16; ++round)
        {
            PublishedHandle<Handle> published;
            Handle empty{type, 42};
            CHECK_FALSE(published.tryGet(&empty));
            CHECK(empty.type == Type::Undefined);
            CHECK_EQ(empty.value, 0);

            Barrier start(8);
            std::atomic<uint32_t> errors{0};
            std::vector<std::thread> threads;
            for (uint32_t i = 0; i < 8; ++i)
            {
                threads.emplace_back(
                    [&, i]
                    {
                        start.arriveAndWait();
                        if (i == 0)
                            published.publish(type, value);
                        for (uint32_t j = 0; j < 256; ++j)
                        {
                            Handle handle;
                            if (published.tryGet(&handle))
                            {
                                if (handle.type != type || handle.value != value)
                                    ++errors;
                            }
                            else if (handle.type != Type::Undefined || handle.value != 0)
                                ++errors;
                        }
                    }
                );
            }
            for (auto& thread : threads)
                thread.join();
            CHECK_EQ(errors.load(), 0);
            CHECK(published.get().type == type);
            CHECK_EQ(published.get().value, value);
        }
    }
}

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

// Exercise the real view caches and descriptor allocator without a GPU. Driver
// handles are opaque tokens; the host allocation/publication code is unchanged.
std::atomic<uintptr_t> nextView{1};
std::atomic<uint32_t> descriptorWrites{0};

template<typename Handle>
VKAPI_ATTR void VKAPI_CALL ignoreDestroy(VkDevice, Handle, const VkAllocationCallbacks*)
{
}

VKAPI_ATTR VkResult VKAPI_CALL
createBufferView(VkDevice, const VkBufferViewCreateInfo*, const VkAllocationCallbacks*, VkBufferView* view)
{
    *view = (VkBufferView)nextView.fetch_add(1);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
createImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* view)
{
    *view = (VkImageView)nextView.fetch_add(1);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
updateDescriptors(VkDevice, uint32_t count, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*)
{
    descriptorWrites += count;
}

} // namespace

TEST_CASE("resource-thread-safety-bindless-allocation-and-reuse")
{
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t rounds = 16;
    vk::DeviceImpl device;
    auto& api = device.m_api;
    api.vkCreateBufferView = createBufferView;
    api.vkCreateImageView = createImageView;
    api.vkDestroyBufferView = ignoreDestroy<VkBufferView>;
    api.vkDestroyImageView = ignoreDestroy<VkImageView>;
    api.vkDestroyBuffer = ignoreDestroy<VkBuffer>;
    api.vkFreeMemory = ignoreDestroy<VkDeviceMemory>;
    api.vkDestroySampler = ignoreDestroy<VkSampler>;
    api.vkUpdateDescriptorSets = updateDescriptors;

    vk::BindlessDescriptorSet descriptors(&device, {});
    descriptors.m_bufferAllocator.capacity = threadCount;
    descriptors.m_textureAllocator.capacity = threadCount;
    descriptors.m_samplerAllocator.capacity = threadCount;
    descriptors.m_firstTextureHandle = threadCount;
    descriptors.m_firstAccelerationStructureHandle = 2 * threadCount;

    using Handles = std::array<DescriptorHandle, 3>;
    std::array<std::array<Handles, threadCount>, rounds> snapshots{};
    Barrier phase(threadCount);
    std::atomic<uint32_t> errors{0};
    descriptorWrites = 0;
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                BufferDesc bufferDesc = {};
                bufferDesc.size = 256;
                vk::BufferImpl buffer(&device, bufferDesc);
                buffer.m_buffer.m_api = &api;
                buffer.m_buffer.m_buffer = VK_NULL_HANDLE;
                buffer.m_buffer.m_memory = VK_NULL_HANDLE;
                TextureDesc textureDesc = {};
                textureDesc.format = Format::R32Float;
                vk::TextureImpl texture(&device, textureDesc);
                texture.m_shouldDestroyImage = false;
                texture.m_vkformat = VK_FORMAT_R32_SFLOAT;
                TextureViewDesc viewDesc = {};
                viewDesc.format = Format::R32Float;
                viewDesc.subresourceRange = {0, 1, 0, 1};
                vk::TextureViewImpl view(&texture, viewDesc);
                vk::SamplerImpl sampler(&device, {});
                sampler.m_sampler = VK_NULL_HANDLE;

                auto allocate = [&](uint32_t kind, DescriptorHandle* out)
                {
                    switch (kind)
                    {
                    case 0:
                        return descriptors
                            .allocBufferHandle(&buffer, DescriptorHandleAccess::Read, Format::R32Float, {0, 256}, out);
                    case 1:
                        return descriptors.allocTextureHandle(&view, DescriptorHandleAccess::Read, out);
                    default:
                        return descriptors.allocSamplerHandle(&sampler, out);
                    }
                };
                auto checkResult = [&](Result result, Result expected)
                {
                    if (result != expected)
                        ++errors;
                };

                for (uint32_t round = 0; round < rounds; ++round)
                {
                    phase.arriveAndWait();
                    // Invalid access must not reserve a slot or publish a handle.
                    DescriptorHandle invalid;
                    checkResult(
                        descriptors.allocBufferHandle(
                            &buffer,
                            DescriptorHandleAccess(99),
                            Format::Undefined,
                            {0, 256},
                            &invalid
                        ),
                        SLANG_E_INVALID_ARG
                    );
                    checkResult(
                        descriptors.allocTextureHandle(&view, DescriptorHandleAccess(99), &invalid),
                        SLANG_E_INVALID_ARG
                    );
                    if (invalid)
                        ++errors;

                    for (uint32_t j = 0; j < 3; ++j)
                    {
                        uint32_t kind = (i + j) % 3;
                        checkResult(allocate(kind, &snapshots[round][i][kind]), SLANG_OK);
                    }
                    phase.arriveAndWait();
                    // All slots are live. Failure must leave the allocators usable.
                    for (uint32_t kind = 0; kind < 3; ++kind)
                    {
                        DescriptorHandle extra;
                        checkResult(allocate(kind, &extra), SLANG_E_OUT_OF_MEMORY);
                        if (extra)
                            ++errors;
                    }
                    phase.arriveAndWait();
                    for (const auto& handle : snapshots[round][i])
                        checkResult(descriptors.freeHandle(handle), SLANG_OK);
                    phase.arriveAndWait();
                    // Interleave allocation and free across threads and resource types.
                    for (uint32_t j = 0; j < 32; ++j)
                    {
                        DescriptorHandle handle;
                        Result result = allocate((i + j) % 3, &handle);
                        checkResult(result, SLANG_OK);
                        if (SLANG_SUCCEEDED(result))
                            checkResult(descriptors.freeHandle(handle), SLANG_OK);
                    }
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();

    CHECK_EQ(errors.load(), 0);
    CHECK_EQ(descriptorWrites.load(), rounds * threadCount * (3 + 32));
    for (const auto& snapshot : snapshots)
    {
        std::set<uint64_t> resources;
        std::set<uint64_t> samplers;
        for (const auto& handles : snapshot)
        {
            CHECK(handles[0].type == DescriptorHandleType::Buffer);
            CHECK(handles[1].type == DescriptorHandleType::Texture);
            CHECK(handles[2].type == DescriptorHandleType::Sampler);
            CHECK(resources.insert(handles[0].value).second);
            CHECK(resources.insert(handles[1].value).second);
            CHECK(samplers.insert(handles[2].value).second);
        }
    }
    CHECK_EQ(descriptors.m_bufferAllocator.freeSlots.size(), threadCount);
    CHECK_EQ(descriptors.m_textureAllocator.freeSlots.size(), threadCount);
    CHECK_EQ(descriptors.m_samplerAllocator.freeSlots.size(), threadCount);
}

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

GPU_TEST_CASE("resource-thread-safety-bindless-mixed-resources", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::Bindless))
        SKIP("Bindless is not supported");

    constexpr uint32_t threadCount = 8;
    struct Resources
    {
        ComPtr<IBuffer> buffer;
        ComPtr<ITexture> texture;
        ComPtr<ITextureView> view;
        ComPtr<ISampler> sampler;
        std::array<DescriptorHandle, 3> handles;
    };
    std::array<Resources, threadCount> resources;
    // Create resources serially: this test only promises concurrent descriptor access.
    for (auto& r : resources)
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 256;
        bufferDesc.format = Format::R32Float;
        bufferDesc.usage = BufferUsage::ShaderResource;
        r.buffer = device->createBuffer(bufferDesc);
        REQUIRE(r.buffer);
        TextureDesc textureDesc = {};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::ShaderResource;
        r.texture = device->createTexture(textureDesc);
        REQUIRE(r.texture);
        r.view = r.texture->createView({});
        REQUIRE(r.view);
        r.sampler = device->createSampler({});
        REQUIRE(r.sampler);
    }

    Barrier start(threadCount);
    std::atomic<uint32_t> errors{0};
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                DeviceScope scope(device);
                auto& r = resources[i];
                start.arriveAndWait();
                for (uint32_t round = 0; round < 32; ++round)
                {
                    for (uint32_t j = 0; j < 3; ++j)
                    {
                        uint32_t kind = (i + j) % 3;
                        DescriptorHandle handle;
                        Result result;
                        if (kind == 0)
                            result = r.buffer->getDescriptorHandle(
                                DescriptorHandleAccess::Read,
                                Format::R32Float,
                                kEntireBuffer,
                                &handle
                            );
                        else if (kind == 1)
                            result = r.view->getDescriptorHandle(DescriptorHandleAccess::Read, &handle);
                        else
                            result = r.sampler->getDescriptorHandle(&handle);
                        if (SLANG_FAILED(result) || !handle)
                            ++errors;
                        if (round == 0)
                            r.handles[kind] = handle;
                        else if (r.handles[kind].type != handle.type || r.handles[kind].value != handle.value)
                            ++errors;
                    }
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(errors.load(), 0);
    std::set<uint64_t> resourceHandles;
    std::set<uint64_t> samplerHandles;
    for (const auto& r : resources)
    {
        CHECK(r.handles[0].type == DescriptorHandleType::Buffer);
        CHECK(r.handles[1].type == DescriptorHandleType::Texture);
        CHECK(r.handles[2].type == DescriptorHandleType::Sampler);
        CHECK(resourceHandles.insert(r.handles[0].value).second);
        CHECK(resourceHandles.insert(r.handles[1].value).second);
        CHECK(samplerHandles.insert(r.handles[2].value).second);
    }
}
