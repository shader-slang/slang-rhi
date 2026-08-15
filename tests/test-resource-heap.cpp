#include "testing.h"
#include "texture-test.h"

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
    ResourceHeapKind kind = ResourceHeapKind::Buffers,
    MemoryType memoryType = MemoryType::DeviceLocal,
    const char* label = "test-resource-heap"
)
{
    ResourceHeapDesc desc = {};
    desc.memoryType = memoryType;
    desc.kind = kind;
    desc.size = size;
    desc.label = label;
    ComPtr<IResourceHeap> heap;
    REQUIRE_CALL(device->createResourceHeap(desc, heap.writeRef()));
    CHECK(heap);
    CHECK_EQ(heap->getDesc().memoryType, memoryType);
    CHECK_EQ(heap->getDesc().kind, kind);
    CHECK_GE(heap->getDesc().size, size);
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

TEST_CASE("resource-heap-kind-helpers")
{
    BufferDesc bufferDesc = {};
    CHECK_EQ(getResourceHeapKind(bufferDesc), ResourceHeapKind::Buffers);

    TextureDesc textureDesc = {};
    textureDesc.usage = TextureUsage::ShaderResource;
    CHECK_EQ(getResourceHeapKind(textureDesc), ResourceHeapKind::NonRtDsTextures);

    textureDesc.usage = TextureUsage::UnorderedAccess;
    CHECK_EQ(getResourceHeapKind(textureDesc), ResourceHeapKind::NonRtDsTextures);

    textureDesc.usage = TextureUsage::RenderTarget;
    CHECK_EQ(getResourceHeapKind(textureDesc), ResourceHeapKind::RtDsTextures);

    textureDesc.usage = TextureUsage::DepthStencil;
    CHECK_EQ(getResourceHeapKind(textureDesc), ResourceHeapKind::RtDsTextures);

    textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::RenderTarget;
    CHECK_EQ(getResourceHeapKind(textureDesc), ResourceHeapKind::RtDsTextures);

    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::All, ResourceHeapKind::Buffers));
    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::All, ResourceHeapKind::NonRtDsTextures));
    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::All, ResourceHeapKind::RtDsTextures));
    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::Buffers, ResourceHeapKind::Buffers));
    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::NonRtDsTextures, ResourceHeapKind::NonRtDsTextures));
    CHECK(isResourceHeapKindCompatible(ResourceHeapKind::RtDsTextures, ResourceHeapKind::RtDsTextures));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::Buffers, ResourceHeapKind::NonRtDsTextures));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::Buffers, ResourceHeapKind::RtDsTextures));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::NonRtDsTextures, ResourceHeapKind::Buffers));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::NonRtDsTextures, ResourceHeapKind::RtDsTextures));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::RtDsTextures, ResourceHeapKind::Buffers));
    CHECK(!isResourceHeapKindCompatible(ResourceHeapKind::RtDsTextures, ResourceHeapKind::NonRtDsTextures));
}

GPU_TEST_CASE("resource-heap-feature", D3D12 | Vulkan | Metal | CUDA)
{
    CHECK(device->hasFeature(Feature::MemoryAliasing));
}

GPU_TEST_CASE("resource-heap-unsupported", D3D11 | CPU | WGPU)
{
    CHECK(!device->hasFeature(Feature::MemoryAliasing));

    ResourceHeapDesc heapDesc = {};
    heapDesc.size = 64 * 1024;
    ComPtr<IResourceHeap> heap;
    CHECK_EQ(device->createResourceHeap(heapDesc, heap.writeRef()), SLANG_E_NOT_AVAILABLE);
    CHECK(heap == nullptr);

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

    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    CHECK_GE(requirements.size, desc.size);
    CHECK_GT(requirements.alignment, 0);
    CHECK_EQ(requirements.alignment & (requirements.alignment - 1), 0);
    CHECK_EQ(requirements.memoryType, desc.memoryType);
    CHECK_EQ(requirements.heapKind, ResourceHeapKind::Buffers);
    CHECK(!requirements.requiresDedicatedAllocation);

    BufferDesc uploadDesc = makeCopyBufferDesc(4096, MemoryType::Upload);
    ResourceMemoryRequirements uploadRequirements = requireBufferMemoryRequirements(device, uploadDesc);
    CHECK_EQ(uploadRequirements.memoryType, MemoryType::Upload);
    CHECK_EQ(uploadRequirements.heapKind, ResourceHeapKind::Buffers);

    BufferDesc sharedDesc = makeCopyBufferDesc(4096);
    sharedDesc.usage |= BufferUsage::Shared;
    ResourceMemoryRequirements sharedRequirements = requireBufferMemoryRequirements(device, sharedDesc);
    CHECK(sharedRequirements.requiresDedicatedAllocation);
}

GPU_TEST_CASE("resource-heap-texture-requirements", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc sampledDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements sampledRequirements = requireTextureMemoryRequirements(device, sampledDesc);
    CHECK_GE(sampledRequirements.size, 32 * 32 * 4);
    CHECK_GT(sampledRequirements.alignment, 0);
    CHECK_EQ(sampledRequirements.alignment & (sampledRequirements.alignment - 1), 0);
    CHECK_EQ(sampledRequirements.memoryType, MemoryType::DeviceLocal);
    CHECK_EQ(sampledRequirements.heapKind, ResourceHeapKind::NonRtDsTextures);

    TextureDesc rtDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements rtRequirements = requireTextureMemoryRequirements(device, rtDesc);
    CHECK_EQ(rtRequirements.heapKind, ResourceHeapKind::RtDsTextures);

    TextureDesc dsDesc = {};
    dsDesc.type = TextureType::Texture2D;
    dsDesc.size = {32, 32, 1};
    dsDesc.format = Format::D32Float;
    dsDesc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
    dsDesc.memoryType = MemoryType::DeviceLocal;
    ResourceMemoryRequirements dsRequirements = requireTextureMemoryRequirements(device, dsDesc);
    CHECK_EQ(dsRequirements.heapKind, ResourceHeapKind::RtDsTextures);

    if (device->getDeviceType() == DeviceType::CUDA)
        CHECK(sampledRequirements.requiresDedicatedAllocation);
    else
        CHECK(!sampledRequirements.requiresDedicatedAllocation);
}

GPU_TEST_CASE("resource-heap-create", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(64 * 1024);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    ComPtr<IResourceHeap> heap = createHeap(
        device,
        requirements.size,
        ResourceHeapKind::Buffers,
        MemoryType::DeviceLocal,
        "named-resource-heap"
    );
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

GPU_TEST_CASE("resource-heap-create-all", D3D12 | Vulkan | Metal | CUDA)
{
    ResourceHeapDesc desc = {};
    desc.memoryType = MemoryType::DeviceLocal;
    desc.kind = ResourceHeapKind::All;
    desc.size = 256 * 1024;
    ComPtr<IResourceHeap> heap;
    Result result = device->createResourceHeap(desc, heap.writeRef());
    if (SLANG_FAILED(result))
        SKIP("ResourceHeapKind::All is not supported on this device");

    CHECK_EQ(heap->getDesc().kind, ResourceHeapKind::All);

    BufferDesc bufferDesc = makeCopyBufferDesc(256);
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, bufferDesc, heap, 0);
    CHECK_EQ(buffer->getDesc().size, bufferDesc.size);
}

GPU_TEST_CASE("resource-heap-create-texture-kinds", D3D12 | Vulkan | Metal)
{
    ComPtr<IResourceHeap> nonRtHeap = createHeap(device, 256 * 1024, ResourceHeapKind::NonRtDsTextures);
    CHECK_EQ(nonRtHeap->getDesc().kind, ResourceHeapKind::NonRtDsTextures);

    ComPtr<IResourceHeap> rtHeap = createHeap(device, 256 * 1024, ResourceHeapKind::RtDsTextures);
    CHECK_EQ(rtHeap->getDesc().kind, ResourceHeapKind::RtDsTextures);
}

GPU_TEST_CASE("resource-heap-cuda-texture-unsupported", CUDA)
{
    ResourceHeapDesc desc = {};
    desc.size = 64 * 1024;

    desc.kind = ResourceHeapKind::NonRtDsTextures;
    ComPtr<IResourceHeap> nonRtHeap;
    CHECK_EQ(device->createResourceHeap(desc, nonRtHeap.writeRef()), SLANG_E_NOT_AVAILABLE);

    desc.kind = ResourceHeapKind::RtDsTextures;
    ComPtr<IResourceHeap> rtHeap;
    CHECK_EQ(device->createResourceHeap(desc, rtHeap.writeRef()), SLANG_E_NOT_AVAILABLE);

    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, textureDesc);
    CHECK(requirements.requiresDedicatedAllocation);

    ComPtr<IResourceHeap> bufferHeap = createHeap(device, requirements.size, ResourceHeapKind::All);
    CHECK_EQ(tryCreatePlacedTexture(device, textureDesc, bufferHeap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-place-buffer-init-data", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 64;
    std::vector<uint32_t> initData(elementCount);
    for (uint32_t i = 0; i < elementCount; ++i)
        initData[i] = 0x1000u + i;

    BufferDesc desc = makeCopyBufferDesc(elementCount * sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size);
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, 0, initData.data());

    CHECK_EQ(buffer->getDesc().size, desc.size);
    CHECK_EQ(buffer->getDesc().memoryType, MemoryType::DeviceLocal);
    compareComputeResult(device, buffer, std::span<uint32_t>(initData));
}

GPU_TEST_CASE("resource-heap-place-and-alias-buffers", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 1024;
    const Size bufferSize = elementCount * sizeof(uint32_t);

    BufferDesc desc = makeComputeBufferDesc(bufferSize, sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size);

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
        encoder->aliasResources(bufferA, bufferB);
        REQUIRE_CALL(encoder->uploadBufferData(bufferB, 0, bufferSize, dataB.data()));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, bufferB, std::span<uint32_t>(dataB));
    }
}

GPU_TEST_CASE("resource-heap-place-sequential-buffers", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t elementCount = 64;
    BufferDesc desc = makeCopyBufferDesc(elementCount * sizeof(uint32_t));
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);

    const Offset offsetB = alignUp(requirements.size, requirements.alignment);
    ComPtr<IResourceHeap> heap = createHeap(device, offsetB + requirements.size);

    std::vector<uint32_t> dataA(elementCount, 0x11111111u);
    std::vector<uint32_t> dataB(elementCount, 0x22222222u);
    ComPtr<IBuffer> bufferA = createPlacedBuffer(device, desc, heap, 0, dataA.data());
    ComPtr<IBuffer> bufferB = createPlacedBuffer(device, desc, heap, offsetB, dataB.data());

    compareComputeResult(device, bufferA, std::span<uint32_t>(dataA));
    compareComputeResult(device, bufferB, std::span<uint32_t>(dataB));
}

GPU_TEST_CASE("resource-heap-place-offset", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offset = requirements.alignment;
    ComPtr<IResourceHeap> heap = createHeap(device, offset + requirements.size);

    const uint32_t expected[] = {7, 8, 9, 10};
    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, offset, expected);
    compareComputeResult(device, buffer, makeArray<uint32_t>(7, 8, 9, 10));
}

GPU_TEST_CASE("resource-heap-place-upload", D3D12 | Vulkan | Metal | CUDA)
{
    const uint32_t expected[] = {0xC0FFEEu, 0xF00Du, 0xBEEFu, 0xA5A5A5A5u};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap =
        createHeap(device, requirements.size, ResourceHeapKind::Buffers, MemoryType::Upload, "upload-resource-heap");

    ComPtr<IBuffer> buffer = createPlacedBuffer(device, desc, heap, 0, expected);
    CHECK_EQ(buffer->getDesc().memoryType, MemoryType::Upload);
    compareComputeResult(device, buffer, makeArray<uint32_t>(0xC0FFEEu, 0xF00Du, 0xBEEFu, 0xA5A5A5A5u));
}

GPU_TEST_CASE("resource-heap-place-upload-state", D3D12)
{
    BufferDesc desc = {};
    desc.size = 256;
    desc.usage = BufferUsage::CopySource;
    desc.defaultState = ResourceState::CopySource;
    desc.memoryType = MemoryType::Upload;

    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size, ResourceHeapKind::Buffers, MemoryType::Upload);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_OK);
}

GPU_TEST_CASE("resource-heap-place-readback-state", D3D12)
{
    BufferDesc desc = makeCopyBufferDesc(256, MemoryType::ReadBack);
    REQUIRE_EQ(desc.defaultState, ResourceState::Undefined);

    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size, ResourceHeapKind::Buffers, MemoryType::ReadBack);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_OK);
}

GPU_TEST_CASE("resource-heap-map-placed-upload", Vulkan)
{
    const uint32_t expected[] = {0x10203040u, 0x50607080u, 0x90A0B0C0u, 0xD0E0F000u};
    BufferDesc desc = makeCopyBufferDesc(sizeof(expected), MemoryType::Upload);
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    const Offset offset = requirements.alignment;
    ComPtr<IResourceHeap> heap =
        createHeap(device, offset + requirements.size, ResourceHeapKind::Buffers, MemoryType::Upload);
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
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size);
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
        ComPtr<IResourceHeap> heap = createHeap(device, requirements.size);
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
        createHeap(device, requirements.size, ResourceHeapKind::Buffers, MemoryType::DeviceLocal);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-kind-mismatch", D3D12 | Vulkan | Metal)
{
    BufferDesc bufferDesc = makeCopyBufferDesc(256);
    ResourceMemoryRequirements bufferRequirements = requireBufferMemoryRequirements(device, bufferDesc);
    ComPtr<IResourceHeap> textureHeap = createHeap(device, bufferRequirements.size, ResourceHeapKind::NonRtDsTextures);
    CHECK_EQ(tryCreatePlacedBuffer(device, bufferDesc, textureHeap, 0), SLANG_E_INVALID_ARG);

    TextureDesc textureDesc = makeSampleTextureDesc();
    ResourceMemoryRequirements textureRequirements = requireTextureMemoryRequirements(device, textureDesc);
    ComPtr<IResourceHeap> bufferHeap = createHeap(device, textureRequirements.size, ResourceHeapKind::Buffers);
    CHECK_EQ(tryCreatePlacedTexture(device, textureDesc, bufferHeap, 0), SLANG_E_INVALID_ARG);

    TextureDesc rtDesc = makeSampleTextureDesc(TextureUsage::RenderTarget);
    ResourceMemoryRequirements rtRequirements = requireTextureMemoryRequirements(device, rtDesc);
    ComPtr<IResourceHeap> nonRtHeap = createHeap(device, rtRequirements.size, ResourceHeapKind::NonRtDsTextures);
    CHECK_EQ(tryCreatePlacedTexture(device, rtDesc, nonRtHeap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-dedicated-buffer", D3D12 | Vulkan | Metal | CUDA)
{
    BufferDesc desc = makeCopyBufferDesc(256);
    desc.usage |= BufferUsage::Shared;
    ResourceMemoryRequirements requirements = requireBufferMemoryRequirements(device, desc);
    REQUIRE(requirements.requiresDedicatedAllocation);

    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size > 0 ? requirements.size : 64 * 1024);
    CHECK_EQ(tryCreatePlacedBuffer(device, desc, heap, 0), SLANG_E_INVALID_ARG);
}

GPU_TEST_CASE("resource-heap-place-textures", D3D12 | Vulkan | Metal)
{
    TextureData data;
    data.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 1);

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, data.desc);
    CHECK_GE(requirements.size, 32 * 32 * 4);
    CHECK(!requirements.requiresDedicatedAllocation);

    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size, requirements.heapKind);
    ComPtr<ITexture> texture = createPlacedTexture(device, data.desc, heap, 0, data.subresourceData.data());
    data.checkEqual(texture);
}

GPU_TEST_CASE("resource-heap-place-and-alias-textures", D3D12 | Vulkan | Metal)
{
    TextureData dataA;
    dataA.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 1);
    TextureData dataB;
    dataB.init(device, makeSampleTextureDesc(), TextureInitMode::Random, 2);

    ResourceMemoryRequirements requirements = requireTextureMemoryRequirements(device, dataA.desc);
    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size, requirements.heapKind);

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
    CHECK_EQ(requirements.heapKind, ResourceHeapKind::RtDsTextures);

    ComPtr<IResourceHeap> heap = createHeap(device, requirements.size, ResourceHeapKind::RtDsTextures);
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
    heapDesc.kind = ResourceHeapKind::All;
    heapDesc.size = heapSize;
    ComPtr<IResourceHeap> heap;
    if (SLANG_FAILED(device->createResourceHeap(heapDesc, heap.writeRef())))
        SKIP("ResourceHeapKind::All is not supported on this device");

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
