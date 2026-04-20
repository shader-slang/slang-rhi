#include "testing.h"
#include <random>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("bind-pointers-single-copy", Vulkan | CUDA | Metal)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-copy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;

    // Generate random data for 'numberCount' uint32s
    std::vector<uint8_t> data;
    std::mt19937 rng(124112);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(numberCount * 4);
    for (auto& byte : data)
        byte = (uint8_t)dist(rng);

    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Create source buffer
    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)data.data(), src.writeRef()));

    // Create empty dest buffer
    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));


    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["src"].setData(src->getDeviceAddress());
        shaderCursor["dst"].setData(dst->getDeviceAddress());

        passEncoder->dispatchCompute(numberCount / 32, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, dst, std::span<uint8_t>(data));
}

GPU_TEST_CASE("bind-pointers-parameter-block", Vulkan | CUDA | Metal)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-param-block", "copyMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;

    std::vector<uint8_t> data;
    std::mt19937 rng(99887);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(numberCount * 4);
    for (auto& byte : data)
        byte = (uint8_t)dist(rng);

    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)data.data(), src.writeRef()));

    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["copyParams"]["src"].setData(src->getDeviceAddress());
        shaderCursor["copyParams"]["dst"].setData(dst->getDeviceAddress());

        passEncoder->dispatchCompute(numberCount / 32, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, dst, std::span<uint8_t>(data));
}

GPU_TEST_CASE("bind-pointers-parameter-block-mixed", Vulkan | CUDA | Metal)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-param-block", "mixedMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;
    const uint32_t scaleA = 3;
    const uint32_t scaleB = 7;

    std::vector<uint32_t> srcData(numberCount);
    std::mt19937 rng(55443);
    std::uniform_int_distribution<uint32_t> dist(1, 1000);
    for (auto& v : srcData)
        v = dist(rng);

    std::vector<uint32_t> expected(numberCount);
    for (int i = 0; i < numberCount; i++)
        expected[i] = srcData[i] * scaleA + scaleB;

    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)srcData.data(), src.writeRef()));

    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["mixedParams"]["scaleA"].setData(scaleA);
        shaderCursor["mixedParams"]["src"].setData(src->getDeviceAddress());
        shaderCursor["mixedParams"]["scaleB"].setData(scaleB);
        shaderCursor["mixedParams"]["dst"].setData(dst->getDeviceAddress());

        passEncoder->dispatchCompute(numberCount / 32, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(
        device,
        dst,
        std::span<uint8_t>((uint8_t*)expected.data(), expected.size() * sizeof(uint32_t))
    );
}

GPU_TEST_CASE("bind-pointers-intermediate-copy-nosync", Vulkan | CUDA | Metal)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-copy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;

    // Generate random data for 'numberCount' uint32s
    std::vector<uint8_t> data;
    std::mt19937 rng(124112);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(numberCount * 4);
    for (auto& byte : data)
        byte = (uint8_t)dist(rng);

    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Create source buffer
    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)data.data(), src.writeRef()));

    // Create empty dest buffer
    ComPtr<IBuffer> tmp;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, tmp.writeRef()));

    // Create empty dest buffer
    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));


    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["src"].setData(src->getDeviceAddress());
            shaderCursor["dst"].setData(tmp->getDeviceAddress());
            passEncoder->dispatchCompute(numberCount / 32, 1, 1);
            passEncoder->end();
        }

        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["src"].setData(tmp->getDeviceAddress());
            shaderCursor["dst"].setData(dst->getDeviceAddress());
            passEncoder->dispatchCompute(numberCount / 32, 1, 1);
            passEncoder->end();
        }

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    if (device->getDeviceType() == DeviceType::CUDA || device->getDeviceType() == DeviceType::Metal)
    {
        // CUDA serializes dispatches within a stream.
        // Metal's tracked hazard tracking mode automatically synchronizes resources between
        // separate command encoders within the same command buffer.
        compareComputeResult(device, dst, std::span<uint8_t>(data));
    }
    else
    {
        // GFX APIs like Vulkan and D3D12 require explicit synchronization between dispatches,
        // which isn't done automatically for pointer-accessed resources, so we'd expect race
        // conditions without barriers.
        compareComputeResult(device, dst, std::span<uint8_t>(data), true);
    }
}
