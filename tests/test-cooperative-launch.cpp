#include "testing.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct ErrorCapture : IDebugCallback
{
    std::string errors;

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        SLANG_UNUSED(source);
        if (type == DebugMessageType::Error)
            errors += message;
    }
};

ComPtr<IComputePipeline> createCooperativePipeline(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cooperative-launch", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    return pipeline;
}

ComPtr<IBuffer> createGroupBuffer(IDevice* device, uint32_t elementCount)
{
    std::vector<uint32_t> initialData(elementCount, 0);
    BufferDesc bufferDesc = {};
    bufferDesc.size = elementCount * sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData.data(), buffer.writeRef()));
    return buffer;
}

void dispatchGridSum(IDevice* device, IComputePipeline* pipeline, IBuffer* buffer, uint32_t groupCount)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor cursor(rootObject);
    cursor["buffer"].setBinding(buffer);
    cursor["groupCount"].setData(groupCount);
    passEncoder->dispatchCompute(groupCount, 1, 1);
    passEncoder->end();
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

uint32_t readUint(IDevice* device, IBuffer* buffer, uint32_t index)
{
    uint32_t value = 0;
    REQUIRE_CALL(device->readBuffer(buffer, index * sizeof(uint32_t), sizeof(uint32_t), &value));
    return value;
}

} // namespace

// A grid-wide barrier gives the exact sum at 1 group, half and the maximum co-resident group count.
GPU_TEST_CASE("cooperative-launch-grid-sum", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");

    ComPtr<IComputePipeline> pipeline = createCooperativePipeline(device);
    uint32_t maxGroupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(&maxGroupCount));
    REQUIRE(maxGroupCount > 0);

    ComPtr<IBuffer> buffer = createGroupBuffer(device, maxGroupCount + 1);
    uint32_t groupCounts[] = {1, std::max(maxGroupCount / 2, 1u), maxGroupCount};
    for (uint32_t groupCount : groupCounts)
    {
        CAPTURE(groupCount);
        dispatchGridSum(device, pipeline, buffer, groupCount);
        uint32_t expected = groupCount * (groupCount + 1) / 2;
        CHECK_EQ(readUint(device, buffer, groupCount), expected);
    }
}

// A pipeline without the flag reports no cooperative group limit.
GPU_TEST_CASE("cooperative-launch-limit-without-flag", CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    uint32_t maxGroupCount = 1;
    CHECK_EQ(pipeline->getMaxCooperativeThreadGroupCount(&maxGroupCount), SLANG_E_NOT_AVAILABLE);
    CHECK_EQ(maxGroupCount, 0);
}

// A dispatch with more groups than can be co-resident reports an error and is not launched.
GPU_TEST_CASE("cooperative-launch-over-limit", CUDA | DontCreateDevice)
{
    ErrorCapture callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    ComPtr<IDevice> testDevice = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(testDevice);
    if (!testDevice->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");

    ComPtr<IComputePipeline> pipeline = createCooperativePipeline(testDevice);
    uint32_t maxGroupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(&maxGroupCount));

    uint32_t groupCount = maxGroupCount + 1;
    ComPtr<IBuffer> buffer = createGroupBuffer(testDevice, groupCount + 1);
    REQUIRE(callback.errors.empty());
    dispatchGridSum(testDevice, pipeline, buffer, groupCount);
    CHECK(callback.errors.find("exceeds the limit") != std::string::npos);
    CHECK_EQ(readUint(testDevice, buffer, 0), 0);
    CHECK_EQ(readUint(testDevice, buffer, groupCount), 0);
}

// Backends without a forward-progress guarantee across groups do not report the feature and refuse the flag.
GPU_TEST_CASE("cooperative-launch-not-supported", D3D11 | D3D12 | Vulkan | Metal | CPU | WGPU | DontCreateDevice)
{
    ErrorCapture callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    ComPtr<IDevice> testDevice = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(testDevice);
    CHECK_FALSE(testDevice->hasFeature(Feature::CooperativeLaunch));

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(testDevice, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    ComPtr<IComputePipeline> pipeline;
    CHECK_EQ(testDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()), SLANG_E_NOT_AVAILABLE);
    CHECK_FALSE(pipeline);
    CHECK(callback.errors.find("Cooperative launch is not supported") != std::string::npos);
}
