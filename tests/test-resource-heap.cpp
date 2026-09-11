#include "testing.h"
#include "texture-test.h"

#if SLANG_RHI_DEBUG
#include "debug-layer/debug-device.h"
#endif

#include <algorithm>
#include <cstring>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

class CaptureValidationCallback : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        SLANG_UNUSED(type);
        SLANG_UNUSED(source);
        output += message;
        output += '\n';
    }

    void clear() { output.clear(); }
    bool contains(const char* text) const { return output.find(text) != std::string::npos; }

    std::string output;
};

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
    ResourcePlacementDesc placement = {};
    placement.heap = heap;
    placement.offset = offset;
    desc.next = &placement;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, initData, buffer.writeRef()));
    CHECK(buffer->getDesc().next == nullptr);
    return buffer;
}

static Result tryCreatePlacedBuffer(IDevice* device, BufferDesc desc, IResourceHeap* heap, Offset offset)
{
    ResourcePlacementDesc placement = {};
    placement.heap = heap;
    placement.offset = offset;
    desc.next = &placement;
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
    ResourcePlacementDesc placement = {};
    placement.heap = heap;
    placement.offset = offset;
    desc.next = &placement;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, initData, texture.writeRef()));
    CHECK(texture->getDesc().next == nullptr);
    return texture;
}

static Result tryCreatePlacedTexture(IDevice* device, TextureDesc desc, IResourceHeap* heap, Offset offset)
{
    ResourcePlacementDesc placement = {};
    placement.heap = heap;
    placement.offset = offset;
    desc.next = &placement;
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

TEST_CASE("resource-heap-usage-helpers")
{
    BufferDesc bufferDesc = {};
    CHECK_EQ(getResourceHeapUsage(bufferDesc), ResourceHeapUsage::Buffers);

    TextureDesc textureDesc = {};
    textureDesc.usage = TextureUsage::ShaderResource;
    CHECK_EQ(getResourceHeapUsage(textureDesc), ResourceHeapUsage::NonRtDsTextures);

    textureDesc.usage = TextureUsage::UnorderedAccess;
    CHECK_EQ(getResourceHeapUsage(textureDesc), ResourceHeapUsage::NonRtDsTextures);

    textureDesc.usage = TextureUsage::RenderTarget;
    CHECK_EQ(getResourceHeapUsage(textureDesc), ResourceHeapUsage::RtDsTextures);

    textureDesc.usage = TextureUsage::DepthStencil;
    CHECK_EQ(getResourceHeapUsage(textureDesc), ResourceHeapUsage::RtDsTextures);

    textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::RenderTarget;
    CHECK_EQ(getResourceHeapUsage(textureDesc), ResourceHeapUsage::RtDsTextures);

    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::All, ResourceHeapUsage::Buffers));
    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::All, ResourceHeapUsage::NonRtDsTextures));
    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::All, ResourceHeapUsage::RtDsTextures));
    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::Buffers, ResourceHeapUsage::Buffers));
    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::NonRtDsTextures, ResourceHeapUsage::NonRtDsTextures));
    CHECK(isResourceHeapUsageCompatible(ResourceHeapUsage::RtDsTextures, ResourceHeapUsage::RtDsTextures));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::Buffers, ResourceHeapUsage::NonRtDsTextures));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::Buffers, ResourceHeapUsage::RtDsTextures));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::NonRtDsTextures, ResourceHeapUsage::Buffers));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::NonRtDsTextures, ResourceHeapUsage::RtDsTextures));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::RtDsTextures, ResourceHeapUsage::Buffers));
    CHECK(!isResourceHeapUsageCompatible(ResourceHeapUsage::RtDsTextures, ResourceHeapUsage::NonRtDsTextures));
}

GPU_TEST_CASE("resource-heap-feature", D3D12 | Vulkan | Metal | CUDA)
{
    CHECK(device->hasFeature(Feature::ResourceHeaps));
    CHECK(device->hasFeature(Feature::ResourceAliasing));
}

GPU_TEST_CASE("resource-heap-unsupported", D3D11 | CPU | WGPU)
{
    CHECK(!device->hasFeature(Feature::ResourceHeaps));
    CHECK(!device->hasFeature(Feature::ResourceAliasing));

    BufferDesc bufferDesc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements bufferRequirements = {};
    CHECK_EQ(device->getBufferMemoryRequirements(bufferDesc, &bufferRequirements), SLANG_E_NOT_AVAILABLE);

    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements textureRequirements = {};
    CHECK_EQ(device->getTextureMemoryRequirements(textureDesc, &textureRequirements), SLANG_E_NOT_AVAILABLE);
}

GPU_TEST_CASE("resource-heap-buffer-requirements", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeComputeBufferDesc(64 * 1024, sizeof(uint32_t));

    uint32_t outputExtensionSentinel = 0x12345678;
    ResourceMemoryRequirements requirements = {};
    requirements.next = &outputExtensionSentinel;
    REQUIRE_CALL(device->getBufferMemoryRequirements(desc, &requirements));
    CHECK_EQ(requirements.structType, ResourceMemoryRequirements::kStructType);
    CHECK(requirements.next == &outputExtensionSentinel);
    CHECK_GE(requirements.size, desc.size);
    CHECK_GT(requirements.alignment, 0);
    CHECK_EQ(requirements.alignment & (requirements.alignment - 1), 0);
    CHECK_EQ(requirements.memoryType, desc.memoryType);
    CHECK_EQ(requirements.usage, ResourceHeapUsage::Buffers);
    CHECK_EQ(
        requirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
        ResourceMemoryRequirementFlags::None
    );
    CHECK_GT(requirements.heapAlignment, 0);

    BufferDesc uploadDesc = makeCopyBufferDesc(4096, MemoryType::Upload);
    ResourceMemoryRequirements uploadRequirements = requireBufferMemoryRequirements(device, uploadDesc);
    CHECK_EQ(uploadRequirements.memoryType, MemoryType::Upload);
    CHECK_EQ(uploadRequirements.usage, ResourceHeapUsage::Buffers);

    BufferDesc sharedDesc = makeCopyBufferDesc(4096);
    sharedDesc.usage |= BufferUsage::Shared;
    ResourceMemoryRequirements sharedRequirements = requireBufferMemoryRequirements(device, sharedDesc);
    CHECK_NE(
        sharedRequirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
        ResourceMemoryRequirementFlags::None
    );
}

GPU_TEST_CASE("resource-heap-texture-requirements", D3D12 | Vulkan | Metal)
{
    TextureDesc sampledDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements sampledRequirements = requireTextureMemoryRequirements(device, sampledDesc);
    CHECK_GE(sampledRequirements.size, 32 * 32 * 4);
    CHECK_GT(sampledRequirements.alignment, 0);
    CHECK_EQ(sampledRequirements.alignment & (sampledRequirements.alignment - 1), 0);
    CHECK_EQ(sampledRequirements.memoryType, MemoryType::DeviceLocal);
    CHECK_EQ(sampledRequirements.usage, ResourceHeapUsage::NonRtDsTextures);

    TextureDesc rtDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements rtRequirements = requireTextureMemoryRequirements(device, rtDesc);
    CHECK_EQ(rtRequirements.usage, ResourceHeapUsage::RtDsTextures);

    TextureDesc dsDesc = {};
    dsDesc.type = TextureType::Texture2D;
    dsDesc.size = {32, 32, 1};
    dsDesc.format = Format::D32Float;
    dsDesc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
    dsDesc.memoryType = MemoryType::DeviceLocal;
    ResourceMemoryRequirements dsRequirements = requireTextureMemoryRequirements(device, dsDesc);
    CHECK_EQ(dsRequirements.usage, ResourceHeapUsage::RtDsTextures);

    CHECK_GT(sampledRequirements.heapAlignment, 0);
    CHECK_EQ(
        sampledRequirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
        ResourceMemoryRequirementFlags::None
    );
}

GPU_TEST_CASE("resource-heap-create", D3D12 | Vulkan | Metal | CUDA)
{
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
}

GPU_TEST_CASE("resource-heap-compatibility-query", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(4096);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);

    bool compatible = false;
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, requirements, &compatible));
    CHECK(compatible);

    ResourceMemoryRequirements incompatible = requirements;
    incompatible.memoryType = MemoryType::Upload;
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, incompatible, &compatible));
    CHECK(!compatible);

    incompatible = requirements;
    incompatible.compatibility.data[1] ^= 1;
    REQUIRE_CALL(device->isResourceHeapCompatible(heap, incompatible, &compatible));
    CHECK(!compatible);
}

GPU_TEST_CASE("resource-heap-create-all", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc bufferDesc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, bufferDesc);

    ResourceHeapDesc desc = {};
    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = ResourceHeapUsage::All;
    desc.size = 256 * 1024;
    desc.requirements = &requirements;
    desc.requirementCount = 1;
    ComPtr<IResourceHeap> heap;
    Result result = device->createResourceHeap(desc, heap.writeRef());
    if (SLANG_FAILED(result))
        SKIP("ResourceHeapUsage::All is not supported on this device");

    CHECK_EQ(heap->getDesc().usage, ResourceHeapUsage::All);

    ComPtr<IBuffer> buffer = createPlacedBuffer(device, bufferDesc, heap, 0);
    CHECK_EQ(buffer->getDesc().size, bufferDesc.size);
}

GPU_TEST_CASE("resource-heap-create-texture-kinds", D3D12 | Vulkan | Metal)
{
    ComPtr<IResourceHeap> nonRtHeap = createHeap(device, 256 * 1024, ResourceHeapUsage::NonRtDsTextures);
    CHECK_EQ(nonRtHeap->getDesc().usage, ResourceHeapUsage::NonRtDsTextures);

    ComPtr<IResourceHeap> rtHeap = createHeap(device, 256 * 1024, ResourceHeapUsage::RtDsTextures);
    CHECK_EQ(rtHeap->getDesc().usage, ResourceHeapUsage::RtDsTextures);
}

GPU_TEST_CASE("resource-heap-cuda-texture-unsupported", CUDA)
{
    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements requirements = {};
    CHECK_EQ(device->getTextureMemoryRequirements(textureDesc, &requirements), SLANG_E_NOT_AVAILABLE);

    ComPtr<IResourceHeap> bufferHeap = createHeap(device, 64 * 1024, ResourceHeapUsage::Buffers);
    CHECK_EQ(tryCreatePlacedTexture(device, textureDesc, bufferHeap, 0), SLANG_E_NOT_AVAILABLE);
}

GPU_TEST_CASE("resource-heap-place-buffer-init-data", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 64;
    std::vector<uint32_t> initData(elementCount);
    for (uint32_t i = 0; i < elementCount; ++i)
        initData[i] = 0x1000u + i;

    BufferDesc desc = makeCopyBufferDesc(elementCount * sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, 0, initData.data());

    CHECK_EQ(buffer->getDesc().size, desc.size);
    CHECK_EQ(buffer->getDesc().memoryType, MemoryType::DeviceLocal);
    compareComputeResult(device, buffer, std::span<uint32_t>(initData));
}

GPU_TEST_CASE("resource-heap-d3d12-placed-buffer-init-states", D3D12)
{
    const uint32_t expected[] = {11, 22, 33, 44};

    BufferDesc copySourceDesc = makeCopyBufferDesc(sizeof(expected));
    copySourceDesc.defaultState = ResourceState::CopySource;
    ResourceMemoryRequirements copySourceRequirements = requireBufferMemoryRequirements(device, copySourceDesc);
    ComPtr<IResourceHeap> copySourceHeap = createHeapForRequirements(device, copySourceRequirements);
    ComPtr<IBuffer> copySourceBuffer = createPlacedBuffer(device, copySourceDesc, copySourceHeap, 0, expected);
    compareComputeResult(device, copySourceBuffer, makeArray<uint32_t>(11, 22, 33, 44));

    BufferDesc uavDesc = makeComputeBufferDesc(sizeof(expected), sizeof(uint32_t));
    ResourceMemoryRequirements uavRequirements = requireBufferMemoryRequirements(device, uavDesc);
    ComPtr<IResourceHeap> uavHeap = createHeapForRequirements(device, uavRequirements);
    ComPtr<IBuffer> uavBuffer = createPlacedBuffer(device, uavDesc, uavHeap, 0, expected);
    compareComputeResult(device, uavBuffer, makeArray<uint32_t>(11, 22, 33, 44));
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

GPU_TEST_CASE("resource-heap-place-and-alias-buffers", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 1024;
    const Size bufferSize = elementCount * sizeof(uint32_t);

    BufferDesc desc = makeComputeBufferDesc(bufferSize, sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);

    ComPtr<IBuffer> bufferA = createPlacedBuffer(device, desc, heap, 0);
    ComPtr<IBuffer> bufferB = createPlacedBuffer(device, desc, heap, 0);

    std::vector<uint32_t> dataA(elementCount, 0xA11A5u);
    std::vector<uint32_t> dataB(elementCount, 0xB22B5u);

    auto queue = device->getQueue(QueueType::Graphics);
    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, bufferA);
        REQUIRE_CALL(encoder->uploadBufferData(bufferA, 0, bufferSize, dataA.data()));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, bufferA, std::span<uint32_t>(dataA));
    }
    {
        auto encoder = queue->createCommandEncoder();
        encoder->setBufferState(bufferA, ResourceState::CopySource);
        encoder->aliasResources(bufferA, bufferB);
        REQUIRE_CALL(encoder->uploadBufferData(bufferB, 0, bufferSize, dataB.data()));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, bufferB, std::span<uint32_t>(dataB));
    }
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

GPU_TEST_CASE("resource-heap-invalid-alias-validation", D3D12 | Vulkan)
{
#if SLANG_RHI_DEBUG
    static CaptureValidationCallback callback;
    callback.clear();
    ComPtr<IDevice> firstDevice = device;
    ComPtr<IDevice> secondDevice = createTestingDevice(ctx, ctx->deviceType, false);

    BufferDesc desc = makeCopyBufferDesc(4096);
    ResourceMemoryRequirements firstRequirements = requireBufferMemoryRequirements(firstDevice, desc);
    ResourceMemoryRequirements secondRequirements = requireBufferMemoryRequirements(secondDevice, desc);
    const Offset secondOffset = alignUp(firstRequirements.size, firstRequirements.alignment);

    ComPtr<IResourceHeap> firstHeap =
        createHeapForRequirements(firstDevice, firstRequirements, secondOffset + firstRequirements.size);
    ComPtr<IResourceHeap> otherHeap = createHeapForRequirements(firstDevice, firstRequirements);
    ComPtr<IResourceHeap> secondDeviceHeap = createHeapForRequirements(secondDevice, secondRequirements);

    ComPtr<IBuffer> first = createPlacedBuffer(firstDevice, desc, firstHeap, 0);
    ComPtr<IBuffer> differentHeap = createPlacedBuffer(firstDevice, desc, otherHeap, 0);
    ComPtr<IBuffer> nonOverlapping = createPlacedBuffer(firstDevice, desc, firstHeap, secondOffset);
    ComPtr<IBuffer> differentDevice = createPlacedBuffer(secondDevice, desc, secondDeviceHeap, 0);
    ComPtr<IBuffer> committed;
    REQUIRE_CALL(firstDevice->createBuffer(desc, nullptr, committed.writeRef()));

    auto firstQueue = firstDevice->getQueue(QueueType::Graphics);
    auto secondQueue = secondDevice->getQueue(QueueType::Graphics);
    auto encoder = firstQueue->createCommandEncoder();
    auto debugDevice = checked_cast<debug::DebugDevice*>(firstDevice.get());
    IDebugCallback* previousCallback = debugDevice->ctx->debugCallback;
    debugDevice->ctx->debugCallback = &callback;

    callback.clear();
    encoder->aliasResources(differentDevice, first);
    CHECK(callback.contains("same device"));

    callback.clear();
    encoder->aliasResources(differentHeap, first);
    CHECK(callback.contains("same resource heap"));

    callback.clear();
    encoder->aliasResources(committed, first);
    CHECK(callback.contains("must be a placed buffer or texture"));

    callback.clear();
    encoder->aliasResources(nonOverlapping, first);
    CHECK(callback.contains("placement ranges must overlap"));

    ComPtr<ICommandBuffer> commandBuffer = encoder->finish();

    debugDevice->ctx->debugCallback = previousCallback;

    encoder = nullptr;
    committed = nullptr;
    differentDevice = nullptr;
    nonOverlapping = nullptr;
    differentHeap = nullptr;
    first = nullptr;

    REQUIRE_CALL(firstQueue->submit(commandBuffer));
    REQUIRE_CALL(firstQueue->waitOnHost());
    auto cleanupEncoder = secondQueue->createCommandEncoder();
    REQUIRE_CALL(secondQueue->submit(cleanupEncoder->finish()));
    REQUIRE_CALL(secondQueue->waitOnHost());

    commandBuffer = nullptr;
    secondDeviceHeap = nullptr;
    otherHeap = nullptr;
    firstHeap = nullptr;
    secondDevice = nullptr;
    firstDevice = nullptr;
#else
    SLANG_UNUSED(ctx);
    SLANG_UNUSED(device);
    SKIP("Debug-layer validation requires a debug build");
#endif
}

GPU_TEST_CASE("resource-heap-place-sequential-buffers", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 64;
    BufferDesc desc = makeCopyBufferDesc(elementCount * sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    const Offset offsetB = alignUp(requirements.size, requirements.alignment);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, offsetB + requirements.size);

    std::vector<uint32_t> dataA(elementCount, 0x11111111u);
    std::vector<uint32_t> dataB(elementCount, 0x22222222u);
    ComPtr<IBuffer> bufferA = createPlacedBuffer(device, desc, heap, 0, dataA.data());
    ComPtr<IBuffer> bufferB = createPlacedBuffer(device, desc, heap, offsetB, dataB.data());

    compareComputeResult(device, bufferA, std::span<uint32_t>(dataA));
    compareComputeResult(device, bufferB, std::span<uint32_t>(dataB));
}

GPU_TEST_CASE("resource-heap-place-offset", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {7, 8, 9, 10};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offset = requirements.alignment;
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, offset + requirements.size);

    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, offset, expected);
    compareComputeResult(device, buffer, makeArray<uint32_t>(7, 8, 9, 10));
}

GPU_TEST_CASE("resource-heap-place-upload", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {0xC0FFEEu, 0xF00Du, 0xBEEFu, 0xA5A5A5A5u};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, 0, "upload-resource-heap");

    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, 0, expected);
    CHECK_EQ(buffer->getDesc().memoryType, MemoryType::Upload);
    compareComputeResult(device, buffer, makeArray<uint32_t>(0xC0FFEEu, 0xF00Du, 0xBEEFu, 0xA5A5A5A5u));
}

GPU_TEST_CASE("resource-heap-map-placed-upload", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {0x10203040u, 0x50607080u, 0x90A0B0C0u, 0xD0E0F000u};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offset = requirements.alignment;
    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements, offset + requirements.size);
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, offset);

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

GPU_TEST_CASE("resource-heap-copy", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offsetB = alignUp(requirements.size, requirements.alignment);
    ComPtr<IResourceHeap> heap = createHeap(device, offsetB + requirements.size);

    ComPtr<IBuffer> src = createPlacedBuffer(device, desc, heap, 0, expected);
    ComPtr<IBuffer> dst = createPlacedBuffer(device, desc, heap, offsetB);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->copyBuffer(dst, 0, src, 0, sizeof(expected));
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    compareComputeResult(device, dst, makeArray<uint32_t>(1, 2, 3, 4, 5, 6, 7, 8));
}

GPU_TEST_CASE("resource-heap-lifetime", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {9, 8, 7, 6};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    ComPtr<IBuffer> buffer;
    {
        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        buffer = createPlacedBuffer(device, desc, heap, 0, expected);
    }

    compareComputeResult(device, buffer, makeArray<uint32_t>(9, 8, 7, 6));
}

GPU_TEST_CASE("resource-heap-invalid-offset", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(4096);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size);
    // Backends may grow the heap (Metal reports the allocated size), so overflow
    // against the actual heap size rather than the queried resource size.
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, heap->getDesc().size), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-unaligned-offset", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    if (requirements.alignment <= 1)
        SKIP("Device reports a 1-byte resource placement alignment");

    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size + requirements.alignment);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 1), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-too-small", D3D12 | Vulkan | Metal | CUDA)
{
    const Size heapSize = 64 * 1024;
    ComPtr<IResourceHeap> heap = createHeap(device, heapSize);

    BufferDesc desc = makeCopyBufferDesc(heap->getDesc().size + 64 * 1024);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    REQUIRE_GT(requirements.size, heap->getDesc().size);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-memory-type-mismatch", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256, MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap =
        createHeap(device, requirements.size, ResourceHeapUsage::Buffers, MemoryType::DeviceLocal);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
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

GPU_TEST_CASE("resource-heap-dedicated-buffer", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256);
    desc.usage |= BufferUsage::Shared;
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    REQUIRE_NE(
        requirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
        ResourceMemoryRequirementFlags::None
    );

    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size > 0 ? requirements.size : 64 * 1024);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-place-textures", D3D12 | Vulkan | Metal)
{
    TextureData data;
    data.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 1);

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, data.desc);
    CHECK_GE(requirements.size, 32 * 32 * 4);
    CHECK_EQ(
        requirements.flags & ResourceMemoryRequirementFlags::RequiresDedicatedAllocation,
        ResourceMemoryRequirementFlags::None
    );

    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
    ComPtr<ITexture> texture = createPlacedTexture(device, data.desc, heap, 0, data.subresourceData.data());
    data.checkEqual(texture);
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
        ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, desc);
        ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
        CHECK(createPlacedTexture(device, desc, heap, 0));
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

GPU_TEST_CASE("resource-heap-place-and-alias-textures", D3D12 | Vulkan | Metal)
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
    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(nullptr, textureA);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        uploadTextureData(device, textureA, dataA);
        dataA.checkEqual(textureA);
    }
    {
        auto encoder = queue->createCommandEncoder();
        encoder->aliasResources(textureA, textureB);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        uploadTextureData(device, textureB, dataB);
        dataB.checkEqual(textureB);
    }
}

GPU_TEST_CASE("resource-heap-place-rt-texture", D3D12 | Vulkan | Metal)
{
    TextureDesc desc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, desc);
    CHECK_EQ(requirements.usage, ResourceHeapUsage::RtDsTextures);

    ComPtr<IResourceHeap> heap = createHeapForRequirements(device, requirements);
    ComPtr<ITexture> texture = createPlacedTexture(device, desc, heap, 0);
    CHECK(texture);
    CHECK_EQ(texture->getDesc().usage & TextureUsage::RenderTarget, TextureUsage::RenderTarget);
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
