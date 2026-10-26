#include "testing.h"
#include "texture-test.h"

#include "../src/device.h"
#include "../src/resource-heap.h"
#include "../src/rhi-shared.h"
#include "../src/state-tracking.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-buffer.h"
#include "vulkan/vk-device.h"
#endif

#if SLANG_RHI_DEBUG
#include "debug-layer/debug-device.h"
#endif

#include <algorithm>
#include <cstring>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

static Size alignUp(Size value, Size alignment)
{
    REQUIRE_GT(alignment, 0);
    return ((value + alignment - 1) / alignment) * alignment;
}

static BufferDesc makeCopyBufferDesc(Size size, MemoryType memoryType = MemoryType::DeviceLocal)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
    desc.memoryType = memoryType;
    return desc;
}

static BufferDesc makeComputeBufferDesc(Size size, uint32_t elementSize)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.elementSize = elementSize;
    desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                 BufferUsage::CopySource;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.memoryType = MemoryType::DeviceLocal;
    return desc;
}

static TextureDesc makeSampleTextureDesc(TextureUsage extraUsage = TextureUsage::None)
{
    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.size = {32, 32, 1};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource | TextureUsage::CopyDestination | extraUsage;
    desc.memoryType = MemoryType::DeviceLocal;
    return desc;
}

static ResourceMemoryRequirements requireBufferMemoryRequirements(IDevice* device, const BufferDesc& desc)
{
    ResourceMemoryRequirements requirements = {};
    REQUIRE_CALL(device->getBufferMemoryRequirements(desc, &requirements));
    return requirements;
}

static ResourceMemoryRequirements requireTextureMemoryRequirements(IDevice* device, const TextureDesc& desc)
{
    ResourceMemoryRequirements requirements = {};
    REQUIRE_CALL(device->getTextureMemoryRequirements(desc, &requirements));
    return requirements;
}

static ComPtr<IResourceHeap> createHeap(
    IDevice* device,
    Size size,
    ResourceHeapUsage usage = ResourceHeapUsage::Buffers,
    MemoryType memoryType = MemoryType::DeviceLocal,
    const char* label = "test-resource-heap"
)
{
    ResourceMemoryRequirements requirements = {};
    if (is_set(usage, ResourceHeapUsage::RtDsTextures))
    {
        TextureDesc resourceDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
        requirements = requireTextureMemoryRequirements(device, resourceDesc);
    }
    else if (is_set(usage, ResourceHeapUsage::NonRtDsTextures))
    {
        TextureDesc resourceDesc = makeSampleTextureDesc();
        requirements = requireTextureMemoryRequirements(device, resourceDesc);
    }
    else
    {
        BufferDesc resourceDesc = makeCopyBufferDesc(256, memoryType);
        requirements = requireBufferMemoryRequirements(device, resourceDesc);
    }

    ResourceHeapDesc desc = {};
    desc.memoryType = memoryType;
    desc.usage = usage;
    desc.size = std::max(size, requirements.size);
    desc.requirements = &requirements;
    desc.requirementCount = 1;
    desc.label = label;
    ComPtr<IResourceHeap> heap;
    REQUIRE_CALL(device->createResourceHeap(desc, heap.writeRef()));
    CHECK(heap);
    CHECK_EQ(heap->getDesc().memoryType, memoryType);
    CHECK_EQ(heap->getDesc().usage, usage);
    CHECK_GE(heap->getDesc().size, size);
    return heap;
}

static ComPtr<IResourceHeap> createHeapForRequirements(
    IDevice* device,
    const ResourceMemoryRequirements& requirements,
    Size size = 0,
    const char* label = "test-resource-heap"
)
{
    ResourceHeapDesc desc = {};
    desc.memoryType = requirements.memoryType;
    desc.size = size ? size : requirements.size;
    desc.requirements = &requirements;
    desc.requirementCount = 1;
    desc.label = label;
    ComPtr<IResourceHeap> heap;
    REQUIRE_CALL(device->createResourceHeap(desc, heap.writeRef()));
    CHECK(heap);
    CHECK_EQ(heap->getDesc().memoryType, requirements.memoryType);
    CHECK(isResourceHeapUsageCompatible(heap->getDesc().usage, requirements.usage));
    CHECK_GE(heap->getDesc().alignment, requirements.heapAlignment);
    CHECK(heap->getDesc().next == nullptr);
    REQUIRE_EQ(heap->getDesc().requirementCount, 1);
    REQUIRE(heap->getDesc().requirements != nullptr);
    CHECK(heap->getDesc().requirements != &requirements);
    CHECK(heap->getDesc().requirements[0].next == nullptr);
    return heap;
}

static ComPtr<IBuffer> createPlacedBuffer(
    IDevice* device,
    BufferDesc desc,
    IResourceHeap* heap,
    Offset offset,
    const void* initData = nullptr
)
{
    desc.heap = heap;
    desc.heapOffset = offset;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, initData, buffer.writeRef()));
    CHECK(buffer->getDesc().next == nullptr);
    CHECK(buffer->getDesc().heap == nullptr);
    CHECK(buffer->getDesc().heapOffset == 0);
    return buffer;
}

static Result tryCreatePlacedBuffer(IDevice* device, BufferDesc desc, IResourceHeap* heap, Offset offset)
{
    desc.heap = heap;
    desc.heapOffset = offset;
    ComPtr<IBuffer> buffer;
    return device->createBuffer(desc, nullptr, buffer.writeRef());
}

static ComPtr<ITexture> createPlacedTexture(
    IDevice* device,
    TextureDesc desc,
    IResourceHeap* heap,
    Offset offset,
    const SubresourceData* initData = nullptr
)
{
    desc.heap = heap;
    desc.heapOffset = offset;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, initData, texture.writeRef()));
    CHECK(texture->getDesc().next == nullptr);
    CHECK(texture->getDesc().heap == nullptr);
    CHECK(texture->getDesc().heapOffset == 0);
    return texture;
}

static Result tryCreatePlacedTexture(IDevice* device, TextureDesc desc, IResourceHeap* heap, Offset offset)
{
    desc.heap = heap;
    desc.heapOffset = offset;
    ComPtr<ITexture> texture;
    return device->createTexture(desc, nullptr, texture.writeRef());
}

static void uploadTextureData(IDevice* device, ITexture* texture, const TextureData& data)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->uploadTextureData(
        texture,
        {0, data.desc.getLayerCount(), 0, data.desc.mipCount},
        {0, 0, 0},
        Extent3D::kWholeTexture,
        data.subresourceData.data(),
        data.subresourceData.size()
    );
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

static void dispatchIncrement(IDevice* device, IComputePipeline* pipeline, IBuffer* buffer)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto passEncoder = encoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor(rootObject)["buffer"].setBinding(buffer);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

TEST_CASE("resource-heap-alias-state-retirement")
{
    // Synthetic placements make the transition ordering independent of GPU compression behavior.
    ComPtr<ResourceHeap> heap(new ResourceHeap(nullptr, {}));
    ComPtr<ResourceHeap> otherHeap(new ResourceHeap(nullptr, {}));
    TextureDesc desc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    desc.mipCount = 2;
    desc.defaultState = ResourceState::ShaderResource;
    ComPtr<Texture> first(new Texture(nullptr, desc));
    ComPtr<Texture> second(new Texture(nullptr, desc));
    ComPtr<Texture> destination(new Texture(nullptr, desc));
    ComPtr<Texture> adjacent(new Texture(nullptr, desc));
    ComPtr<Texture> unrelated(new Texture(nullptr, desc));
    ComPtr<Texture> committed(new Texture(nullptr, desc));
    struct TestBuffer : Buffer
    {
        using Buffer::Buffer;
        SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override { return 0; }
    };
    auto bufferDesc = makeCopyBufferDesc(256);
    bufferDesc.defaultState = ResourceState::CopySource;
    ComPtr<TestBuffer> buffer(new TestBuffer(nullptr, bufferDesc));
    ResourceMemoryRequirements requirements = {};
    requirements.size = 256;
    first->setPlacement(heap, 0, requirements);
    second->setPlacement(heap, 256, requirements);
    buffer->setPlacement(heap, 512, requirements);
    adjacent->setPlacement(heap, 768, requirements);
    unrelated->setPlacement(otherHeap, 0, requirements);
    requirements.size = 768;
    destination->setPlacement(heap, 0, requirements);
    IResource* destinations[] = {destination};

    for (bool restoreDefaultStates : {false, true})
    {
        CAPTURE(restoreDefaultStates);
        StateTracking tracking;
        tracking.setTextureState(first, kEntireTexture, ResourceState::RenderTarget);
        tracking.setTextureState(second, {0, 1, 0, 1}, ResourceState::RenderTarget);
        tracking.setTextureState(second, {0, 1, 1, 1}, ResourceState::CopySource);
        tracking.setBufferState(buffer, ResourceState::CopyDestination);
        for (auto texture : {adjacent.get(), unrelated.get(), committed.get()})
            tracking.setTextureState(texture, kEntireTexture, ResourceState::CopySource);
        tracking.clearBarriers();

        tracking.deactivateAliases(destinations, restoreDefaultStates);
        CHECK_EQ(tracking.getBufferBarriers().size(), restoreDefaultStates ? 1 : 0);
        if (restoreDefaultStates)
        {
            CHECK_EQ(tracking.getBufferBarriers()[0].buffer, buffer.get());
            CHECK_EQ(tracking.getBufferBarriers()[0].stateAfter, ResourceState::CopySource);
        }
        const auto& barriers = tracking.getTextureBarriers();
        CHECK_EQ(barriers.size(), restoreDefaultStates ? 3 : 0);
        for (const auto& barrier : barriers)
        {
            CHECK((barrier.texture == first.get() || barrier.texture == second.get()));
            CHECK_EQ(barrier.stateAfter, ResourceState::ShaderResource);
        }
        tracking.clearBarriers();

        // Activation/discard changes the destination state. A repeated activation must neither
        // restore an already inactive predecessor nor forget this new destination state.
        tracking.setTextureState(destination, kEntireTexture, ResourceState::RenderTarget);
        tracking.clearBarriers();
        tracking.deactivateAliases(destinations, restoreDefaultStates);
        CHECK(tracking.getBufferBarriers().empty());
        CHECK(tracking.getTextureBarriers().empty());
        tracking.setTextureState(destination, kEntireTexture, ResourceState::CopySource);
        REQUIRE_EQ(tracking.getTextureBarriers().size(), 1);
        CHECK_EQ(tracking.getTextureBarriers()[0].stateBefore, ResourceState::RenderTarget);
        tracking.clearBarriers();

        // End-of-command-buffer restoration must leave inactive resources alone and preserve
        // tracking for adjacent allocations, other heaps, and committed resources.
        tracking.requireDefaultStates();
        CHECK(tracking.getBufferBarriers().empty());
        CHECK_EQ(tracking.getTextureBarriers().size(), 4);
        for (const auto& barrier : tracking.getTextureBarriers())
        {
            CHECK_NE(barrier.texture, first.get());
            CHECK_NE(barrier.texture, second.get());
        }
        tracking.clearBarriers();
        tracking.setTextureState(second, kEntireTexture, ResourceState::RenderTarget);
        REQUIRE_EQ(tracking.getTextureBarriers().size(), 1);
        CHECK_EQ(tracking.getTextureBarriers()[0].stateBefore, ResourceState::ShaderResource);

        // Unsorted destinations on two heaps. The large predecessor overlaps two
        // destinations, while the allocation in their gap and the adjacent one survive.
        ComPtr<Texture> otherPredecessor(new Texture(nullptr, desc));
        otherPredecessor->setPlacement(otherHeap, 0, unrelated->m_placementRequirements);
        tracking.clear();
        for (auto texture : {destination.get(), otherPredecessor.get(), first.get(), unrelated.get()})
            tracking.setTextureState(texture, kEntireTexture, ResourceState::RenderTarget);
        tracking.setBufferState(buffer, ResourceState::UnorderedAccess);
        for (auto texture : {second.get(), adjacent.get(), committed.get()})
            tracking.setTextureState(texture, kEntireTexture, ResourceState::CopySource);
        tracking.clearBarriers();

        IResource* batch[] = {buffer, unrelated, first};
        tracking.deactivateAliases(batch, restoreDefaultStates);
        CHECK(tracking.getBufferBarriers().empty());
        CHECK_EQ(tracking.getTextureBarriers().size(), restoreDefaultStates ? 2 : 0);
        for (const auto& barrier : tracking.getTextureBarriers())
        {
            CHECK((barrier.texture == destination.get() || barrier.texture == otherPredecessor.get()));
            CHECK_EQ(barrier.stateAfter, ResourceState::ShaderResource);
        }
        tracking.clearBarriers();

        // Destinations retain their current states; gap, boundary, and committed
        // resources remain tracked. Retired predecessors need no final transition.
        for (auto texture : {first.get(), unrelated.get()})
            tracking.setTextureState(texture, kEntireTexture, ResourceState::ShaderResource);
        REQUIRE_EQ(tracking.getTextureBarriers().size(), 2);
        for (const auto& barrier : tracking.getTextureBarriers())
            CHECK_EQ(barrier.stateBefore, ResourceState::RenderTarget);
        tracking.clearBarriers();
        tracking.requireDefaultStates();
        REQUIRE_EQ(tracking.getBufferBarriers().size(), 1);
        CHECK_EQ(tracking.getBufferBarriers()[0].buffer, buffer.get());
        CHECK_EQ(tracking.getBufferBarriers()[0].stateBefore, ResourceState::UnorderedAccess);
        REQUIRE_EQ(tracking.getTextureBarriers().size(), 3);
        for (const auto& barrier : tracking.getTextureBarriers())
        {
            CHECK(
                (barrier.texture == second.get() || barrier.texture == adjacent.get() ||
                 barrier.texture == committed.get())
            );
            CHECK_EQ(barrier.stateBefore, ResourceState::CopySource);
        }
    }
}

#if SLANG_RHI_ENABLE_VULKAN
namespace {
struct MockMemoryAllocator
{
    std::vector<uint32_t> attempts;
    VkResult cachedResult = VK_SUCCESS;

    static VKAPI_ATTR VkResult VKAPI_CALL
    allocate(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* memory)
    {
        auto& self = *reinterpret_cast<MockMemoryAllocator*>(device);
        self.attempts.push_back(info->memoryTypeIndex);
        if (info->memoryTypeIndex == 2 && self.cachedResult != VK_SUCCESS)
            return self.cachedResult;
        *memory = (VkDeviceMemory)1;
        return VK_SUCCESS;
    }
};
} // namespace

TEST_CASE("resource-heap-vulkan-memory-preference")
{
    MockMemoryAllocator allocator;
    vk::VulkanApi api = {};
    api.m_device = reinterpret_cast<VkDevice>(&allocator);
    api.vkAllocateMemory = MockMemoryAllocator::allocate;
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    api.m_deviceMemoryProperties.memoryTypeCount = 3;
    api.m_deviceMemoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    api.m_deviceMemoryProperties.memoryTypes[1].propertyFlags = required;
    api.m_deviceMemoryProperties.memoryTypes[2].propertyFlags = required | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VkMemoryRequirements requirements = {};
    requirements.size = 4096;
    requirements.memoryTypeBits = 7;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t chosenType = ~0u;
    auto allocate = [&](VkMemoryPropertyFlags preferred)
    {
        allocator.attempts.clear();
        chosenType = ~0u;
        return vk::allocateVkMemory(api, requirements, required, preferred, false, 0, &memory, &chosenType);
    };

    REQUIRE_CALL(allocate(VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    CHECK_EQ(chosenType, 2);
    CHECK(allocator.attempts == std::vector<uint32_t>{2});

    allocator.cachedResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    REQUIRE_CALL(allocate(VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    CHECK_EQ(chosenType, 1);
    CHECK(allocator.attempts == std::vector<uint32_t>{2, 1});

    allocator.cachedResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    CHECK(SLANG_FAILED(allocate(VK_MEMORY_PROPERTY_HOST_CACHED_BIT)));
    CHECK(allocator.attempts == std::vector<uint32_t>{2});

    // The allocation's compatibility mask always takes precedence over preferences.
    requirements.memoryTypeBits = 3;
    REQUIRE_CALL(allocate(VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    CHECK_EQ(chosenType, 1);
    CHECK(allocator.attempts == std::vector<uint32_t>{1});

    requirements.memoryTypeBits = 1;
    CHECK_EQ(allocate(VK_MEMORY_PROPERTY_HOST_CACHED_BIT), SLANG_E_NOT_AVAILABLE);
    CHECK(allocator.attempts.empty());

    requirements.memoryTypeBits = 7;
    REQUIRE_CALL(allocate(0)); // Upload allocations retain their required-only policy.
    CHECK_EQ(chosenType, 1);
}

GPU_TEST_CASE("resource-heap-vulkan-cached-readback", Vulkan)
{
    const uint32_t expected[] = {0x10203040, 0x50607080, 0x90A0B0C0, 0xD0E0F000};
    auto desc = makeCopyBufferDesc(sizeof(expected), MemoryType::ReadBack);
    auto requirements = requireBufferMemoryRequirements(device, desc);
    auto heap = createHeapForRequirements(device, requirements);
    auto buffer = createPlacedBuffer(device, desc, heap, 0);

    auto backendDevice = checked_cast<vk::DeviceImpl*>(getUnderlyingDevice(device));
    const auto& api = backendDevice->m_api;
    const auto properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    uint32_t cachedMask = 0;
    for (uint32_t i = 0; i < api.m_deviceMemoryProperties.memoryTypeCount; ++i)
        if ((api.m_deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            cachedMask |= 1u << i;
    cachedMask &= uint32_t(getResourceHeapCompatibilityMask(requirements));
    if (cachedMask)
    {
        auto cachedRequirements = requirements;
        setResourceHeapCompatibility(cachedRequirements, backendDevice, cachedMask);
        bool compatible = false;
        REQUIRE_CALL(device->isResourceHeapCompatible(heap, cachedRequirements, &compatible));
        CHECK(compatible);
    }

    auto source = device->createBuffer(makeCopyBufferDesc(sizeof(expected), MemoryType::Upload), expected);
    REQUIRE(source);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->aliasResources(nullptr, buffer);
    encoder->copyBuffer(buffer, 0, source, 0, sizeof(expected));
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    void* mapped = nullptr;
    REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &mapped));
    CHECK_EQ(std::memcmp(mapped, expected, sizeof(expected)), 0);
    REQUIRE_CALL(device->unmapBuffer(buffer));
}
#endif

GPU_TEST_CASE("resource-heap-create", D3D12 | Vulkan | Metal | CUDA)
{
    CHECK(device->hasFeature(Feature::ResourceHeaps));
    CHECK(device->hasFeature(Feature::ResourceAliasing));

    BufferDesc desc = makeCopyBufferDesc(64 * 1024);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, 0, "named-resource-heap");
    CHECK(heap->getDesc().label != nullptr);
    CHECK_EQ(std::strcmp(heap->getDesc().label, "named-resource-heap"), 0);

    NativeHandle handle = {};
    REQUIRE_CALL(heap->getNativeHandle(&handle));
    CHECK(handle);
    switch (device->getDeviceType())
    {
    case DeviceType::D3D12:
        CHECK_EQ(handle.type, NativeHandleType::D3D12Heap);
        break;
    case DeviceType::Vulkan:
        CHECK_EQ(handle.type, NativeHandleType::VkDeviceMemory);
        break;
    case DeviceType::Metal:
        CHECK_EQ(handle.type, NativeHandleType::MTLHeap);
        break;
    case DeviceType::CUDA:
        CHECK_EQ(handle.type, NativeHandleType::CUdeviceptr);
        break;
    default:
        break;
    }

    bool compatible = false;
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, requirements, &compatible));
    CHECK(compatible);

    ResourceMemoryRequirements incompatible = requirements;
    incompatible.memoryType = MemoryType::Upload;
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, incompatible, &compatible));
    CHECK(!compatible);

    auto otherDevice = createTestingDevice(ctx, ctx->deviceType, false);
    incompatible = requireBufferMemoryRequirements(otherDevice, desc);
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, incompatible, &compatible));
    CHECK(!compatible);

    auto placed = createPlacedBuffer(device, desc, heap, 0);
    BufferDesc copiedDesc = placed->getDesc();
    // A copied descriptor defaults to an independent committed allocation.
    // Offsets are ignored when no heap is supplied.
    copiedDesc.heapOffset = 1;
    auto committed = device->createBuffer(copiedDesc);
    REQUIRE(committed);
    CHECK(!checked_cast<Buffer*>(committed.get())->isPlaced());
    CHECK(committed->getDesc().heapOffset == 0);
}

GPU_TEST_CASE("resource-heap-vulkan-disjoint-memory-types", Vulkan)
{
    auto requirements = requireBufferMemoryRequirements(device, makeCopyBufferDesc(256));
    ResourceMemoryRequirements heapRequirements[] = {requirements, requirements};
    IDevice* innerDevice = device;
#if SLANG_RHI_DEBUG
    innerDevice = checked_cast<debug::DebugDevice*>(innerDevice)->baseObject;
#endif
    auto backendDevice = checked_cast<Device*>(innerDevice);
    // Synthetic masks make this independent of the physical device's memory types.
    setResourceHeapCompatibility(heapRequirements[0], backendDevice, 1);
    setResourceHeapCompatibility(heapRequirements[1], backendDevice, 2);

    ResourceHeapDesc desc = {};
    desc.size = requirements.size;
    desc.memoryType = requirements.memoryType;
    desc.requirements = heapRequirements;
    desc.requirementCount = 2;
    ComPtr<IResourceHeap> heap;
    CHECK(device->createResourceHeap(desc, heap.writeRef()) == SLANG_E_NOT_AVAILABLE);
    CHECK(!heap);
}

GPU_TEST_CASE("resource-heap-d3d12-host-visible-buffer-flags", D3D12)
{
    const uint32_t expected[] = {0x10203040u, 0x50607080u, 0x90A0B0C0u, 0xD0E0F000u};
    for (BufferUsage extraUsage : {BufferUsage::UnorderedAccess, BufferUsage::AccelerationStructure})
    {
        CAPTURE(extraUsage);
        BufferDesc uploadDesc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
        uploadDesc.usage |= extraUsage;
        uploadDesc.defaultState = ResourceState::CopySource;
        auto uploadRequirements = requireBufferMemoryRequirements(device, uploadDesc);
        auto uploadHeap = createHeapForRequirements(device, uploadRequirements);
        auto upload = createPlacedBuffer(device, uploadDesc, uploadHeap, 0);

        BufferDesc readbackDesc = makeCopyBufferDesc(sizeof(expected), MemoryType::ReadBack);
        readbackDesc.usage |= extraUsage;
        readbackDesc.defaultState = ResourceState::CopyDestination;
        auto readbackRequirements = requireBufferMemoryRequirements(device, readbackDesc);
        auto readbackHeap = createHeapForRequirements(device, readbackRequirements);
        auto readback = createPlacedBuffer(device, readbackDesc, readbackHeap, 0);

        // Host-visible buffers retain their fixed copy states, even with these usage hints.
        void* mappedData = nullptr;
        REQUIRE_CALL(device->mapBuffer(upload, CpuAccessMode::Write, &mappedData));
        REQUIRE(mappedData != nullptr);
        std::memcpy(mappedData, expected, sizeof(expected));
        REQUIRE_CALL(device->unmapBuffer(upload));

        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        encoder->copyBuffer(readback, 0, upload, 0, sizeof(expected));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());

        REQUIRE_CALL(device->mapBuffer(readback, CpuAccessMode::Read, &mappedData));
        REQUIRE(mappedData != nullptr);
        CHECK(std::memcmp(mappedData, expected, sizeof(expected)) == 0);
        REQUIRE_CALL(device->unmapBuffer(readback));
    }
}

GPU_TEST_CASE("resource-heap-d3d12-msaa-alignment", D3D12)
{
    TextureDesc sampledDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements sampledRequirements = requireTextureMemoryRequirements(device, sampledDesc);
    CHECK_EQ(sampledRequirements.usage, ResourceHeapUsage::NonRtDsTextures);

    ResourceHeapDesc sampledHeapDesc = {};
    sampledHeapDesc.memoryType = sampledRequirements.memoryType;
    sampledHeapDesc.usage = ResourceHeapUsage::NonRtDsTextures;
    sampledHeapDesc.size = Size(4) * 1024 * 1024;
    sampledHeapDesc.alignment = Size(4) * 1024 * 1024;
    sampledHeapDesc.requirements = &sampledRequirements;
    sampledHeapDesc.requirementCount = 1;
    ComPtr<IResourceHeap> sampledHeap;
    REQUIRE_CALL(device->createResourceHeap(sampledHeapDesc, sampledHeap.writeRef()));
    CHECK_EQ(sampledHeap->getDesc().alignment, sampledHeapDesc.alignment);
    CHECK(createPlacedTexture(device, sampledDesc, sampledHeap, 0));

    TextureDesc rtDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    rtDesc.type = TextureType::Texture2DMS;
    rtDesc.sampleCount = 4;

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, rtDesc);
    CHECK_EQ(requirements.usage, ResourceHeapUsage::RtDsTextures);
    CHECK_EQ(requirements.heapAlignment, Size(4) * 1024 * 1024);

    ComPtr<IResourceHeap> rtHeap = createHeapForRequirements(device, requirements);
    CHECK_EQ(rtHeap->getDesc().alignment, requirements.heapAlignment);
    CHECK(createPlacedTexture(device, rtDesc, rtHeap, 0));

    ResourceHeapDesc allHeapDesc = {};
    allHeapDesc.memoryType = requirements.memoryType;
    allHeapDesc.usage = ResourceHeapUsage::All;
    allHeapDesc.size = requirements.size;
    allHeapDesc.requirements = &requirements;
    allHeapDesc.requirementCount = 1;
    ComPtr<IResourceHeap> allHeap;
    REQUIRE_CALL(device->createResourceHeap(allHeapDesc, allHeap.writeRef()));
    CHECK_EQ(allHeap->getDesc().alignment, requirements.heapAlignment);
    CHECK(createPlacedTexture(device, rtDesc, allHeap, 0));
}

GPU_TEST_CASE("resource-heap-alias-batch", D3D12 | Vulkan | Metal | CUDA)
{
    auto desc = makeComputeBufferDesc(256, sizeof(uint32_t));
    desc.defaultState = ResourceState::CopySource;
    auto requirements = requireBufferMemoryRequirements(device, desc);
    const Offset stride = alignUp(requirements.size, requirements.alignment);
    auto largeDesc = desc;
    largeDesc.size = 2 * stride;
    auto largeRequirements = requireBufferMemoryRequirements(device, largeDesc);
    ResourceMemoryRequirements heapRequirements[] = {requirements, largeRequirements};
    ResourceHeapDesc heapDesc = {};
    heapDesc.size = std::max(stride + requirements.size, largeRequirements.size);
    heapDesc.requirements = heapRequirements;
    heapDesc.requirementCount = SLANG_COUNT_OF(heapRequirements);

    ComPtr<IBuffer> partitions[4];
    ComPtr<IBuffer> merged[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        auto heap = device->createResourceHeap(heapDesc);
        REQUIRE(heap);
        partitions[2 * i] = createPlacedBuffer(device, desc, heap, 0);
        partitions[2 * i + 1] = createPlacedBuffer(device, desc, heap, stride);
        merged[i] = createPlacedBuffer(device, largeDesc, heap, 0);
    }

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    {
        // The list order is deliberately different across heaps: entries are not paired by index.
        IResource* partitionResources[] = {partitions[0], partitions[1], partitions[2], partitions[3]};
        IResource* mergedResources[] = {merged[1], merged[0]};
        encoder->aliasResources(0, nullptr, SLANG_COUNT_OF(partitionResources), partitionResources);
        for (auto& buffer : partitions)
            encoder->clearBuffer(buffer);

        // Activation must flush a pending transition before retiring its resource.
        encoder->setBufferState(partitions[0], ResourceState::CopySource);

        // Many-to-many across two heaps, with multiple predecessors per destination.
        encoder->aliasResources(
            SLANG_COUNT_OF(partitionResources),
            partitionResources,
            SLANG_COUNT_OF(mergedResources),
            mergedResources
        );
        for (auto& buffer : merged)
            encoder->clearBuffer(buffer);

        // Split each large predecessor back into two disjoint destinations.
        encoder->aliasResources(
            SLANG_COUNT_OF(mergedResources),
            mergedResources,
            SLANG_COUNT_OF(partitionResources),
            partitionResources
        );

        // Neither array storage nor the caller's references may be needed by deferred recording.
        std::fill_n(partitionResources, SLANG_COUNT_OF(partitionResources), nullptr);
        std::fill_n(mergedResources, SLANG_COUNT_OF(mergedResources), nullptr);
        for (auto& buffer : merged)
            buffer.setNull();
    }
    for (uint32_t i = 0; i < SLANG_COUNT_OF(partitions); ++i)
    {
        const uint32_t values[] = {i + 1, i + 2, i + 3, i + 4};
        REQUIRE_CALL(encoder->uploadBufferData(partitions[i], 0, sizeof(values), values));
    }
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    for (uint32_t i = 0; i < SLANG_COUNT_OF(partitions); ++i)
        compareComputeResult(device, partitions[i], makeArray<uint32_t>(i + 1, i + 2, i + 3, i + 4));
}

GPU_TEST_CASE("resource-heap-alias-synchronization", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t dataA[] = {1, 2, 3, 4};
    const uint32_t dataB[] = {5, 6, 7, 8};
    BufferDesc desc = makeCopyBufferDesc(sizeof(dataA));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
    ComPtr<IBuffer> bufferA = createPlacedBuffer(device, desc, heap, 0);
    ComPtr<IBuffer> bufferB = createPlacedBuffer(device, desc, heap, 0);
    auto queue = device->getQueue(QueueType::Graphics);

    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, bufferA);
        REQUIRE_CALL(encoder->uploadBufferData(bufferA, 0, sizeof(dataA), dataA));
        encoder->aliasResources(bufferA, bufferB);
        REQUIRE_CALL(encoder->uploadBufferData(bufferB, 0, sizeof(dataB), dataB));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, bufferB, makeArray<uint32_t>(5, 6, 7, 8));
    }

    {
        auto firstEncoder = queue->createCommandEncoder();
        firstEncoder->aliasResources(bufferB, bufferA);
        REQUIRE_CALL(firstEncoder->uploadBufferData(bufferA, 0, sizeof(dataA), dataA));
        REQUIRE_CALL(queue->submit(firstEncoder->finish()));

        auto secondEncoder = queue->createCommandEncoder();
        secondEncoder->aliasResources(bufferA, bufferB);
        REQUIRE_CALL(secondEncoder->uploadBufferData(bufferB, 0, sizeof(dataB), dataB));
        REQUIRE_CALL(queue->submit(secondEncoder->finish()));

        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, bufferB, makeArray<uint32_t>(5, 6, 7, 8));
    }
}

GPU_TEST_CASE("resource-heap-place-sequential-buffers", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 64;
    BufferDesc desc = makeCopyBufferDesc(elementCount * sizeof(uint32_t));
    desc.defaultState = ResourceState::CopySource;
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    const Offset offsetB = alignUp(requirements.size, requirements.alignment);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, offsetB + requirements.size);

    std::vector<uint32_t> dataA(elementCount, 0x11111111u);
    std::vector<uint32_t> dataB(elementCount, 0x22222222u);
    ComPtr<IBuffer> bufferA = createPlacedBuffer(device, desc, heap, 0, dataA.data());
    ComPtr<IBuffer> bufferB = createPlacedBuffer(device, desc, heap, offsetB, dataB.data());

    CHECK_EQ(bufferA->getDesc().size, desc.size);
    CHECK_EQ(bufferA->getDesc().memoryType, MemoryType::DeviceLocal);
    // Placed resources must retain the heap after the caller releases it.
    heap.setNull();

    compareComputeResult(device, bufferA, std::span<uint32_t>(dataA));
    compareComputeResult(device, bufferB, std::span<uint32_t>(dataB));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->copyBuffer(bufferB, 0, bufferA, 0, desc.size);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, bufferB, std::span<uint32_t>(dataA));
}

GPU_TEST_CASE("resource-heap-map-placed-upload", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {0x10203040u, 0x50607080u, 0x90A0B0C0u, 0xD0E0F000u};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offset = requirements.alignment;
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, offset + requirements.size);
    const uint32_t initial[] = {1, 2, 3, 4};
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, offset, initial);
    CHECK_EQ(buffer->getDesc().memoryType, MemoryType::Upload);
    compareComputeResult(device, buffer, makeArray<uint32_t>(1, 2, 3, 4));

    void* mappedData = nullptr;
    REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, &mappedData));
    REQUIRE(mappedData != nullptr);
    std::memcpy(mappedData, expected, sizeof(expected));
    REQUIRE_CALL(device->unmapBuffer(buffer));

    compareComputeResult(device, buffer, makeArray<uint32_t>(0x10203040u, 0x50607080u, 0x90A0B0C0u, 0xD0E0F000u));
}

GPU_TEST_CASE("resource-heap-compute", D3D12 | Vulkan | Metal | CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc desc = makeComputeBufferDesc(sizeof(initialData), sizeof(float));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, 0, initialData);

    dispatchIncrement(device, pipeline, buffer);
    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

GPU_TEST_CASE("resource-heap-buffer-placement-validation", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(4096);
    auto requirements = requireBufferMemoryRequirements(device, desc);
    auto heap = createHeapForRequirements(device, requirements, requirements.size + requirements.alignment);

    SUBCASE("offset-outside-heap")
    {
        // Metal may grow the allocation, so check against the actual heap size.
        CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, heap->getDesc().size), SLANG_E_INVALID_ARG);
    }
    SUBCASE("unaligned-offset")
    {
        if (requirements.alignment <= 1)
            SKIP("Device reports a 1-byte resource placement alignment");
        CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 1), SLANG_E_INVALID_ARG);
    }
    SUBCASE("resource-exceeds-heap")
    {
        desc.size = heap->getDesc().size + 64 * 1024;
        requirements = requireBufferMemoryRequirements(device, desc);
        REQUIRE_GT(requirements.size, heap->getDesc().size);
        CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
    }
    SUBCASE("memory-type-mismatch")
    {
        desc.memoryType = MemoryType::Upload;
        CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
    }
    SUBCASE("dedicated-allocation")
    {
        desc.usage |= BufferUsage::Shared;
        requirements = requireBufferMemoryRequirements(device, desc);
        REQUIRE_NE(
            requirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
            ResourceMemoryRequirementFlags::None
        );
        CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
    }
}

GPU_TEST_CASE("resource-heap-usage-mismatch", D3D12 | Vulkan | Metal)
{
    BufferDesc bufferDesc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements bufferRequirements = requireBufferMemoryRequirements(device, bufferDesc);
    ComPtr<IResourceHeap> textureHeap = createHeap(device, bufferRequirements.size, ResourceHeapUsage::NonRtDsTextures);
    CHECK_EQ(tryCreatePlacedBuffer(device, bufferDesc, textureHeap, 0), SLANG_E_INVALID_ARG);

    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements textureRequirements = requireTextureMemoryRequirements(device, textureDesc);
    ComPtr<IResourceHeap> bufferHeap = createHeap(device, textureRequirements.size, ResourceHeapUsage::Buffers);
    CHECK_EQ(tryCreatePlacedTexture(device, textureDesc, bufferHeap, 0), SLANG_E_INVALID_ARG);

    TextureDesc rtDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements rtRequirements = requireTextureMemoryRequirements(device, rtDesc);
    ComPtr<IResourceHeap> nonRtHeap = createHeap(device, rtRequirements.size, ResourceHeapUsage::NonRtDsTextures);
    CHECK_EQ(tryCreatePlacedTexture(device, rtDesc, nonRtHeap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-place-textures", D3D12 | Vulkan | Metal)
{
    for (TextureUsage extraUsage : {TextureUsage::None, TextureUsage::RenderTarget})
    {
        CAPTURE(extraUsage);
        TextureData data;
        data.init(device, makeSampleTextureDesc(extraUsage), TextureInitMode::Random, 1);

        ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, data.desc);
        CHECK_GE(requirements.size, 32 * 32 * 4);
        CHECK_EQ(
            requirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
            ResourceMemoryRequirementFlags::None
        );

        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        ComPtr<ITexture> texture = createPlacedTexture(device, data.desc, heap, 0, data.subresourceData.data());
        CHECK_EQ(texture->getDesc().usage & TextureUsage::RenderTarget, extraUsage);
        data.checkEqual(texture);
    }
}

GPU_TEST_CASE("resource-heap-vulkan-exact-texture-requirements", Vulkan)
{
    {
        TextureDesc desc = makeSampleTextureDesc(TextureUsage::Typeless);
        ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, desc);
        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        CHECK(createPlacedTexture(device, desc, heap, 0));
    }

    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture3D;
        desc.size = {16, 16, 4};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
        ResourceMemoryRequirements requirements = {};
        Result result = device->getTextureMemoryRequirements(desc, &requirements);
        if (result == SLANG_E_NOT_AVAILABLE)
        {
            MESSAGE("Skipping placed 3D render target: 2D views on 3D images are not supported");
        }
        else
        {
            REQUIRE_CALL(result);
            ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
            CHECK(createPlacedTexture(device, desc, heap, 0));
        }
    }

    {
        TextureData data;
        data.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 4);
        data.desc.usage &= ~TextureUsage::CopyDestination;
        ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, data.desc);
        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        ComPtr<ITexture> texture = createPlacedTexture(device, data.desc, heap, 0, data.subresourceData.data());
        data.checkEqual(texture);
    }
}

GPU_TEST_CASE("resource-heap-vulkan-buffer-image-granularity", Vulkan)
{
    BufferDesc bufferDesc = makeCopyBufferDesc(4096);
    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements requirements[] = {
        requireBufferMemoryRequirements(device, bufferDesc),
        requireTextureMemoryRequirements(device, textureDesc),
    };
    const Offset textureOffset = alignUp(requirements[0].size, requirements[1].alignment);

    ResourceHeapDesc heapDesc = {};
    heapDesc.memoryType = MemoryType::DeviceLocal;
    heapDesc.size = textureOffset + requirements[1].size;
    heapDesc.requirements = requirements;
    heapDesc.requirementCount = 2;
    ComPtr<IResourceHeap> heap;
    REQUIRE_CALL(device->createResourceHeap(heapDesc, heap.writeRef()));

    CHECK(createPlacedBuffer(device, bufferDesc, heap, 0));
    CHECK(createPlacedTexture(device, textureDesc, heap, textureOffset));
}

GPU_TEST_CASE("resource-heap-alias-buffer-texture", D3D12 | Vulkan | Metal)
{
    BufferDesc bufferDesc = makeCopyBufferDesc(32 * 32 * 4);
    TextureDesc textureDesc = makeSampleTextureDesc();

    ResourceMemoryRequirements bufferRequirements = requireBufferMemoryRequirements(device, bufferDesc);
    ResourceMemoryRequirements textureRequirements = requireTextureMemoryRequirements(device, textureDesc);
    const Size heapSize = std::max(bufferRequirements.size, textureRequirements.size);

    ResourceHeapDesc heapDesc = {};
    heapDesc.memoryType = MemoryType::DeviceLocal;
    heapDesc.usage = ResourceHeapUsage::All;
    heapDesc.size = heapSize;
    ResourceMemoryRequirements requirements[] = {bufferRequirements, textureRequirements};
    heapDesc.requirements = requirements;
    heapDesc.requirementCount = 2;
    ComPtr<IResourceHeap> heap;
    if (SLANG_FAILED(device->createResourceHeap(heapDesc, heap.writeRef())))
        SKIP("ResourceHeapUsage::All is not supported on this device");

    ComPtr<IBuffer> buffer = createPlacedBuffer(device, bufferDesc, heap, 0);
    ComPtr<ITexture> texture = createPlacedTexture(device, textureDesc, heap, 0);

    TextureData textureData;
    textureData.init(device, textureDesc, TextureInitMode::Random, 3);

    auto queue = device->getQueue(QueueType::Graphics);
    {
        const uint32_t expected[] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, buffer);
        REQUIRE_CALL(encoder->uploadBufferData(buffer, 0, sizeof(expected), expected));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, buffer, makeArray<uint32_t>(0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u));
    }
    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(buffer, texture);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        uploadTextureData(device, texture, textureData);
        textureData.checkEqual(texture);
    }
}

GPU_TEST_CASE("resource-heap-alias-texture-no-host-wait", D3D12 | Vulkan | Metal)
{
    TextureData dataA;
    dataA.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 1);
    TextureData dataB;
    dataB.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 2);

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, dataA.desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);

    ComPtr<ITexture> textureA = createPlacedTexture(device, dataA.desc, heap, 0);
    ComPtr<ITexture> textureB = createPlacedTexture(device, dataB.desc, heap, 0);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->aliasResources(nullptr, textureA);
    REQUIRE_CALL(encoder->uploadTextureData(
        textureA,
        {0, dataA.desc.getLayerCount(), 0, dataA.desc.mipCount},
        {0, 0, 0},
        Extent3D::kWholeTexture,
        dataA.subresourceData.data(),
        dataA.subresourceData.size()
    ));
    encoder->aliasResources(textureA, textureB);
    REQUIRE_CALL(encoder->uploadTextureData(
        textureB,
        {0, dataB.desc.getLayerCount(), 0, dataB.desc.mipCount},
        {0, 0, 0},
        Extent3D::kWholeTexture,
        dataB.subresourceData.data(),
        dataB.subresourceData.size()
    ));
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    dataB.checkEqual(textureB);
}

GPU_TEST_CASE("resource-heap-d3d12-depth-shader-views", D3D12)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-texture-bindings", "compareDepth", program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    SamplerDesc samplerDesc = {};
    samplerDesc.reductionOp = TextureReductionOp::Comparison;
    samplerDesc.comparisonFunc = ComparisonFunc::Less;
    auto sampler = device->createSampler(samplerDesc);
    auto results = device->createBuffer(makeComputeBufferDesc(4 * sizeof(float), 4 * sizeof(float)));
    REQUIRE(sampler);
    REQUIRE(results);

    for (Format format : {Format::D16Unorm, Format::D32Float, Format::D32FloatS8Uint})
    {
        CAPTURE(format);
        TextureDesc desc = {};
        desc.size = {4, 4, 1};
        desc.format = format;
        desc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
        const auto requirements = requireTextureMemoryRequirements(device, desc);
        Size size = 0, alignment = 0;
        REQUIRE_CALL(device->getTextureAllocationInfo(desc, &size, &alignment));
        CHECK(size == requirements.size);
        CHECK(alignment == requirements.alignment);
        auto heap = createHeapForRequirements(device, requirements);
        for (bool placed : {false, true})
        {
            CAPTURE(placed);
            auto texture = placed ? createPlacedTexture(device, desc, heap, 0) : device->createTexture(desc);
            REQUIRE(texture);
            auto queue = device->getQueue(QueueType::Graphics);
            auto encoder = queue->createCommandEncoder();
            if (placed)
                encoder->aliasResources(nullptr, texture);
            RenderPassDepthStencilAttachment attachment = {};
            attachment.view = texture->getDefaultView();
            attachment.depthLoadOp = LoadOp::Clear;
            attachment.depthClearValue = 0.5f;
            RenderPassDesc renderPass = {};
            renderPass.depthStencilAttachment = &attachment;
            encoder->beginRenderPass(renderPass)->end();
            auto pass = encoder->beginComputePass();
            ShaderCursor cursor(pass->bindPipeline(pipeline));
            REQUIRE_CALL(cursor["depthInput"].setBinding(texture));
            REQUIRE_CALL(cursor["comparisonSampler"].setBinding(sampler));
            REQUIRE_CALL(cursor["results"].setBinding(results));
            pass->dispatchCompute(1, 1, 1);
            pass->end();
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
            compareComputeResult(device, results, makeArray<float>(1.f, 0.f, 0.f, 0.f));
        }
    }
}

GPU_TEST_CASE("resource-heap-d3d12-depth-format-uav", D3D12)
{
    // D32Float may back an R32_FLOAT UAV when DepthStencil usage is absent.
    const char* source = R"(
        RWTexture2D<float> output;
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void main() { output[uint2(0)] = 0.25; }
    )";
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadComputeProgramFromSource(device, source, program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    TextureDesc desc = {};
    desc.format = Format::D32Float;
    desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
    auto requirements = requireTextureMemoryRequirements(device, desc);
    auto heap = createHeapForRequirements(device, requirements);
    for (bool placed : {false, true})
    {
        CAPTURE(placed);
        auto texture = placed ? createPlacedTexture(device, desc, heap, 0) : device->createTexture(desc);
        REQUIRE(texture);
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        if (placed)
            encoder->aliasResources(nullptr, texture);
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["output"].setBinding(texture));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, texture, 0, 0, makeArray<float>(0.25f));
    }

    // Adding DepthStencil would request incompatible native DS/UAV flags.
    desc.usage |= TextureUsage::DepthStencil;
    ResourceMemoryRequirements invalidRequirements;
    CHECK_EQ(device->getTextureMemoryRequirements(desc, &invalidRequirements), SLANG_E_INVALID_ARG);
    Size size = 0, alignment = 0;
    CHECK_EQ(device->getTextureAllocationInfo(desc, &size, &alignment), SLANG_E_INVALID_ARG);
    ComPtr<ITexture> texture;
    CHECK_EQ(device->createTexture(desc, nullptr, texture.writeRef()), SLANG_E_INVALID_ARG);
    desc.heap = heap;
    CHECK_EQ(device->createTexture(desc, nullptr, texture.writeRef()), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-d3d12-alias-multiple-predecessors", D3D12)
{
    for (bool depth : {false, true})
    {
        CAPTURE(depth);
        TextureDesc desc = {};
        desc.size = {256, 256, 1};
        desc.format = depth ? Format::D32Float : Format::RGBA8Unorm;
        desc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource |
                     (depth ? TextureUsage::DepthStencil : TextureUsage::RenderTarget);
        desc.defaultState = ResourceState::ShaderResource;
        auto smallRequirements = requireTextureMemoryRequirements(device, desc);
        const Offset secondOffset = alignUp(smallRequirements.size, smallRequirements.alignment);
        TextureDesc largeDesc = desc;
        largeDesc.size.height *= 2;
        auto largeRequirements = requireTextureMemoryRequirements(device, largeDesc);
        REQUIRE_GT(largeRequirements.size, secondOffset);

        ResourceMemoryRequirements requirements[] = {smallRequirements, largeRequirements};
        ResourceHeapDesc heapDesc = {};
        heapDesc.size = std::max(secondOffset + smallRequirements.size, largeRequirements.size);
        heapDesc.requirements = requirements;
        heapDesc.requirementCount = SLANG_COUNT_OF(requirements);
        auto heap = device->createResourceHeap(heapDesc);
        REQUIRE(heap);
        auto first = createPlacedTexture(device, desc, heap, 0);
        auto second = createPlacedTexture(device, desc, heap, secondOffset);
        auto destination = createPlacedTexture(device, largeDesc, heap, 0);

        auto queue = device->getQueue(QueueType::Graphics);
        for (uint32_t submission = 0; submission < 2; ++submission)
        {
            auto encoder = queue->createCommandEncoder();
            auto clear = [&](ITexture* texture, float value)
            {
                RenderPassColorAttachment colorAttachment = {};
                RenderPassDepthStencilAttachment depthAttachment = {};
                RenderPassDesc passDesc = {};
                if (depth)
                {
                    depthAttachment.view = texture->getDefaultView();
                    depthAttachment.depthClearValue = value;
                    passDesc.depthStencilAttachment = &depthAttachment;
                }
                else
                {
                    colorAttachment.view = texture->getDefaultView();
                    std::fill_n(colorAttachment.clearValue, 4, value);
                    passDesc.colorAttachments = &colorAttachment;
                    passDesc.colorAttachmentCount = 1;
                }
                encoder->beginRenderPass(passDesc)->end();
            };
            for (uint32_t cycle = 0; cycle < 2; ++cycle)
            {
                ITexture* previous = submission == 0 && cycle == 0 ? nullptr : destination.get();
                IResource* partitions[] = {first, second};
                IResource* merged[] = {destination};
                if (cycle == 0)
                {
                    encoder->aliasResources(previous, first);
                    encoder->aliasResources(previous, second);
                }
                else
                {
                    encoder->aliasResources(1, merged, 2, partitions);
                }
                clear(first, 0.25f);
                clear(second, 0.5f);
                if (cycle == 0)
                {
                    encoder->aliasResources(first, destination);
                    // Retain coverage for consecutive convenience calls to the same destination.
                    encoder->aliasResources(second, destination);
                }
                else
                {
                    encoder->aliasResources(2, partitions, 1, merged);
                }
                clear(destination, float(submission));
            }
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());

            ComPtr<ISlangBlob> data;
            SubresourceLayout layout;
            REQUIRE_CALL(device->readTexture(destination, 0, 0, data.writeRef(), &layout));
            for (uint32_t y = 0; y < largeDesc.size.height; ++y)
            {
                auto row = static_cast<const uint8_t*>(data->getBufferPointer()) + y * layout.rowPitch;
                for (uint32_t x = 0; x < largeDesc.size.width; ++x)
                {
                    if (depth)
                        CHECK_EQ(reinterpret_cast<const float*>(row)[x], float(submission));
                    else
                        for (uint32_t c = 0; c < 4; ++c)
                            CHECK_EQ(row[x * 4 + c], uint8_t(submission * 255));
                }
            }
        }
    }
}

GPU_TEST_CASE("resource-heap-d3d12-alias-render-dont-care", D3D12)
{
    const char* source = R"(
        uniform float4 color;
        [shader("vertex")]
        float4 vertexMain(uint id : SV_VertexID) : SV_Position
        {
            float2 uv = float2((id << 1) & 2, id & 2);
            return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
        }
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return color; }
    )";
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadRenderProgramFromSource(device, source, "vertexMain", "fragmentMain", program.writeRef()));

    TextureDesc colorDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    // Exercise activation from a default state different from the discard state.
    colorDesc.defaultState = ResourceState::ShaderResource;
    TextureDesc depthDesc = colorDesc;
    depthDesc.format = Format::D32Float;
    depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::CopySource;
    depthDesc.defaultState = ResourceState::CopySource;
    auto colorHeap = createHeapForRequirements(device, requireTextureMemoryRequirements(device, colorDesc));
    auto depthHeap = createHeapForRequirements(device, requireTextureMemoryRequirements(device, depthDesc));
    ComPtr<ITexture> colors[2];
    ComPtr<ITexture> depths[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        colors[i] = createPlacedTexture(device, colorDesc, colorHeap, 0);
        depths[i] = createPlacedTexture(device, depthDesc, depthHeap, 0);
    }

    ColorTargetDesc target = {};
    target.format = colorDesc.format;
    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    pipelineDesc.targets = &target;
    pipelineDesc.targetCount = 1;
    pipelineDesc.depthStencil.format = depthDesc.format;
    pipelineDesc.depthStencil.depthTestEnable = true;
    pipelineDesc.depthStencil.depthWriteEnable = true;
    pipelineDesc.depthStencil.depthFunc = ComparisonFunc::Always;
    ComPtr<IRenderPipeline> pipeline;
    REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t submission = 0; submission < 2; ++submission)
    {
        auto encoder = queue->createCommandEncoder();
        for (uint32_t i = 0; i < 2; ++i)
        {
            const bool first = submission == 0 && i == 0;
            encoder->aliasResources(first ? nullptr : colors[1 - i].get(), colors[i]);
            encoder->aliasResources(first ? nullptr : depths[1 - i].get(), depths[i]);
            RenderPassColorAttachment color = {};
            color.view = colors[i]->getDefaultView();
            color.loadOp = LoadOp::DontCare;
            RenderPassDepthStencilAttachment depth = {};
            depth.view = depths[i]->getDefaultView();
            depth.depthLoadOp = LoadOp::DontCare;
            depth.stencilLoadOp = LoadOp::DontCare;
            RenderPassDesc passDesc = {};
            passDesc.colorAttachments = &color;
            passDesc.colorAttachmentCount = 1;
            passDesc.depthStencilAttachment = &depth;
            auto pass = encoder->beginRenderPass(passDesc);
            ShaderCursor cursor(pass->bindPipeline(pipeline));
            const float value[] = {float(i), float(submission), 0.f, 1.f};
            REQUIRE_CALL(cursor["color"].setData(value, sizeof(value)));
            RenderState state = {};
            state.viewports[0] = Viewport::fromSize(32, 32);
            state.viewportCount = 1;
            state.scissorRects[0] = ScissorRect::fromSize(32, 32);
            state.scissorRectCount = 1;
            pass->setRenderState(state);
            DrawArguments args = {};
            args.vertexCount = 3;
            pass->draw(args);
            pass->end();
        }
        REQUIRE_CALL(queue->submit(encoder->finish()));
    }
    REQUIRE_CALL(queue->waitOnHost());
    for (bool depth : {false, true})
    {
        ComPtr<ISlangBlob> data;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(depth ? depths[1] : colors[1], 0, 0, data.writeRef(), &layout));
        for (uint32_t y = 0; y < 32; ++y)
        {
            const auto row = static_cast<const uint8_t*>(data->getBufferPointer()) + y * layout.rowPitch;
            for (uint32_t x = 0; x < 32; ++x)
            {
                if (depth)
                    CHECK(reinterpret_cast<const float*>(row)[x] == 0.5f);
                else
                {
                    CHECK(row[x * 4 + 0] == 255);
                    CHECK(row[x * 4 + 1] == 255);
                    CHECK(row[x * 4 + 2] == 0);
                    CHECK(row[x * 4 + 3] == 255);
                }
            }
        }
    }
}

GPU_TEST_CASE("resource-heap-placed-texture-create-preserves-alias", D3D12 | Vulkan | Metal)
{
    TextureData dataA;
    dataA.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 1);

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, dataA.desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);

    ComPtr<ITexture> textureA = createPlacedTexture(device, dataA.desc, heap, 0);
    auto queue = device->getQueue(QueueType::Graphics);
    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, textureA);
        REQUIRE_CALL(encoder->uploadTextureData(
            textureA,
            {0, dataA.desc.getLayerCount(), 0, dataA.desc.mipCount},
            {0, 0, 0},
            Extent3D::kWholeTexture,
            dataA.subresourceData.data(),
            dataA.subresourceData.size()
        ));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    }
    dataA.checkEqual(textureA);

    ComPtr<ITexture> textureB = createPlacedTexture(device, dataA.desc, heap, 0);
    dataA.checkEqual(textureA);
}

GPU_TEST_CASE("resource-heap-explicit-alignment", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Size requestedAlignment = std::max(requirements.heapAlignment * 2, Size(64) * 1024);

    ResourceHeapDesc heapDesc = {};
    heapDesc.memoryType = requirements.memoryType;
    heapDesc.size = std::max(requirements.size, requestedAlignment);
    heapDesc.alignment = requestedAlignment;
    heapDesc.requirements = &requirements;
    heapDesc.requirementCount = 1;
    heapDesc.label = "explicit-alignment-heap";

    ComPtr<IResourceHeap> heap;
    const Result result = device->createResourceHeap(heapDesc, heap.writeRef());
    switch (device->getDeviceType())
    {
    case DeviceType::Vulkan:
    case DeviceType::Metal:
        CHECK_EQ(result, SLANG_E_INVALID_ARG);
        return;
    default:
        REQUIRE_EQ(result, SLANG_OK);
        break;
    }

    CHECK_GE(heap->getDesc().alignment, requestedAlignment);
    NativeHandle handle = {};
    REQUIRE_CALL(heap->getNativeHandle(&handle));
    if (handle.type == NativeHandleType::CUdeviceptr)
        CHECK_EQ(handle.value % requestedAlignment, 0);
    CHECK(createPlacedBuffer(device, desc, heap, 0));
}

static ComPtr<IDevice> createLifetimeTestDevice(GpuTestContext* ctx, bool enableValidation)
{
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = ctx->deviceType;
    deviceDesc.adapter = getSelectedDeviceAdapter(ctx->deviceType);
    deviceDesc.enableValidation = enableValidation;
    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    ComPtr<IDevice> testDevice;
    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, testDevice.writeRef()));
    return testDevice;
}

// Heaps and devices are not Resources, so Debug builds also compare RefObject counts.
struct LiveObjectCounts
{
    uint64_t resources = gResourceCount.load();
#if SLANG_RHI_DEBUG
    uint64_t objects = RefObject::getObjectCount();
#endif

    void checkReleased() const
    {
        CHECK_EQ(gResourceCount.load(), resources);
#if SLANG_RHI_DEBUG
        CHECK_EQ(RefObject::getObjectCount(), objects);
#endif
    }
};

static LiveObjectCounts countLiveObjectsAfterWarmup(GpuTestContext* ctx, bool enableValidation)
{
    ComPtr<IDevice> warmup = createLifetimeTestDevice(ctx, enableValidation);
    warmup.setNull();
    return LiveObjectCounts();
}

static void checkPlacedResourcesReleasedWithPendingWork(GpuTestContext* ctx, bool enableValidation, bool waitFirst)
{
    const LiveObjectCounts before = countLiveObjectsAfterWarmup(ctx, enableValidation);
    {
        ComPtr<IDevice> testDevice = createLifetimeTestDevice(ctx, enableValidation);
        BufferDesc desc = makeCopyBufferDesc(256);
        ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(testDevice, desc);
        ComPtr<IResourceHeap> heap = createHeapForRequirements(testDevice, requirements);
        ComPtr<IBuffer> bufferA = createPlacedBuffer(testDevice, desc, heap, 0);
        ComPtr<IBuffer> bufferB = createPlacedBuffer(testDevice, desc, heap, 0);

        const uint32_t dataA = 0x12345678;
        const uint32_t dataB = 0x9abcdef0;
        ComPtr<ICommandQueue> queue = testDevice->getQueue(QueueType::Graphics);
        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, bufferA);
        REQUIRE_CALL(encoder->uploadBufferData(bufferA, 0, sizeof(dataA), &dataA));
        encoder->aliasResources(bufferA, bufferB);
        REQUIRE_CALL(encoder->uploadBufferData(bufferB, 0, sizeof(dataB), &dataB));
        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        REQUIRE(commandBuffer);
        encoder.setNull();
        REQUIRE_CALL(queue->submit(commandBuffer));
        commandBuffer.setNull();

        bufferA.setNull();
        bufferB.setNull();
        heap.setNull();
        if (waitFirst)
        {
            REQUIRE_CALL(queue->waitOnHost());
            queue.setNull();
            testDevice.setNull();
        }
        else
        {
            testDevice.setNull();
            queue.setNull();
        }
    }
    before.checkReleased();
}

GPU_TEST_CASE("resource-heap-deferred-delete-pending-work", D3D12 | Vulkan | Metal | CUDA | DontCreateDevice)
{
    for (bool enableValidation : {false, true})
    {
        CAPTURE(enableValidation);
        SUBCASE("wait-before-release")
        {
            checkPlacedResourcesReleasedWithPendingWork(ctx, enableValidation, true);
        }
        SUBCASE("release-without-wait")
        {
            checkPlacedResourcesReleasedWithPendingWork(ctx, enableValidation, false);
        }
    }
}

static ComPtr<ITextureView> createPlacedTextureView(
    IDevice* device,
    bool defaultView,
    ComPtr<IResourceHeap>& outHeap,
    ComPtr<ITexture>& outTexture
)
{
    TextureDesc desc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, desc);
    outHeap = createHeapForRequirements(device, requirements);
    outTexture = createPlacedTexture(device, desc, outHeap, 0);
    ComPtr<ITextureView> view = defaultView ? outTexture->getDefaultView() : outTexture->createView({});
    REQUIRE(view);
    return view;
}

GPU_TEST_CASE("resource-heap-placed-texture-view-lifetime", D3D12 | Vulkan | Metal | DontCreateDevice)
{
    for (bool enableValidation : {false, true})
    {
        CAPTURE(enableValidation);
        const LiveObjectCounts before = countLiveObjectsAfterWarmup(ctx, enableValidation);
        for (bool defaultView : {false, true})
        {
            CAPTURE(defaultView);

            {
                ComPtr<IDevice> testDevice = createLifetimeTestDevice(ctx, enableValidation);
                ComPtr<IResourceHeap> heap;
                ComPtr<ITexture> texture;
                ComPtr<ITextureView> view = createPlacedTextureView(testDevice, defaultView, heap, texture);
                const TextureDesc desc = texture->getDesc();
                texture.setNull();
                heap.setNull();
                testDevice.setNull();

                ComPtr<ITexture> recovered(view->getTexture());
                REQUIRE(recovered);
                CHECK_EQ(recovered->getDesc().format, desc.format);
                CHECK_EQ(recovered->getDesc().size.width, desc.size.width);
                if (defaultView)
                    CHECK_EQ(recovered->getDefaultView().get(), view.get());
                view.setNull();
                recovered.setNull();
            }
            before.checkReleased();

            // Release the device and caller-owned resources while the render submission is pending.
            {
                ComPtr<IDevice> testDevice = createLifetimeTestDevice(ctx, enableValidation);
                ComPtr<IResourceHeap> heap;
                ComPtr<ITexture> texture;
                ComPtr<ITextureView> view = createPlacedTextureView(testDevice, defaultView, heap, texture);

                ComPtr<ICommandQueue> queue = testDevice->getQueue(QueueType::Graphics);
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                encoder->aliasResources(nullptr, texture);
                RenderPassColorAttachment colorAttachment = {};
                colorAttachment.view = view;
                colorAttachment.loadOp = LoadOp::Clear;
                colorAttachment.storeOp = StoreOp::Store;
                RenderPassDesc renderPassDesc = {};
                renderPassDesc.colorAttachments = &colorAttachment;
                renderPassDesc.colorAttachmentCount = 1;
                IRenderPassEncoder* passEncoder = encoder->beginRenderPass(renderPassDesc);
                REQUIRE(passEncoder);
                passEncoder->end();
                ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
                REQUIRE(commandBuffer);
                encoder.setNull();
                REQUIRE_CALL(queue->submit(commandBuffer));

                testDevice.setNull();
                view.setNull();
                texture.setNull();
                heap.setNull();
                commandBuffer.setNull();
                queue.setNull();
            }
            before.checkReleased();
        }
    }
}

// Bind the placed buffer only to the shader: copy, upload and alias commands would retain it.
GPU_TEST_CASE("resource-heap-shader-binding-retains-placed-buffer", D3D12 | Vulkan | Metal | CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-buffer-copy", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<uint32_t> data(32);
    for (uint32_t i = 0; i < data.size(); ++i)
        data[i] = 0xc0de0000u + i;
    const BufferDesc desc = makeComputeBufferDesc(data.size() * sizeof(uint32_t), sizeof(uint32_t));
    ComPtr<IBuffer> output;
    REQUIRE_CALL(device->createBuffer(desc, nullptr, output.writeRef()));

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    // Drain deferred deletions from earlier tests.
    REQUIRE_CALL(queue->waitOnHost());

    ComPtr<ICommandBuffer> commandBuffer;
    uint64_t resourceCountWhileRecorded = 0;
    {
        ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        ComPtr<IBuffer> input = createPlacedBuffer(device, desc, heap, 0, data.data());

        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        IComputePassEncoder* passEncoder = encoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        REQUIRE_CALL(cursor["src"].setBinding(input));
        REQUIRE_CALL(cursor["dst"].setBinding(output));
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        REQUIRE_CALL(encoder->finish(commandBuffer.writeRef()));
        resourceCountWhileRecorded = gResourceCount.load();
    }

    // Drain deferred deletion before submitting; only the command buffer still retains the placed buffer.
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(gResourceCount.load(), resourceCountWhileRecorded);

    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());
    commandBuffer.setNull();

    compareComputeResult(device, output, std::span<uint32_t>(data));
}
