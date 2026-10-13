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

    // readBuffer() copies from the GPU buffer, so this checks the uploaded data.
    uint8_t result[6] = {};
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
    CHECK(std::memcmp(result, contents, sizeof(contents)) == 0);
}

// Shader writes to an upload buffer are visible to readBuffer() and to a write mapping, and unmapping
// keeps them apart from the values written through the mapping.
GPU_TEST_CASE("upload-buffer-shader-writes", Vulkan | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-upload-buffer", "writeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const float zeros[4] = {};
    BufferDesc desc = {};
    desc.size = sizeof(zeros);
    desc.elementSize = sizeof(float);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    desc.defaultState = ResourceState::UnorderedAccess;
    desc.memoryType = MemoryType::Upload;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, zeros, buffer.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    ShaderCursor cursor(passEncoder->bindPipeline(pipeline));
    cursor["target"].setBinding(buffer);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    float result[4] = {};
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
    CHECK(result[0] == 100.f);
    CHECK(result[3] == 103.f);

    float* data = nullptr;
    REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, (void**)&data));
    CHECK(data[1] == 101.f);
    data[0] = 7.f;
    REQUIRE_CALL(device->unmapBuffer(buffer));

    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
    CHECK(result[0] == 7.f);
    CHECK(result[1] == 101.f);
    CHECK(result[2] == 102.f);
    CHECK(result[3] == 103.f);
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

// A copy into an upload buffer is visible to a write mapping, and unmapping keeps it apart from the
// values written through the mapping.
GPU_TEST_CASE("upload-buffer-copy-writes", Vulkan | WGPU)
{
    const uint32_t initial[4] = {1, 2, 3, 4};
    const uint32_t copied[4] = {5, 6, 7, 8};

    BufferDesc desc = {};
    desc.size = sizeof(initial);
    desc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    desc.memoryType = MemoryType::Upload;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, initial, buffer.writeRef()));

    desc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> source;
    REQUIRE_CALL(device->createBuffer(desc, copied, source.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder->copyBuffer(buffer, 0, source, 0, sizeof(copied));
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    uint32_t* data = nullptr;
    REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, (void**)&data));
    CHECK(data[1] == 6);
    data[0] = 9;
    REQUIRE_CALL(device->unmapBuffer(buffer));

    uint32_t result[4] = {};
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
    CHECK(result[0] == 9);
    CHECK(result[1] == 6);
    CHECK(result[2] == 7);
    CHECK(result[3] == 8);
}

// An imported upload buffer without MapWrite usage is mapped through a CPU copy that starts with the
// buffer's contents.
GPU_TEST_CASE("upload-buffer-import", WGPU)
{
    const uint32_t contents[4] = {1, 2, 3, 4};
    BufferDesc desc = {};
    desc.size = sizeof(contents);
    desc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> original;
    REQUIRE_CALL(device->createBuffer(desc, contents, original.writeRef()));
    NativeHandle handle;
    REQUIRE_CALL(original->getNativeHandle(&handle));

    BufferDesc uploadDesc = desc;
    uploadDesc.memoryType = MemoryType::Upload;
    ComPtr<IBuffer> imported;
    REQUIRE_CALL(device->createBufferFromNativeHandle(handle, uploadDesc, imported.writeRef()));

    uint32_t* data = nullptr;
    REQUIRE_CALL(device->mapBuffer(imported, CpuAccessMode::Write, (void**)&data));
    CHECK(data[2] == 3);
    data[0] = 9;
    REQUIRE_CALL(device->unmapBuffer(imported));

    uint32_t result[4] = {};
    REQUIRE_CALL(device->readBuffer(original, 0, sizeof(result), result));
    CHECK(result[0] == 9);
    CHECK(result[3] == 4);
}
