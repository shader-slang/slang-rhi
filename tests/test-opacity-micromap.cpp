#include "testing.h"
#include "rhi-shared.h"
#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("find-struct-in-chain")
{
    D3D12DeviceExtendedDesc first = {};
    VulkanDeviceExtendedDesc second = {};
    first.next = &second;

    CHECK(findStructInChain<D3D12DeviceExtendedDesc>(&first) == &first);
    CHECK(findStructInChain<VulkanDeviceExtendedDesc>(&first) == &second);
    CHECK(findStructInChain<D3D12ExperimentalFeaturesDesc>(&first) == nullptr);
    CHECK(findStructInChain<D3D12DeviceExtendedDesc>(nullptr) == nullptr);
}

GPU_TEST_CASE("opacity-micromap-build", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::OpacityMicromap))
        SKIP("Opacity micromaps are not supported");

    std::array<uint8_t, 256> opacityData = {};
    opacityData[0] = 1; // One opaque level-zero microtriangle.
    BufferDesc inputDesc = {};
    inputDesc.size = opacityData.size();
    inputDesc.usage = BufferUsage::MicromapBuildInput;
    inputDesc.defaultState = ResourceState::MicromapBuildInput;
    ComPtr<IBuffer> inputBuffer = device->createBuffer(inputDesc, opacityData.data());
    REQUIRE(inputBuffer);

    std::array<MicromapTriangleDesc, 32> triangleDescs = {};
    triangleDescs[0].dataOffset = 0;
    triangleDescs[0].subdivisionLevel = 0;
    triangleDescs[0].format = uint16_t(OpacityMicromapFormat::TwoState);
    BufferDesc triangleDescBufferDesc = inputDesc;
    triangleDescBufferDesc.size = sizeof(triangleDescs);
    ComPtr<IBuffer> triangleDescBuffer = device->createBuffer(triangleDescBufferDesc, triangleDescs.data());
    REQUIRE(triangleDescBuffer);

    MicromapUsageCount usageCount = {1, 0, uint32_t(OpacityMicromapFormat::TwoState)};
    MicromapBuildDesc buildDesc = {};
    buildDesc.dataBuffer = inputBuffer;
    buildDesc.descriptorBuffer = triangleDescBuffer;
    buildDesc.histogram = &usageCount;
    buildDesc.histogramCount = 1;

    MicromapSizes sizes = {};
    REQUIRE_CALL(device->getMicromapSizes(buildDesc, &sizes));
    REQUIRE(sizes.micromapSize > 0);
    REQUIRE(sizes.scratchSize > 0);

    MicromapDesc micromapDesc = {};
    micromapDesc.size = sizes.micromapSize;
    ComPtr<IMicromap> micromap;
    REQUIRE_CALL(device->createMicromap(micromapDesc, micromap.writeRef()));

    BufferDesc scratchDesc = {};
    scratchDesc.size = sizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchDesc);
    REQUIRE(scratchBuffer);

    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);
    auto encoder = queue->createCommandEncoder();
    REQUIRE(encoder);
    encoder->buildMicromap(buildDesc, micromap, scratchBuffer);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    struct Vertex
    {
        float position[3];
    };
    Vertex vertices[] = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    BufferDesc vertexDesc = {};
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexDesc, vertices);
    REQUIRE(vertexBuffer);

    AccelerationStructureOpacityMicromapDesc ommAttachment = {};
    ommAttachment.link.micromap = micromap;
    ommAttachment.link.usageCounts = &usageCount;
    ommAttachment.link.usageCount = 1;

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Triangles;
    buildInput.triangles.vertexBuffers[0] = vertexBuffer;
    buildInput.triangles.vertexBufferCount = 1;
    buildInput.triangles.vertexFormat = Format::RGB32Float;
    buildInput.triangles.vertexCount = 3;
    buildInput.triangles.vertexStride = sizeof(Vertex);
    buildInput.triangles.flags = AccelerationStructureGeometryFlags::None;
    buildInput.triangles.next = &ommAttachment;
    AccelerationStructureBuildDesc blasBuildDesc = {};
    blasBuildDesc.inputs = &buildInput;
    blasBuildDesc.inputCount = 1;

    AccelerationStructureSizes blasSizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(blasBuildDesc, &blasSizes));
    AccelerationStructureDesc blasDesc = {};
    blasDesc.kind = AccelerationStructureKind::BottomLevel;
    blasDesc.size = blasSizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> blas;
    REQUIRE_CALL(device->createAccelerationStructure(blasDesc, blas.writeRef()));
    scratchDesc.size = blasSizes.scratchSize;
    scratchBuffer = device->createBuffer(scratchDesc);
    REQUIRE(scratchBuffer);

    encoder = queue->createCommandEncoder();
    REQUIRE(encoder);
    encoder->buildAccelerationStructure(blasBuildDesc, blas, nullptr, scratchBuffer, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    uint16_t micromapIndex = 0;
    BufferDesc indexDesc = {};
    indexDesc.size = sizeof(micromapIndex);
    indexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    indexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> micromapIndexBuffer = device->createBuffer(indexDesc, &micromapIndex);
    REQUIRE(micromapIndexBuffer);
    ommAttachment.link.indexingMode = MicromapIndexingMode::Indexed;
    ommAttachment.link.indexBuffer = micromapIndexBuffer;
    ommAttachment.link.indexFormat = MicromapIndexFormat::Uint16;
    ommAttachment.link.indexStride = sizeof(uint16_t);

    REQUIRE_CALL(device->getAccelerationStructureSizes(blasBuildDesc, &blasSizes));
    blasDesc.size = blasSizes.accelerationStructureSize;
    blas.setNull();
    REQUIRE_CALL(device->createAccelerationStructure(blasDesc, blas.writeRef()));
    scratchDesc.size = blasSizes.scratchSize;
    scratchBuffer = device->createBuffer(scratchDesc);
    REQUIRE(scratchBuffer);
    encoder = queue->createCommandEncoder();
    REQUIRE(encoder);
    encoder->buildAccelerationStructure(blasBuildDesc, blas, nullptr, scratchBuffer, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

GPU_TEST_CASE("opacity-micromap-trace", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::OpacityMicromap))
        SKIP("Opacity micromaps are not supported");

    auto queue = device->getQueue(QueueType::Graphics);

    // Two level-zero OMMs followed by a level-one OMM whose four microtriangles
    // alternate between transparent and opaque.
    std::array<uint8_t, 256> opacityData = {};
    opacityData[1] = 1;
    opacityData[2] = 0b1010;
    BufferDesc inputDesc = {};
    inputDesc.size = opacityData.size();
    inputDesc.usage = BufferUsage::MicromapBuildInput;
    inputDesc.defaultState = ResourceState::MicromapBuildInput;
    ComPtr<IBuffer> inputBuffer = device->createBuffer(inputDesc, opacityData.data());
    REQUIRE(inputBuffer);

    std::array<MicromapTriangleDesc, 32> triangleDescs = {};
    triangleDescs[0] = {0, 0, uint16_t(OpacityMicromapFormat::TwoState)};
    triangleDescs[1] = {1, 0, uint16_t(OpacityMicromapFormat::TwoState)};
    triangleDescs[2] = {2, 1, uint16_t(OpacityMicromapFormat::TwoState)};
    BufferDesc triangleDescBufferDesc = inputDesc;
    triangleDescBufferDesc.size = sizeof(triangleDescs);
    ComPtr<IBuffer> triangleDescBuffer = device->createBuffer(triangleDescBufferDesc, triangleDescs.data());
    REQUIRE(triangleDescBuffer);

    MicromapUsageCount usageCounts[] = {
        {2, 0, uint32_t(OpacityMicromapFormat::TwoState)},
        {1, 1, uint32_t(OpacityMicromapFormat::TwoState)},
    };
    MicromapBuildDesc micromapBuildDesc = {};
    micromapBuildDesc.dataBuffer = inputBuffer;
    micromapBuildDesc.descriptorBuffer = triangleDescBuffer;
    micromapBuildDesc.histogram = usageCounts;
    micromapBuildDesc.histogramCount = SLANG_COUNT_OF(usageCounts);

    MicromapSizes micromapSizes = {};
    REQUIRE_CALL(device->getMicromapSizes(micromapBuildDesc, &micromapSizes));
    MicromapDesc micromapDesc = {};
    micromapDesc.size = micromapSizes.micromapSize;
    ComPtr<IMicromap> micromap;
    REQUIRE_CALL(device->createMicromap(micromapDesc, micromap.writeRef()));

    BufferDesc scratchDesc = {};
    scratchDesc.size = micromapSizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> micromapScratchBuffer = device->createBuffer(scratchDesc);
    REQUIRE(micromapScratchBuffer);

    Vertex vertices[] = {
        {{-0.9f, -0.5f, 1.f}},
        {{-0.1f, -0.5f, 1.f}},
        {{-0.5f, 0.5f, 1.f}},
        {{0.1f, -0.5f, 1.f}},
        {{0.9f, -0.5f, 1.f}},
        {{0.5f, 0.5f, 1.f}},
        {{1.f, -0.5f, 1.f}},
        {{2.f, -0.5f, 1.f}},
        {{1.5f, 0.5f, 1.f}},
    };
    BufferDesc vertexDesc = {};
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexDesc, vertices);
    REQUIRE(vertexBuffer);

    AccelerationStructureOpacityMicromapDesc ommAttachment = {};
    ommAttachment.link.micromap = micromap;
    ommAttachment.link.usageCounts = usageCounts;
    ommAttachment.link.usageCount = SLANG_COUNT_OF(usageCounts);

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Triangles;
    buildInput.triangles.vertexBuffers[0] = vertexBuffer;
    buildInput.triangles.vertexBufferCount = 1;
    buildInput.triangles.vertexFormat = Format::RGB32Float;
    buildInput.triangles.vertexCount = SLANG_COUNT_OF(vertices);
    buildInput.triangles.vertexStride = sizeof(Vertex);
    buildInput.triangles.flags = AccelerationStructureGeometryFlags::None;
    buildInput.triangles.next = &ommAttachment;

    AccelerationStructureBuildDesc blasBuildDesc = {};
    blasBuildDesc.inputs = &buildInput;
    blasBuildDesc.inputCount = 1;
    AccelerationStructureSizes blasSizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(blasBuildDesc, &blasSizes));

    AccelerationStructureDesc blasDesc = {};
    blasDesc.kind = AccelerationStructureKind::BottomLevel;
    blasDesc.size = blasSizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> blas;
    REQUIRE_CALL(device->createAccelerationStructure(blasDesc, blas.writeRef()));
    scratchDesc.size = blasSizes.scratchSize;
    ComPtr<IBuffer> blasScratchBuffer = device->createBuffer(scratchDesc);
    REQUIRE(blasScratchBuffer);

    auto encoder = queue->createCommandEncoder();
    REQUIRE(encoder);
    encoder->buildMicromap(micromapBuildDesc, micromap, micromapScratchBuffer);
    // OptiX guarantees ordering through stream submission rather than explicit resource barriers.
    if (device->getDeviceType() == DeviceType::CUDA)
    {
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        encoder = queue->createCommandEncoder();
        REQUIRE(encoder);
    }
    encoder->buildAccelerationStructure(blasBuildDesc, blas, nullptr, blasScratchBuffer, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    TLAS tlas(device, queue, blas);
    RayTracingTestPipeline pipeline(
        device,
        "test-opacity-micromap",
        {"rayGen"},
        {{"closestHit", nullptr, nullptr}},
        {"miss"},
        RayTracingPipelineFlags::EnableOpacityMicromaps
    );

    BufferDesc resultDesc = {};
    resultDesc.size = 6 * sizeof(uint32_t);
    resultDesc.elementSize = sizeof(uint32_t);
    resultDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    resultDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> resultBuffer = device->createBuffer(resultDesc);
    REQUIRE(resultBuffer);

    encoder = queue->createCommandEncoder();
    auto passEncoder = encoder->beginRayTracingPass();
    auto rootObject = passEncoder->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
    ShaderCursor(rootObject)["resultBuffer"].setBinding(resultBuffer);
    ShaderCursor(rootObject)["sceneBVH"].setBinding(tlas.tlas);
    passEncoder->dispatchRays(0, 6, 1, 1);
    passEncoder->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    ComPtr<ISlangBlob> resultBlob;
    REQUIRE_CALL(device->readBuffer(resultBuffer, 0, resultDesc.size, resultBlob.writeRef()));
    const auto* results = static_cast<const uint32_t*>(resultBlob->getBufferPointer());
    CHECK(results[0] == 1); // Transparent OMM: miss.
    CHECK(results[1] == 2); // Opaque OMM: closest hit.
    CHECK(results[2] == 1); // Partial OMM microtriangle 0: transparent.
    CHECK(results[3] == 2); // Partial OMM microtriangle 1: opaque.
    CHECK(results[4] == 1); // Partial OMM microtriangle 2: transparent.
    CHECK(results[5] == 2); // Partial OMM microtriangle 3: opaque.
}
