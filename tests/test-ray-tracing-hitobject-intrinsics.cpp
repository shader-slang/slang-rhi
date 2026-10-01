#include "testing.h"
#include "test-ray-tracing-common.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct TestResult
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float rayOrigin[3];
    float rayDirection[3];
};

struct RayTracingSingleTriangleTest
{
    enum class RecordSection
    {
        HitGroup,
        Miss,
    };

    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<const char*>& closestHitNames,
        const std::vector<const char*>& missNames,
        bool useRecordData = false,
        RecordSection recordSection = RecordSection::HitGroup,
        uint32_t selectedRecordIndex = 0
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        std::vector<HitGroupProgramNames> hitGroupProgramNames;
        for (const char* closestHitName : closestHitNames)
            hitGroupProgramNames.push_back({closestHitName, /*anyhit=*/nullptr, /*intersection=*/nullptr});

        uint32_t testSbtValue = 0xDEADBEEF;

        // Populate hit group SBT with test data
        // SBT record layout: [Shader Identifier / Record Header (32 bytes)] [Local Root Arguments]
        // According to the DXR specification and OptiX documentation, the shader identifier / record header is always
        // at offset 0. Both D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES and OPTIX_SBT_RECORD_HEADER_SIZE are 32 bytes, so
        // local root arguments start at offset 32 on both backends.
        std::vector<ShaderRecordOverwrite> hitGroupSbtData;
        for (size_t i = 0; i < hitGroupProgramNames.size(); i++)
        {
            ShaderRecordOverwrite currSbtData = {};
            currSbtData.offset = 32;                 // After the 32-byte shader identifier / record header
            currSbtData.size = sizeof(testSbtValue); // uint32_t
            memcpy(currSbtData.data, &testSbtValue, sizeof(testSbtValue));
            hitGroupSbtData.push_back(currSbtData);
        }

        std::vector<std::vector<uint8_t>> recordStorage;
        std::vector<ShaderRecordData> recordData;
        if (useRecordData)
        {
            const size_t recordCount =
                recordSection == RecordSection::HitGroup ? hitGroupProgramNames.size() : missNames.size();
            REQUIRE(selectedRecordIndex < recordCount);
            recordStorage.resize(recordCount);
            recordData.resize(recordCount);
            for (size_t i = 0; i < recordCount; ++i)
            {
                // Give record zero a larger payload so selecting record one also verifies that the
                // backend addresses records using the maximum stride of the complete section.
                const size_t dataSize = i == 0 && recordCount > 1 ? 68 : sizeof(uint32_t);
                recordStorage[i].resize(dataSize);
                const uint32_t value = i == selectedRecordIndex ? testSbtValue : 0xBAADF00D;
                memcpy(recordStorage[i].data(), &value, sizeof(value));
                recordData[i] = {recordStorage[i].data(), recordStorage[i].size()};
            }
        }

        const ShaderRecordData* hitGroupRecordData =
            useRecordData && recordSection == RecordSection::HitGroup ? recordData.data() : nullptr;
        const ShaderRecordData* missShaderRecordData =
            useRecordData && recordSection == RecordSection::Miss ? recordData.data() : nullptr;

        RayTracingTestPipeline pipeline(
            device,
            filepath,
            {raygenName},
            hitGroupProgramNames,
            missNames,
            RayTracingPipelineFlags::None,
            useRecordData ? nullptr : hitGroupSbtData.data(),
            {},
            hitGroupRecordData,
            missShaderRecordData
        );
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

struct RayTracingSingleCustomGeometryTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<HitGroupProgramNames>& hitGroupProgramNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleCustomGeometryBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        RayTracingTestPipeline pipeline(device, filepath, {raygenName}, hitGroupProgramNames, missNames);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);

        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

struct RayTracingSingleTriangleMotionTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<const char*>& closestHitNames,
        const std::vector<const char*>& missNames,
        RayTracingPipelineFlags flags = RayTracingPipelineFlags::None
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleVertexMotionBLAS blas(device, queue);
        VertexMotionInstanceTLAS tlas(device, queue, blas.blas, 2);

        std::vector<HitGroupProgramNames> hitGroupProgramNames;
        for (const char* closestHitName : closestHitNames)
            hitGroupProgramNames.push_back({closestHitName, /*intersection=*/nullptr});

        RayTracingTestPipeline pipeline(device, filepath, {raygenName}, hitGroupProgramNames, missNames, flags);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

void checkQueryAndInvokeResult(ISlangBlob* resultBlob)
{
    const TestResult* testResult = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());
    CHECK_EQ(testResult->queryWasSuccess, 1);
    CHECK_EQ(testResult->invokeWasSuccess, 1);
}
} // namespace

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-rg", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderMakeQueryInvokeNOP", {"closestHitNOP"}, {"missNOP"});

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ch", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ms", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-rg", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderMakeQueryInvokeMiss",
        {"closestHitNOP"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ch", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeMiss"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ms", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeMiss", "missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-rg", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderTraceQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ch", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeHit", "closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ms", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP", "closestHitInvoke"},
        {"missMakeQueryInvokeHit"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-front-face", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindFrontFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-back-face", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindBackFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-custom", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleCustomGeometryTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindCustom",
        {{"closestHitNOP", /*anyhit=*/nullptr, "intersectionReportHitWithKind"}},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA/OptiX is disabled because it only supports getting the ray origin in world space.
// D3D12 is disabled due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-origin", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectOrigin",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayOrigin[0], 0.1f);
    CHECK_EQ(result->rayOrigin[1], 0.1f);
    CHECK_EQ(result->rayOrigin[2], 0.1f);
}

// Disabled under CUDA/OptiX and D3D12 due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-direction", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectDirection",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayDirection[0], 0.0f);
    CHECK_EQ(result->rayDirection[1], 0.0f);
    CHECK_EQ(result->rayDirection[2], 1.0f);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-hit", ALL | DontCreateDevice)
{
    // Limit the shader model to SM 6.6 for this test, since the NVAPI headers don't support MakeHit
    // for newer shader models.
    DeviceExtraOptions extraOptions;
    extraOptions.d3d12HighestShaderModel = 0x66; // SM 6.6
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics-make-hit",
        "rayGenShaderMakeQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-miss", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderMakeMiss", {"closestHitNOP"}, {"missInvoke"});

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-motion-miss", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    RayTracingSingleTriangleMotionTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderMakeMotionMiss",
        {"closestHitNOP"},
        {"missInvoke"},
        RayTracingPipelineFlags::EnableMotion
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-motion-hit", ALL | DontCreateDevice)
{
    // Limit the shader model to SM 6.6 for this test, since the NVAPI headers don't support MakeHit
    // for newer shader models.
    DeviceExtraOptions extraOptions;
    extraOptions.d3d12HighestShaderModel = 0x66; // SM 6.6
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    RayTracingSingleTriangleMotionTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics-make-hit",
        "rayGenShaderMakeMotionHit",
        {"closestHitInvoke"},
        {"missNOP"},
        RayTracingPipelineFlags::EnableMotion
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-trace-motion-ray", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    RayTracingSingleTriangleMotionTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderTraceMotionRay",
        {"closestHitInvoke"},
        {"missNOP"},
        RayTracingPipelineFlags::EnableMotion
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    // Check that it's a hit
    CHECK_EQ(result->queryWasSuccess, 1);
    CHECK_EQ(result->invokeWasSuccess, 1);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-ray-desc", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);
    if (device->getDeviceType() == DeviceType::D3D12 && device->hasFeature(Feature::SM_6_9) &&
        !device->hasCapability(Capability::hlsl_nvapi))
        SKIP("Skipping due to slang bug");

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderQueryRayDesc", {"closestHitNOP"}, {"missNOP"});

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-instance-id", ALL)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderQueryInstanceID", {"closestHitNOP"}, {"missNOP"});

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// Not available in Vulkan
// Disabled under D3D12 due to https://github.com/shader-slang/slang/issues/9509
GPU_TEST_CASE("ray-tracing-hitobject-set-and-get-shader-table-index", CUDA /*| D3D12*/)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    if (device->getDeviceType() == DeviceType::CUDA && device->getInfo().optixVersion < 90000)
        SKIP("Set and get shader table index is not supported for CUDA with OptiX version less than 9.0");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderSetAndGetShaderTableIndex",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-load-local-root-table-constant", D3D12 | CUDA)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadLocalRootTableConstant",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-load-local-root-table-constant-record-data", D3D12 | CUDA)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadLocalRootTableConstant",
        {"closestHitNOP"},
        {"missNOP"},
        true
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-load-variable-hit-record-data", D3D12 | CUDA)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadHitRecord1",
        {"closestHitNOP", "closestHitRecordNOP"},
        {"missNOP"},
        true,
        RayTracingSingleTriangleTest::RecordSection::HitGroup,
        1
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-load-variable-miss-record-data", D3D12 | CUDA)
{
    SKIP_D3D12_NVAPI_WITH_SM_6_9(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadMissRecord1",
        {"closestHitNOP"},
        {"missNOP", "missRecordNOP"},
        true,
        RayTracingSingleTriangleTest::RecordSection::Miss,
        1
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-get-shader-record-buffer-handle", Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderGetShaderRecordBufferHandle",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-get-shader-record-buffer-handle-record-data", Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderGetShaderRecordBufferHandle",
        {"closestHitNOP"},
        {"missNOP"},
        true
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-get-variable-hit-record-data", Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadHitRecord1",
        {"closestHitNOP", "closestHitRecordNOP"},
        {"missNOP"},
        true,
        RayTracingSingleTriangleTest::RecordSection::HitGroup,
        1
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-get-variable-miss-record-data", Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderLoadMissRecord1",
        {"closestHitNOP"},
        {"missNOP", "missRecordNOP"},
        true,
        RayTracingSingleTriangleTest::RecordSection::Miss,
        1
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}
