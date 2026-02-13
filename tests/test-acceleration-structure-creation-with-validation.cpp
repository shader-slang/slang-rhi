#include "testing.h"
#include "slang-rhi/acceleration-structure-utils.h"

using namespace rhi;
using namespace rhi::testing;

static DebugLayerOptions kDebugLayerOptions = DebugLayerOptionsBuilder().enableGPUAssistedValidation();
GPU_TEST_CASE_EX("acceleration-structure-creation-with-validation", Vulkan, kDebugLayerOptions)
{
    // Ensure that GPU-AV does not assert when using an acceleration-structure
    DeviceExtraOptions deviceExtraOptions{};
    device = createTestingDevice(ctx, ctx->deviceType, false, &deviceExtraOptions);
    auto m_device = device;
    auto m_queue = m_device->getQueue(QueueType::Graphics);
    ComPtr<IAccelerationStructure> m_bottomLevelAccelerationStructure;
    ComPtr<IAccelerationStructure> m_topLevelAccelerationStructure;

    struct Vertex
    {
        float position[3];
    };

    std::vector<Vertex> kVertexData =
        {{{-100.0f, 0, 100.0f}}, {{100.0f, 0, 100.0f}}, {{100.0f, 0, -100.0f}}, {{-100.0f, 0, -100.0f}}};
    const int kVertexCount = kVertexData.size();

    BufferDesc vertexBufferDesc = {};
    vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
    vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer = m_device->createBuffer(vertexBufferDesc, &kVertexData[0]);

    BufferDesc transformBufferDesc = {};
    transformBufferDesc.size = sizeof(float) * 12;
    transformBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    transformBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    float transformData[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    ComPtr<IBuffer> transformBuffer = m_device->createBuffer(transformBufferDesc, &transformData);

    // Build bottom level acceleration structure.
    {
        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Triangles;
        buildInput.triangles.vertexBuffers[0] = vertexBuffer;
        buildInput.triangles.vertexBufferCount = 1;
        buildInput.triangles.vertexFormat = Format::RGB32Float;
        buildInput.triangles.vertexCount = kVertexCount;
        buildInput.triangles.vertexStride = sizeof(Vertex);
        buildInput.triangles.preTransformBuffer = transformBuffer;
        buildInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;
        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes accelerationStructureSizes = {};
        m_device->getAccelerationStructureSizes(buildDesc, &accelerationStructureSizes);

        BufferDesc scratchBufferDesc = {};
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = accelerationStructureSizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = m_device->createBuffer(scratchBufferDesc);

        ComPtr<IQueryPool> compactedSizeQuery;
        QueryPoolDesc queryPoolDesc = {};
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        m_device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef());

        // Build acceleration structure.
        ComPtr<IAccelerationStructure> draftAS;
        AccelerationStructureDesc draftDesc = {};
        draftDesc.size = accelerationStructureSizes.accelerationStructureSize;
        m_device->createAccelerationStructure(draftDesc, draftAS.writeRef());

        compactedSizeQuery->reset();

        auto encoder = m_queue->createCommandEncoder();
        AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
        compactedSizeQueryDesc.queryPool = compactedSizeQuery;
        compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        encoder->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &compactedSizeQueryDesc);
        m_queue->submit(encoder->finish());
        m_queue->waitOnHost();

        uint64_t compactedSize = 0;
        compactedSizeQuery->getResult(0, 1, &compactedSize);
        AccelerationStructureDesc finalDesc;
        finalDesc.size = compactedSize;
        m_device->createAccelerationStructure(finalDesc, m_bottomLevelAccelerationStructure.writeRef());

        encoder = m_queue->createCommandEncoder();
        encoder->copyAccelerationStructure(
            m_bottomLevelAccelerationStructure,
            draftAS,
            AccelerationStructureCopyMode::Compact
        );
        m_queue->submit(encoder->finish());
        m_queue->waitOnHost();
    }


    // Build top level acceleration structure.
    {
        AccelerationStructureInstanceDescType nativeInstanceDescType =
            getAccelerationStructureInstanceDescType(m_device);
        rhi::Size nativeInstanceDescSize = getAccelerationStructureInstanceDescSize(nativeInstanceDescType);

        std::vector<AccelerationStructureInstanceDescGeneric> genericInstanceDescs;
        genericInstanceDescs.resize(1);
        genericInstanceDescs[0].accelerationStructure = m_bottomLevelAccelerationStructure->getHandle();
        genericInstanceDescs[0].flags = AccelerationStructureInstanceFlags::TriangleFacingCullDisable;
        genericInstanceDescs[0].instanceContributionToHitGroupIndex = 0;
        genericInstanceDescs[0].instanceID = 0;
        genericInstanceDescs[0].instanceMask = 0xFF;
        float transformMatrix[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        memcpy(&genericInstanceDescs[0].transform[0][0], transformMatrix, sizeof(float) * 12);

        std::vector<unsigned char> nativeInstanceDescs;
        nativeInstanceDescs.resize(genericInstanceDescs.size() * nativeInstanceDescSize);
        convertAccelerationStructureInstanceDescs(
            genericInstanceDescs.size(),
            nativeInstanceDescType,
            nativeInstanceDescs.data(),
            nativeInstanceDescSize,
            genericInstanceDescs.data(),
            sizeof(AccelerationStructureInstanceDescGeneric)
        );

        BufferDesc instanceBufferDesc = {};
        instanceBufferDesc.size = nativeInstanceDescs.size();
        instanceBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        instanceBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        ComPtr<IBuffer> instanceBuffer = m_device->createBuffer(instanceBufferDesc, nativeInstanceDescs.data());

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Instances;
        buildInput.instances.instanceBuffer = instanceBuffer;
        buildInput.instances.instanceCount = 1;
        buildInput.instances.instanceStride = nativeInstanceDescSize;
        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes accelerationStructureSizes = {};
        m_device->getAccelerationStructureSizes(buildDesc, &accelerationStructureSizes);

        BufferDesc scratchBufferDesc = {};
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = (size_t)accelerationStructureSizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = m_device->createBuffer(scratchBufferDesc);

        AccelerationStructureDesc createDesc = {};
        createDesc.size = accelerationStructureSizes.accelerationStructureSize;
        m_device->createAccelerationStructure(createDesc, m_topLevelAccelerationStructure.writeRef());

        auto encoder = m_queue->createCommandEncoder();
        encoder->buildAccelerationStructure(
            buildDesc,
            m_topLevelAccelerationStructure,
            nullptr,
            scratchBuffer,
            0,
            nullptr
        );
        m_queue->submit(encoder->finish());
        m_queue->waitOnHost();
    }
}
