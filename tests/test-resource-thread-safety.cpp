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
#include "vulkan/vk-command.h"
#endif

#include <thread>
#include <set>
#include <utility>
#include <cstring>

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

// Keep all assertions on the joining thread. Worker errors must not leave other
// workers waiting at a barrier or destroy a joinable std::thread.
GPU_TEST_CASE("resource-thread-safety-create-with-submission", ALL)
{
    constexpr uint32_t threadCount = 4;
    constexpr uint32_t rounds = 12;
    constexpr uint32_t wordCount = 64;
    struct Resources
    {
        ComPtr<IBuffer> buffer;
        ComPtr<ITexture> texture;
        uint32_t pattern;
    };
    std::array<std::array<Resources, rounds>, threadCount> resources;
    std::array<Result, threadCount> results{};
    std::atomic<uint32_t> workersRemaining{threadCount};
    BufferDesc bufferDesc = {};
    bufferDesc.size = wordCount * sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    TextureDesc textureDesc = {};
    textureDesc.size = {8, 8, 1};
    textureDesc.format = Format::R32Uint;
    textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource | TextureUsage::CopyDestination;

    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);
    std::array<uint32_t, wordCount> renderData;
    renderData.fill(0x12345678);
    auto src = device->createBuffer(bufferDesc, renderData.data());
    auto dst = device->createBuffer(bufferDesc);
    REQUIRE(src);
    REQUIRE(dst);

    Barrier start(threadCount + 1);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                DeviceScope scope(device);
                start.arriveAndWait();
                results[i] = [&]() -> Result
                {
                    for (uint32_t round = 0; round < rounds; ++round)
                    {
                        auto& r = resources[i][round];
                        r.pattern = 1 + i * rounds + round;
                        std::array<uint32_t, wordCount> data;
                        data.fill(r.pattern);
                        SLANG_RETURN_ON_FAIL(device->createBuffer(bufferDesc, data.data(), r.buffer.writeRef()));
                        SubresourceData initData = {data.data(), 8 * sizeof(uint32_t), sizeof(data)};
                        SLANG_RETURN_ON_FAIL(device->createTexture(textureDesc, &initData, r.texture.writeRef()));
                        ComPtr<ISampler> sampler;
                        SLANG_RETURN_ON_FAIL(device->createSampler({}, sampler.writeRef()));
                        ComPtr<ITextureView> view;
                        SLANG_RETURN_ON_FAIL(r.texture->createView({}, view.writeRef()));

                        // Exercise allocation/free overlap as well as the initialized path.
                        ComPtr<IBuffer> scratchBuffer;
                        ComPtr<ITexture> scratchTexture;
                        BufferDesc scratchDesc = bufferDesc;
                        scratchDesc.memoryType = round % 3 == 0   ? MemoryType::DeviceLocal
                                                 : round % 3 == 1 ? MemoryType::Upload
                                                                  : MemoryType::ReadBack;
                        SLANG_RETURN_ON_FAIL(device->createBuffer(
                            scratchDesc,
                            scratchDesc.memoryType == MemoryType::Upload ? data.data() : nullptr,
                            scratchBuffer.writeRef()
                        ));
                        SLANG_RETURN_ON_FAIL(device->createTexture(textureDesc, nullptr, scratchTexture.writeRef()));
                    }
                    return SLANG_OK;
                }();
                --workersRemaining;
            }
        );
    }
    start.arriveAndWait();
    Result renderResult = [&]() -> Result
    {
        for (uint32_t round = 0; round < rounds || workersRemaining.load() != 0; ++round)
        {
            ComPtr<ICommandEncoder> encoder;
            SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
            encoder->copyBuffer(dst, 0, src, 0, bufferDesc.size);
            ComPtr<ICommandBuffer> commandBuffer;
            SLANG_RETURN_ON_FAIL(encoder->finish(commandBuffer.writeRef()));
            SLANG_RETURN_ON_FAIL(queue->submit(commandBuffer));
            SLANG_RETURN_ON_FAIL(queue->waitOnHost());
        }
        return SLANG_OK;
    }();
    for (auto& thread : threads)
        thread.join();
    REQUIRE_CALL(renderResult);
    for (Result result : results)
        REQUIRE_CALL(result);
    REQUIRE_CALL(queue->waitOnHost());

    ComPtr<ISlangBlob> blob;
    REQUIRE_CALL(device->readBuffer(dst, 0, bufferDesc.size, blob.writeRef()));
    CHECK_EQ(std::memcmp(blob->getBufferPointer(), renderData.data(), sizeof(renderData)), 0);
    for (const auto& worker : resources)
    {
        for (const auto& r : worker)
        {
            REQUIRE_CALL(device->readBuffer(r.buffer, 0, bufferDesc.size, blob.writeRef()));
            auto words = static_cast<const uint32_t*>(blob->getBufferPointer());
            for (uint32_t j = 0; j < wordCount; ++j)
                CHECK_EQ(words[j], r.pattern);
            SubresourceLayout layout;
            REQUIRE_CALL(device->readTexture(r.texture, 0, 0, blob.writeRef(), &layout));
            for (uint32_t y = 0; y < 8; ++y)
            {
                auto row = reinterpret_cast<const uint32_t*>(
                    static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch
                );
                for (uint32_t x = 0; x < 8; ++x)
                    CHECK_EQ(row[x], r.pattern);
            }
        }
    }
}

static void testConcurrentPlacement(IDevice* device, bool useTextures)
{
    constexpr uint32_t threadCount = 8;
    BufferDesc bufferDesc = {};
    bufferDesc.size = 256;
    bufferDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    TextureDesc textureDesc = {};
    textureDesc.size = {8, 8, 1};
    textureDesc.format = Format::R32Uint;
    textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource | TextureUsage::CopyDestination;
    ResourceMemoryRequirements requirements = {};
    REQUIRE_CALL(
        useTextures ? device->getTextureMemoryRequirements(textureDesc, &requirements)
                    : device->getBufferMemoryRequirements(bufferDesc, &requirements)
    );
    Size stride = ((requirements.size + requirements.alignment - 1) / requirements.alignment) * requirements.alignment;
    ResourceHeapDesc heapDesc = {};
    heapDesc.size = stride * threadCount;
    heapDesc.memoryType = requirements.memoryType;
    heapDesc.requirements = &requirements;
    heapDesc.requirementCount = 1;
    auto sharedHeap = device->createResourceHeap(heapDesc);
    REQUIRE(sharedHeap);
    std::array<ComPtr<IBuffer>, threadCount> buffers;
    std::array<ComPtr<ITexture>, threadCount> textures;
    std::array<Result, threadCount> results{};
    Barrier start(threadCount);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                DeviceScope scope(device);
                start.arriveAndWait();
                results[i] = [&]() -> Result
                {
                    // Half share disjoint placements; half also create independent heaps.
                    ComPtr<IResourceHeap> heap = sharedHeap;
                    if (i % 2)
                        SLANG_RETURN_ON_FAIL(device->createResourceHeap(heapDesc, heap.writeRef()));
                    std::array<uint32_t, 64> data;
                    data.fill(i + 1);
                    if (useTextures)
                    {
                        TextureDesc desc = textureDesc;
                        desc.heap = heap;
                        desc.heapOffset = i * stride;
                        SubresourceData initData = {data.data(), 8 * sizeof(uint32_t), sizeof(data)};
                        return device->createTexture(desc, &initData, textures[i].writeRef());
                    }
                    BufferDesc desc = bufferDesc;
                    desc.heap = heap;
                    desc.heapOffset = i * stride;
                    return device->createBuffer(desc, data.data(), buffers[i].writeRef());
                }();
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    for (Result result : results)
        REQUIRE_CALL(result);
    REQUIRE_CALL(device->getQueue(QueueType::Graphics)->waitOnHost());
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        ComPtr<ISlangBlob> blob;
        if (useTextures)
        {
            SubresourceLayout layout;
            REQUIRE_CALL(device->readTexture(textures[i], 0, 0, blob.writeRef(), &layout));
            for (uint32_t y = 0; y < 8; ++y)
            {
                auto row = reinterpret_cast<const uint32_t*>(
                    static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch
                );
                for (uint32_t x = 0; x < 8; ++x)
                    CHECK_EQ(row[x], i + 1);
            }
        }
        else
        {
            REQUIRE_CALL(device->readBuffer(buffers[i], 0, bufferDesc.size, blob.writeRef()));
            auto words = static_cast<const uint32_t*>(blob->getBufferPointer());
            for (uint32_t j = 0; j < 64; ++j)
                CHECK_EQ(words[j], i + 1);
        }
    }
}

GPU_TEST_CASE("resource-thread-safety-placed-buffers", D3D12 | Vulkan | CUDA | Metal)
{
    testConcurrentPlacement(device, false);
}

GPU_TEST_CASE("resource-thread-safety-placed-textures", D3D12 | Vulkan | Metal)
{
    testConcurrentPlacement(device, true);
}

GPU_TEST_CASE("resource-thread-safety-heap-allocation", CUDA | Vulkan)
{
    constexpr uint32_t threadCount = 4;
    constexpr uint32_t rounds = 8;
    constexpr uint32_t batchSize = 16;
    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap({}, heap.writeRef()));
    std::array<std::array<rhi::HeapAlloc, batchSize>, threadCount> allocations{};
    std::atomic<uint32_t> errors{0};
    Barrier phase(threadCount + 1);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                DeviceScope scope(device);
                for (uint32_t round = 0; round < rounds; ++round)
                {
                    for (auto& allocation : allocations[i])
                    {
                        allocation = {};
                        HeapAllocDesc desc = {};
                        desc.size = 4096;
                        desc.alignment = 128;
                        if (SLANG_FAILED(heap->allocate(desc, &allocation)))
                            ++errors;
                    }
                    phase.arriveAndWait();
                    phase.arriveAndWait();
                    for (auto& allocation : allocations[i])
                    {
                        if (allocation.pageId && SLANG_FAILED(heap->free(allocation)))
                            ++errors;
                    }
                    phase.arriveAndWait();
                }
            }
        );
    }
    for (uint32_t round = 0; round < rounds; ++round)
    {
        phase.arriveAndWait();
        std::set<DeviceAddress> addresses;
        for (const auto& batch : allocations)
            for (const auto& allocation : batch)
                CHECK(addresses.insert(allocation.getDeviceAddress()).second);
        phase.arriveAndWait();
        // These operations overlap frees and the next round's allocations.
        HeapReport report = {};
        CHECK_CALL(heap->report(&report));
        CHECK_CALL(heap->flush());
        CHECK_CALL(heap->removeEmptyPages());
        phase.arriveAndWait();
    }
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(errors.load(), 0);
    REQUIRE_CALL(heap->flush());
    CHECK_EQ(heap->report().numAllocations, 0);
}

GPU_TEST_CASE("resource-thread-safety-same-resource-descriptors", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::Bindless))
        SKIP("Bindless is not supported");
    for (uint32_t round = 0; round < 8; ++round)
    {
        BufferDesc desc = {};
        desc.size = 256;
        desc.usage = BufferUsage::ShaderResource;
        auto buffer = device->createBuffer(desc);
        TextureDesc textureDesc = {};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::ShaderResource;
        auto texture = device->createTexture(textureDesc);
        auto sampler = device->createSampler({});
        REQUIRE(buffer);
        REQUIRE(texture);
        REQUIRE(sampler);
        auto view = texture->createView({});
        REQUIRE(view);
        checkConcurrentValues(
            device,
            [&](uint32_t)
            {
                std::array<DescriptorHandle, 3> handles{};
                Result result = [&]() -> Result
                {
                    SLANG_RETURN_ON_FAIL(buffer->getDescriptorHandle(
                        DescriptorHandleAccess::Read,
                        Format::R32Float,
                        kEntireBuffer,
                        &handles[0]
                    ));
                    SLANG_RETURN_ON_FAIL(view->getDescriptorHandle(DescriptorHandleAccess::Read, &handles[1]));
                    return sampler->getDescriptorHandle(&handles[2]);
                }();
                return std::make_pair(result, handles);
            },
            [](const auto& value, const auto& expected)
            {
                CHECK_CALL(value.first);
                for (uint32_t i = 0; i < 3; ++i)
                {
                    CHECK(bool(value.second[i]));
                    CHECK(value.second[i].type == expected.second[i].type);
                    CHECK_EQ(value.second[i].value, expected.second[i].value);
                }
            }
        );
    }
}

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

GPU_TEST_CASE("resource-thread-safety-upload-preserves-surface-sync", Vulkan)
{
    auto impl = static_cast<vk::DeviceImpl*>(getUnderlyingDevice(device));
    auto& api = impl->m_api;
    VkSemaphore available = VK_NULL_HANDLE;
    VkSemaphore finished = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    REQUIRE_EQ(api.vkCreateSemaphore(impl->m_device, &semaphoreInfo, nullptr, &available), VK_SUCCESS);
    REQUIRE_EQ(api.vkCreateSemaphore(impl->m_device, &semaphoreInfo, nullptr, &finished), VK_SUCCESS);
    REQUIRE_EQ(api.vkCreateFence(impl->m_device, &fenceInfo, nullptr, &fence), VK_SUCCESS);
    {
        std::lock_guard<std::mutex> lock(impl->m_queueMutex);
        // Model acquisition without creating a window. Signaling available also
        // lets an incorrect implementation fail the assertions without hanging.
        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &available;
        REQUIRE_EQ(api.vkQueueSubmit(impl->m_queue->m_queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
        impl->m_queue->m_surfaceSync = {fence, available, finished};
    }
    TextureDesc desc = {};
    desc.format = Format::R32Uint;
    desc.usage = TextureUsage::ShaderResource;
    uint32_t data = 42;
    SubresourceData initData = {&data, sizeof(data), sizeof(data)};
    auto texture = device->createTexture(desc, &initData);
    CHECK(texture);
    CHECK_CALL(impl->m_queue->waitOnHost());
    CHECK(impl->m_queue->m_surfaceSync.fence == fence);
    CHECK(impl->m_queue->m_surfaceSync.imageAvailableSemaphore == available);
    CHECK(impl->m_queue->m_surfaceSync.renderFinishedSemaphore == finished);
    CHECK_EQ(api.vkGetFenceStatus(impl->m_device, fence), VK_NOT_READY);
    impl->m_queue->m_surfaceSync = {};
    api.vkDestroyFence(impl->m_device, fence, nullptr);
    api.vkDestroySemaphore(impl->m_device, available, nullptr);
    api.vkDestroySemaphore(impl->m_device, finished, nullptr);
}

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
