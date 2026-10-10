#include "testing.h"

#include <cstring>

using namespace rhi;
using namespace rhi::testing;

// Buffers in upload memory are written by mapping and read directly by shaders. Each unmap makes
// the new contents visible to later submissions.
GPU_TEST_CASE("upload-buffer-shader-input", D3D12 | Vulkan | Metal | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-upload-buffer", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t count = 4;

    BufferDesc inputDesc = {};
    inputDesc.size = count * sizeof(float);
    inputDesc.elementSize = sizeof(float);
    inputDesc.usage = BufferUsage::ShaderResource;
    inputDesc.memoryType = MemoryType::Upload;
    ComPtr<IBuffer> input;
    REQUIRE_CALL(device->createBuffer(inputDesc, nullptr, input.writeRef()));

    BufferDesc outputDesc = {};
    outputDesc.size = count * sizeof(float);
    outputDesc.elementSize = sizeof(float);
    outputDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    outputDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> output;
    REQUIRE_CALL(device->createBuffer(outputDesc, nullptr, output.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    for (float base : {1.f, 10.f})
    {
        float* data = nullptr;
        REQUIRE_CALL(device->mapBuffer(input, CpuAccessMode::Write, (void**)&data));
        for (uint32_t i = 0; i < count; ++i)
            data[i] = base + float(i);
        REQUIRE_CALL(device->unmapBuffer(input));

        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(pipeline));
        cursor["input"].setBinding(input);
        cursor["output"].setBinding(output);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        compareComputeResult(
            device,
            output,
            makeArray<float>(2.f * base, 2.f * (base + 1.f), 2.f * (base + 2.f), 2.f * (base + 3.f))
        );
    }
}

// Buffer sizes that are not a multiple of 4 bytes can be mapped, written and read back.
GPU_TEST_CASE("upload-buffer-unaligned-size", D3D12 | Vulkan | Metal | WGPU)
{
    const uint8_t contents[6] = {1, 2, 3, 4, 5, 6};

    BufferDesc desc = {};
    desc.size = sizeof(contents);
    desc.usage = BufferUsage::CopySource;
    desc.memoryType = MemoryType::Upload;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, nullptr, buffer.writeRef()));

    void* data = nullptr;
    REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, &data));
    std::memcpy(data, contents, sizeof(contents));
    REQUIRE_CALL(device->unmapBuffer(buffer));

    BufferDesc copyDesc = desc;
    copyDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    copyDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> copy;
    REQUIRE_CALL(device->createBuffer(copyDesc, contents, copy.writeRef()));

    uint8_t result[6] = {};
    REQUIRE_CALL(device->readBuffer(copy, 0, sizeof(result), result));
    CHECK(std::memcmp(result, contents, sizeof(contents)) == 0);
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
    CHECK(std::memcmp(result, contents, sizeof(contents)) == 0);
}

// Reads at offsets and sizes that are not multiples of 4 bytes return the requested bytes.
GPU_TEST_CASE("buffer-read-unaligned", Vulkan | WGPU)
{
    uint8_t contents[11];
    for (uint8_t i = 0; i < sizeof(contents); ++i)
        contents[i] = uint8_t(i + 1);

    BufferDesc desc = {};
    desc.size = sizeof(contents);
    desc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, contents, buffer.writeRef()));

    for (Offset offset : {1, 2, 3, 5})
    {
        for (Size size : {Size(1), Size(3), Size(sizeof(contents) - offset)})
        {
            uint8_t result[sizeof(contents)] = {};
            REQUIRE_CALL(device->readBuffer(buffer, offset, size, result));
            CAPTURE(offset);
            CAPTURE(size);
            CHECK(std::memcmp(result, contents + offset, size) == 0);
        }
    }
}
