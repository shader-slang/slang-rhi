#include "testing.h"
#include "test-ray-tracing-common.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-api.h"
#endif

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("ray-tracing-large-records", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    auto session = device->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    auto module = session->loadModule("test-ray-tracing-large-records", diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module != nullptr);
    const bool isD3D12 = device->getDeviceType() == DeviceType::D3D12;
    const char* largeMissName = isD3D12 ? "missLimited" : "missLarge";
    const char* names[] = {"rayGen", "closestHit", largeMissName, "missSmall", "callableMain"};
    ComPtr<slang::IEntryPoint> entryPoints[5];
    slang::IComponentType* components[5];
    for (uint32_t i = 0; i < 5; ++i)
    {
        REQUIRE_CALL(module->findEntryPointByName(names[i], entryPoints[i].writeRef()));
        components[i] = entryPoints[i];
    }
    ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = components;
    programDesc.slangEntryPointCount = 5;
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

    const uint32_t entryIndices[4][2] = {{0, 0}, {2, 3}, {1, 1}, {4, 4}};
    const uint32_t vectorCounts[4][2] = {{20, 20}, {isD3D12 ? 32u : 254u, 4}, {24, 24}, {28, 28}};
    std::vector<uint8_t> bytes[4][2];
    uint32_t sums[4][2] = {};
    ShaderRecordData records[4][2];
    auto packingRoot = device->createRootShaderObject(program);
    REQUIRE(packingRoot != nullptr);
    for (uint32_t section = 0; section < 4; ++section)
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto object = packingRoot->getEntryPoint(entryIndices[section][i]);
            REQUIRE(object != nullptr);
            std::vector<uint32_t> values(vectorCounts[section][i] * 4);
            for (uint32_t j = 0; j < values.size(); ++j)
            {
                values[j] = 1 + section * 10 + i * 100 + j;
                sums[section][i] += values[j];
            }
            REQUIRE_CALL(ShaderCursor(object)["environment"].setData(values.data(), values.size() * sizeof(uint32_t)));
            REQUIRE(object->getSize() == values.size() * sizeof(uint32_t));
            const auto* data = static_cast<const uint8_t*>(object->getRawData());
            bytes[section][i].assign(data, data + object->getSize());
            records[section][i] = {bytes[section][i].data(), uint32_t(bytes[section][i].size())};
        }
    }

    const char* rayGenNames[] = {"rayGen", "rayGen"};
    const char* missNames[] = {largeMissName, "missSmall"};
    const char* hitNames[] = {"hit", "hit"};
    const char* callableNames[] = {"callableMain", "callableMain"};
    ShaderTableDesc tableDesc;
    tableDesc.program = program;
    tableDesc.rayGenShaderCount = tableDesc.missShaderCount = tableDesc.hitGroupCount = tableDesc.callableShaderCount =
        2;
    tableDesc.rayGenShaderEntryPointNames = rayGenNames;
    tableDesc.missShaderEntryPointNames = missNames;
    tableDesc.hitGroupNames = hitNames;
    tableDesc.callableShaderEntryPointNames = callableNames;
    tableDesc.rayGenShaderRecordData = records[0];
    tableDesc.missShaderRecordData = records[1];
    tableDesc.hitGroupRecordData = records[2];
    tableDesc.callableShaderRecordData = records[3];
    ComPtr<IShaderTable> tables[2];
    REQUIRE_CALL(device->createShaderTable(tableDesc, tables[0].writeRef()));

    // Change the runtime extents of three sections without changing any shader layout.
    // Keep D3D12 within its temporary driver cap while exercising larger Vulkan records.
    for (uint32_t section = 0; section < 4; ++section)
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto& data = bytes[section][i];
            if (section != 1 && i == 1)
                data.resize(isD3D12 ? 512 : data.size() + 512);
            uint32_t first;
            memcpy(&first, data.data(), sizeof(first));
            first += 7;
            memcpy(data.data(), &first, sizeof(first));
            records[section][i] = {data.data(), uint32_t(data.size())};
        }
    }
    REQUIRE_CALL(device->createShaderTable(tableDesc, tables[1].writeRef()));
    // Both tables must own snapshots, even though native realization happens at bind.
    for (auto& section : bytes)
        for (auto& data : section)
            memset(data.data(), 0, data.size());

    auto queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);
    BufferDesc outputDesc;
    outputDesc.size = 4 * sizeof(uint32_t);
    outputDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto output = device->createBuffer(outputDesc);
    REQUIRE(output != nullptr);
    for (uint32_t table = 0; table < 2; ++table)
    {
        for (uint32_t rayGen = 0; rayGen < 2; ++rayGen)
        {
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginRayTracingPass();
            auto root = pass->bindPipeline(pipeline, tables[table]);
            REQUIRE(root != nullptr);
            REQUIRE_CALL(ShaderCursor(root)["scene"].setBinding(tlas.tlas));
            REQUIRE_CALL(ShaderCursor(root)["output"].setBinding(output));
            pass->dispatchRays(rayGen, 4, 1, 1);
            pass->end();
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
            std::array<uint32_t, 4> expected = {};
            for (uint32_t i = 0; i < 4; ++i)
                expected[i] = sums[0][rayGen] + sums[i < 2 ? 2 : 1][i % 2] + sums[3][i % 2] + table * 21;
            compareComputeResult(device, output, expected);
        }
    }
}

// A dedicated device captures expected validation errors without changing the shared test device.
class RecordLimitDebugCallback : public IDebugCallback
{
public:
    std::string errors;

    void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        if (type == DebugMessageType::Error)
            errors += std::string(message) + "\n";
    }
};

GPU_TEST_CASE("ray-tracing-record-limits", (D3D12 | Vulkan) | DontCreateDevice)
{
    RecordLimitDebugCallback callback;
    ComPtr<IDevice> validationDevice;
    DeviceDesc desc;
    desc.deviceType = ctx->deviceType;
    desc.adapter = getSelectedDeviceAdapter(ctx->deviceType);
    desc.slang.slangGlobalSession = ctx->slangGlobalSession;
    desc.debugCallback = &callback;
    REQUIRE_CALL(getRHI()->createDevice(desc, validationDevice.writeRef()));
    if (!validationDevice->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    uint32_t maxDataSize = 512;
#if SLANG_RHI_ENABLE_VULKAN
    if (ctx->deviceType == DeviceType::Vulkan)
    {
        // Query the actual device rather than assuming Vulkan's minimum stride limit.
        DeviceNativeHandles handles;
        REQUIRE_CALL(validationDevice->getNativeDeviceHandles(&handles));
        vk::VulkanModule loader;
        REQUIRE_CALL(loader.init());
        auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(loader.getFunction("vkGetInstanceProcAddr"));
        REQUIRE(getProc != nullptr);
        auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            getProc(reinterpret_cast<VkInstance>(handles.handles[0].value), "vkGetPhysicalDeviceProperties2")
        );
        REQUIRE(getProperties != nullptr);
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
        };
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rtProperties};
        getProperties(reinterpret_cast<VkPhysicalDevice>(handles.handles[1].value), &properties);
        maxDataSize = (rtProperties.maxShaderGroupStride & ~(rtProperties.shaderGroupBaseAlignment - 1)) -
                      rtProperties.shaderGroupHandleSize;
    }
#endif
    auto session = validationDevice->getSlangSession();
    const uint32_t arrayLength = maxDataSize / 16 + 1;
    std::string source = "[shader(\"raygeneration\")] void tooLarge(uniform uint4 data[" + std::to_string(arrayLength) +
                         "]) {}\n[shader(\"raygeneration\")] void validRayGen() {}";
    ComPtr<slang::IBlob> diagnostics;
    auto module =
        session
            ->loadModuleFromSourceString("recordLimits", "recordLimits.slang", source.c_str(), diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module != nullptr);
    ComPtr<slang::IEntryPoint> entryPoint;
    REQUIRE_CALL(module->findEntryPointByName("tooLarge", entryPoint.writeRef()));
    slang::IComponentType* component = entryPoint;
    ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = &component;
    programDesc.slangEntryPointCount = 1;
    programDesc.useRayTracingShaderRecordParameters = true;
    ComPtr<IShaderProgram> program;
    callback.errors.clear();
    CHECK(
        validationDevice->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef()) ==
        SLANG_E_NOT_AVAILABLE
    );
    CHECK(callback.errors.find("tooLarge") != std::string::npos);
    CHECK(callback.errors.find(std::to_string(arrayLength * 16)) != std::string::npos);
    if (ctx->deviceType == DeviceType::D3D12)
    {
        CHECK(callback.errors.find("512") != std::string::npos);
        CHECK(callback.errors.find("D3D12 driver blocker") != std::string::npos);
    }
    else
        CHECK(callback.errors.find("driver blocker") == std::string::npos);

    REQUIRE_CALL(module->findEntryPointByName("validRayGen", entryPoint.writeRef()));
    component = entryPoint;
    REQUIRE_CALL(validationDevice->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef()));
    const char* name = "validRayGen";
    std::vector<uint8_t> bytes(maxDataSize + 16);
    ShaderRecordData record = {bytes.data(), maxDataSize};
    ShaderTableDesc tableDesc;
    tableDesc.program = program;
    tableDesc.rayGenShaderCount = 1;
    tableDesc.rayGenShaderEntryPointNames = &name;
    tableDesc.rayGenShaderRecordData = &record;
    ComPtr<IShaderTable> table;
    REQUIRE_CALL(validationDevice->createShaderTable(tableDesc, table.writeRef()));

    // Reject a one-byte excess and an overflowing offset before copying caller memory.
    for (const ShaderRecordData invalid :
         {ShaderRecordData{bytes.data(), maxDataSize + 1},
          ShaderRecordData{bytes.data(), 16, UINT32_MAX - 3},
          ShaderRecordData{nullptr, 1}})
    {
        record = invalid;
        callback.errors.clear();
        CHECK(validationDevice->createShaderTable(tableDesc, table.writeRef()) == SLANG_E_INVALID_ARG);
        CHECK(callback.errors.find("ray-generation record 0 ('validRayGen')") != std::string::npos);
        if (invalid.data)
        {
            CHECK(callback.errors.find(std::to_string(uint64_t(invalid.offset) + invalid.size)) != std::string::npos);
            CHECK(callback.errors.find(std::to_string(maxDataSize)) != std::string::npos);
            if (ctx->deviceType == DeviceType::D3D12)
                CHECK(callback.errors.find("D3D12 driver blocker") != std::string::npos);
            else
                CHECK(callback.errors.find("driver blocker") == std::string::npos);
        }
        else
            CHECK(callback.errors.find("null data pointer") != std::string::npos);
    }
}
