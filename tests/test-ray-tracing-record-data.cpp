#include "testing.h"
#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("ray-tracing-record-data", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<slang::ISession> session;
    REQUIRE_CALL(device->getSlangSession(session.writeRef()));
    ComPtr<slang::IBlob> diagnostics;
    auto module = session->loadModule("test-ray-tracing-record-data", diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module != nullptr);
    const char* names[] = {"rayGen", "closestHit", "missA", "missB"};
    std::vector<ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*> components;
    for (const char* name : names)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        REQUIRE_CALL(module->findEntryPointByName(name, entryPoint.writeRef()));
        components.push_back(entryPoint.get());
        entryPoints.push_back(entryPoint);
    }
    ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = components.data();
    programDesc.slangEntryPointCount = uint32_t(components.size());
    programDesc.useRayTracingShaderRecordParameters = true;
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(device->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);

    HitGroupDesc hitGroup;
    hitGroup.hitGroupName = "hit";
    hitGroup.closestHitEntryPoint = "closestHit";
    RayTracingPipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    pipelineDesc.hitGroupCount = 1;
    pipelineDesc.hitGroups = &hitGroup;
    pipelineDesc.maxRecursion = 1;
    pipelineDesc.maxRayPayloadSize = sizeof(uint32_t);
    ComPtr<IRayTracingPipeline> pipeline;
    REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);
    const uint32_t values[] = {100, 200, 300, 400, 500, 600};
    BufferDesc arenaDesc;
    arenaDesc.size = sizeof(values);
    arenaDesc.usage = BufferUsage::ShaderResource;
    auto arena = device->createBuffer(arenaDesc, values);
    REQUIRE(arena != nullptr);
    BufferDesc outputDesc;
    outputDesc.size = 4 * sizeof(uint32_t);
    outputDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto output = device->createBuffer(outputDesc);
    REQUIRE(output != nullptr);

    const char* rayGenNames[] = {"rayGen", "rayGen"};
    const char* missNames[] = {"missA", "missB"};
    const char* hitNames[] = {"hit", "hit"};
    uint32_t rayGenValues[] = {0, 1};
    uint32_t missValues[] = {4, 5};
    uint32_t hitValues[][4] = {{0, 0, 0, 2}, {0, 0, 0, 3}};
    ShaderRecordData rayGenData[] = {{&rayGenValues[0], 4}, {&rayGenValues[1], 4}};
    // Exercise application-relative offsets and zero-initialized leading padding.
    ShaderRecordData missData[] = {{&missValues[0], 4, 4}, {&missValues[1], 4, 4}};
    // Sixteen bytes also exercises payloads larger than ShaderRecordOverwrite supports.
    ShaderRecordData hitData[] = {{hitValues[0], 16}, {hitValues[1], 16}};
    ShaderTableDesc tableDesc;
    tableDesc.program = program;
    tableDesc.rayGenShaderCount = 2;
    tableDesc.rayGenShaderEntryPointNames = rayGenNames;
    tableDesc.rayGenShaderRecordData = rayGenData;
    tableDesc.missShaderCount = 2;
    tableDesc.missShaderEntryPointNames = missNames;
    tableDesc.missShaderRecordData = missData;
    tableDesc.hitGroupCount = 2;
    tableDesc.hitGroupNames = hitNames;
    tableDesc.hitGroupRecordData = hitData;
    ComPtr<IShaderTable> table;
    REQUIRE_CALL(device->createShaderTable(tableDesc, table.writeRef()));

    // Shader table creation must snapshot its inputs before native realization at bind.
    rayGenValues[0] = 5;
    hitValues[0][3] = 5;
    missValues[0] = 5;
    auto dispatch = [&](IShaderTable* shaderTable, uint32_t rayGenIndex)
    {
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto root = pass->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(root);
        cursor["scene"].setBinding(tlas.tlas);
        cursor["arena"].setBinding(arena);
        cursor["output"].setBinding(output);
        pass->dispatchRays(rayGenIndex, 4, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    };
    dispatch(table, 0);
    compareComputeResult(device, output, std::array<uint32_t, 4>{400, 500, 600, 1700});
    dispatch(table, 1);
    compareComputeResult(device, output, std::array<uint32_t, 4>{500, 600, 700, 1800});

    // New record values use exactly the same native pipeline and shader identifiers.
    ComPtr<IShaderTable> updatedTable;
    REQUIRE_CALL(device->createShaderTable(tableDesc, updatedTable.writeRef()));
    dispatch(updatedTable, 0);
    compareComputeResult(device, output, std::array<uint32_t, 4>{1200, 1000, 1200, 2200});
}

// Cursors pack record-local descriptor handles using the target layout. The record bytes
// must work without global resource bindings and without copying a host structure layout.
GPU_TEST_CASE("ray-tracing-record-bindless", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing) || !device->hasFeature(Feature::Bindless))
        SKIP("bindless ray tracing not supported");

    ComPtr<slang::ISession> session;
    REQUIRE_CALL(device->getSlangSession(session.writeRef()));
    ComPtr<slang::IBlob> diagnostics;
    auto module = session->loadModule("test-ray-tracing-record-bindless", diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module != nullptr);
    const char* names[] = {"rayGen", "closestHit", "missA", "missB"};
    const char* exports[] = {"recordRayGen", "recordClosestHit", "recordMissA", "recordMissB"};
    std::vector<ComPtr<slang::IComponentType>> entryPoints;
    std::vector<slang::IComponentType*> components;
    for (uint32_t i = 0; i < 4; ++i)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        REQUIRE_CALL(module->findEntryPointByName(names[i], entryPoint.writeRef()));
        ComPtr<slang::IComponentType> renamed;
        REQUIRE_CALL(entryPoint->renameEntryPoint(exports[i], renamed.writeRef()));
        components.push_back(renamed.get());
        entryPoints.push_back(renamed);
    }
    ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = components.data();
    programDesc.slangEntryPointCount = uint32_t(components.size());
    programDesc.useRayTracingShaderRecordParameters = true;
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(device->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);

    HitGroupDesc hitGroup;
    hitGroup.hitGroupName = "recordHit";
    hitGroup.closestHitEntryPoint = exports[1];
    RayTracingPipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    pipelineDesc.hitGroupCount = 1;
    pipelineDesc.hitGroups = &hitGroup;
    pipelineDesc.maxRecursion = 1;
    pipelineDesc.maxRayPayloadSize = sizeof(uint32_t);
    ComPtr<IRayTracingPipeline> pipeline;
    REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);
    DescriptorHandle sceneHandle;
    REQUIRE_CALL(tlas.tlas->getDescriptorHandle(&sceneHandle));
    CHECK(sceneHandle.type == DescriptorHandleType::AccelerationStructure);
    if (device->getDeviceType() == DeviceType::Vulkan)
        CHECK(sceneHandle.value == tlas.tlas->getDeviceAddress());
    DescriptorHandle repeatedSceneHandle;
    REQUIRE_CALL(tlas.tlas->getDescriptorHandle(&repeatedSceneHandle));
    CHECK(sceneHandle.value == repeatedSceneHandle.value);

    ComPtr<IBuffer> values[2];
    DescriptorHandle valueHandles[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        const uint32_t data[] = {100 + 200 * i, 200 + 200 * i};
        BufferDesc desc;
        desc.size = sizeof(data);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource;
        REQUIRE_CALL(device->createBuffer(desc, data, values[i].writeRef()));
        REQUIRE_CALL(
            values[i]
                ->getDescriptorHandle(DescriptorHandleAccess::Read, Format::Undefined, kEntireBuffer, &valueHandles[i])
        );
    }
    BufferDesc outputDesc;
    outputDesc.size = 4 * sizeof(uint32_t);
    outputDesc.elementSize = sizeof(uint32_t);
    outputDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto output = device->createBuffer(outputDesc);
    REQUIRE(output != nullptr);
    DescriptorHandle outputHandle;
    REQUIRE_CALL(
        output->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, Format::Undefined, kEntireBuffer, &outputHandle)
    );

    ComPtr<IShaderObject> roots[2];
    ComPtr<IShaderObject> rayGenObjects[2];
    ShaderRecordData rayGenData[2], missData[2], hitData[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        REQUIRE_CALL(device->createRootShaderObject(program, roots[i].writeRef()));
        REQUIRE_CALL(roots[i]->getEntryPoint(0, rayGenObjects[i].writeRef()));
        auto rayGenCursor = ShaderCursor(rayGenObjects[i]);
        REQUIRE(rayGenCursor["environment"].m_offset.uniformOffset > 0);
        REQUIRE_CALL(rayGenCursor["prefix"].setData(std::array<uint32_t, 4>{0, 0, 0, 10}));
        REQUIRE_CALL(rayGenCursor["environment"]["scene"].setDescriptorHandle(sceneHandle));
        REQUIRE_CALL(rayGenCursor["environment"]["output"].setDescriptorHandle(outputHandle));
        REQUIRE_CALL(rayGenCursor["environment"]["bias"].setData(uint32_t(100 * i)));
        rayGenData[i] = {rayGenObjects[i]->getRawData(), uint32_t(rayGenObjects[i]->getSize())};

        auto hitObject = roots[i]->getEntryPoint(1);
        REQUIRE(hitObject != nullptr);
        auto hitCursor = ShaderCursor(hitObject);
        REQUIRE(hitCursor["environment"].m_offset.uniformOffset > 0);
        REQUIRE_CALL(hitCursor["prefix"].setData(uint32_t(20)));
        REQUIRE_CALL(hitCursor["environment"]["values"].setDescriptorHandle(valueHandles[i]));
        REQUIRE_CALL(hitCursor["environment"]["index"].setData(uint32_t(0)));
        hitData[i] = {hitObject->getRawData(), uint32_t(hitObject->getSize())};

        auto missObject = roots[i]->getEntryPoint(2 + i);
        REQUIRE(missObject != nullptr);
        auto missCursor = ShaderCursor(missObject);
        REQUIRE(missCursor["environment"].m_offset.uniformOffset > 0);
        REQUIRE_CALL(missCursor["prefix"].setData(std::array<uint32_t, 2>{0, 30}));
        REQUIRE_CALL(missCursor["environment"]["values"].setDescriptorHandle(valueHandles[i]));
        REQUIRE_CALL(missCursor["environment"]["index"].setData(uint32_t(1)));
        missData[i] = {missObject->getRawData(), uint32_t(missObject->getSize())};
    }
    const char* rayGenNames[] = {exports[0], exports[0]};
    const char* missNames[] = {exports[2], exports[3]};
    const char* hitNames[] = {"recordHit", "recordHit"};
    ShaderTableDesc tableDesc;
    tableDesc.program = program;
    tableDesc.rayGenShaderCount = 2;
    tableDesc.rayGenShaderEntryPointNames = rayGenNames;
    tableDesc.rayGenShaderRecordData = rayGenData;
    tableDesc.missShaderCount = 2;
    tableDesc.missShaderEntryPointNames = missNames;
    tableDesc.missShaderRecordData = missData;
    tableDesc.hitGroupCount = 2;
    tableDesc.hitGroupNames = hitNames;
    tableDesc.hitGroupRecordData = hitData;
    ComPtr<IShaderTable> table;
    REQUIRE_CALL(device->createShaderTable(tableDesc, table.writeRef()));

    // Mutating the packing objects after table creation must not change its snapshots.
    REQUIRE_CALL(ShaderCursor(rayGenObjects[0])["environment"]["bias"].setData(uint32_t(999)));
    for (uint32_t i = 0; i < 2; ++i)
    {
        auto encoder = queue->createCommandEncoder();
        encoder->setBufferState(values[0], ResourceState::ShaderResource);
        encoder->setBufferState(values[1], ResourceState::ShaderResource);
        encoder->setBufferState(output, ResourceState::UnorderedAccess);
        auto pass = encoder->beginRayTracingPass();
        pass->bindPipeline(pipeline, table);
        pass->dispatchRays(i, 4, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(
            device,
            output,
            std::array<uint32_t, 4>{130 + 100 * i, 330 + 100 * i, 240 + 100 * i, 1440 + 100 * i}
        );
    }
}
