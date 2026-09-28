#include "testing.h"

#include <cstring>

using namespace rhi;
using namespace rhi::testing;

// Check the bytes written through the public binding API as well as GPU execution. Running this
// test with old and new Slang libraries exercises both CUDA buffer representations.
static void checkBufferBinding(ShaderCursor cursor, IBuffer* buffer, BufferRange range, size_t elementSize)
{
    auto layout = cursor.getTypeLayout();
    if (auto counter = layout->getExplicitCounter())
        layout = counter->getTypeLayout();
    size_t size = layout->getSize();
    REQUIRE((size == 8 || size == 16));

    REQUIRE_CALL(cursor.setBinding(Binding(buffer, range)));
    uint64_t words[2] = {};
    auto data = static_cast<const uint8_t*>(cursor.m_baseObject->getRawData()) + cursor.m_offset.uniformOffset;
    memcpy(words, data, size);
    CHECK(words[0] == buffer->getDeviceAddress() + range.offset);
    if (size == 16)
        CHECK(words[1] == range.size / elementSize);

    REQUIRE_CALL(cursor.setBinding(Binding(static_cast<IBuffer*>(nullptr))));
    memcpy(words, data, size);
    CHECK(words[0] == 0);
    if (size == 16)
        CHECK(words[1] == 0);
    REQUIRE_CALL(cursor.setBinding(Binding(buffer, range)));
}

GPU_TEST_CASE("cuda-buffer-abi", CUDA)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-cuda-buffer-abi", "computeMain", program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    uint32_t values[] = {10, 20, 30, 40};
    BufferDesc desc = {};
    desc.size = sizeof(values);
    desc.elementSize = sizeof(uint32_t);
    desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> structured;
    REQUIRE_CALL(device->createBuffer(desc, values, structured.writeRef()));
    desc.elementSize = 0;
    ComPtr<IBuffer> bytes;
    REQUIRE_CALL(device->createBuffer(desc, values, bytes.writeRef()));
    ComPtr<IBuffer> output;
    REQUIRE_CALL(device->createBuffer(desc, nullptr, output.writeRef()));
    ComPtr<IBuffer> appended;
    REQUIRE_CALL(device->createBuffer(desc, nullptr, appended.writeRef()));
    desc.size = sizeof(uint32_t);
    desc.elementSize = sizeof(uint32_t);
    uint32_t counterValue = 0;
    ComPtr<IBuffer> appendCounter;
    REQUIRE_CALL(device->createBuffer(desc, &counterValue, appendCounter.writeRef()));
    counterValue = 1;
    ComPtr<IBuffer> consumeCounter;
    REQUIRE_CALL(device->createBuffer(desc, &counterValue, consumeCounter.writeRef()));

    ComPtr<IShaderObject> root;
    REQUIRE_CALL(device->createRootShaderObject(program, root.writeRef()));
    ShaderCursor cursor(root);
    for (uint32_t i = 0; i < 3; ++i)
    {
        auto buffers = i == 0 ? cursor["globals"] : cursor["params"]["buffers"][i - 1];
        const char* names[] = {"input", "mutableInput", "bytes", "mutableBytes"};
        const char* sentinels[] = {"inputSentinel", "mutableInputSentinel", "bytesSentinel", "mutableBytesSentinel"};
        for (uint32_t j = 0; j < 4; ++j)
        {
            // Set the following scalar first: an unconditional legacy size write would corrupt it.
            REQUIRE_CALL(buffers[sentinels[j]].setData(uint32_t(100)));
            checkBufferBinding(buffers[names[j]], j < 2 ? structured.get() : bytes.get(), {i * 4, 4}, j < 2 ? 4 : 1);
        }
    }
    REQUIRE_CALL(cursor["output"].setBinding(output));
    checkBufferBinding(cursor["appended"], appended, {0, 16}, 1);
    checkBufferBinding(cursor["appended"].getExplicitCounter(), appendCounter, {0, 4}, 4);
    checkBufferBinding(cursor["consumed"], structured, {0, 16}, 4);
    checkBufferBinding(cursor["consumed"].getExplicitCounter(), consumeCounter, {0, 4}, 4);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    pass->bindPipeline(pipeline, root);
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    queue->submit(encoder->finish());
    queue->waitOnHost();
    compareComputeResult(device, output, makeArray<uint32_t>(440, 480, 520, 10));
    ComPtr<ISlangBlob> result;
    REQUIRE_CALL(device->readBuffer(appended, 0, 4, result.writeRef()));
    CHECK(*static_cast<const uint32_t*>(result->getBufferPointer()) == 99);
}
