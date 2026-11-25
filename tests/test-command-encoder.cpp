#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("command-encoder-without-finish", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder.setNull();
}

GPU_TEST_CASE("command-encoder-labels", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);

    // Create command encoder with label.
    CommandEncoderDesc encoderDesc;
    encoderDesc.label = "test-encoder";
    auto encoder = queue->createCommandEncoder(encoderDesc);
    REQUIRE(encoder);

    // Verify encoder label via getDesc().
    const CommandEncoderDesc& retrievedEncoderDesc = encoder->getDesc();
    CHECK(retrievedEncoderDesc.label != nullptr);
    CHECK(std::strcmp(retrievedEncoderDesc.label, "test-encoder") == 0);

    // Finish with command buffer label.
    CommandBufferDesc bufferDesc;
    bufferDesc.label = "test-command-buffer";
    auto commandBuffer = encoder->finish(bufferDesc);
    REQUIRE(commandBuffer);

    // Verify command buffer label via getDesc().
    const CommandBufferDesc& retrievedBufferDesc = commandBuffer->getDesc();
    CHECK(retrievedBufferDesc.label != nullptr);
    CHECK(std::strcmp(retrievedBufferDesc.label, "test-command-buffer") == 0);
}

GPU_TEST_CASE("compute-binding-reuse-mutation", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    // Uses the addkernel shader: res[i] = a[i] + b[i] for i < count, with all
    // parameters living in a nested ParameterBlock. The test dispatches several
    // times from one compute pass while mutating bindings and nested data between
    // dispatches, then validates that every dispatch used the parameter values that
    // were current when it was recorded. This guards the reuse of recorded binding
    // data across dispatches: a miss here means a mutation was not picked up.
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-benchmark-command", "addkernel", program.writeRef(), &reflection));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t kCount = 32;
    float dataA1[kCount];
    float dataA2[kCount];
    float dataB[kCount];
    float zeros[kCount];
    for (uint32_t i = 0; i < kCount; i++)
    {
        dataA1[i] = (float)i;
        dataA2[i] = 1000.0f + (float)i;
        dataB[i] = 10.0f * (float)i;
        zeros[i] = 0.0f;
    }

    BufferDesc bufferDesc = {};
    bufferDesc.size = kCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    BufferDesc readDesc = bufferDesc;
    readDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> bufA1;
    ComPtr<IBuffer> bufA2;
    ComPtr<IBuffer> bufB;
    REQUIRE_CALL(device->createBuffer(readDesc, dataA1, bufA1.writeRef()));
    REQUIRE_CALL(device->createBuffer(readDesc, dataA2, bufA2.writeRef()));
    REQUIRE_CALL(device->createBuffer(readDesc, dataB, bufB.writeRef()));

    BufferDesc writeDesc = bufferDesc;
    writeDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> res1;
    ComPtr<IBuffer> res2;
    ComPtr<IBuffer> res3;
    ComPtr<IBuffer> res4;
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res1.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res2.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res3.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res4.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    auto setBindings = [&](ShaderCursor& block, IBuffer* a, IBuffer* res, int count)
    {
        block["a"].setBinding(a);
        block["b"].setBinding(bufB);
        block["res"].setBinding(res);
        block["count"].setData(count);
    };

    {
        auto commandEncoder = queue->createCommandEncoder();
        auto computePass = commandEncoder->beginComputePass();
        auto shaderObject = computePass->bindPipeline(pipeline);
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];

        // Dispatch 1: a = A1, res = res1, count = 32.
        setBindings(block, bufA1, res1, kCount);
        computePass->dispatchCompute(1, 1, 1);

        // Dispatch 2: mutate a binding and the nested data between dispatches.
        setBindings(block, bufA2, res2, 16);
        computePass->dispatchCompute(1, 1, 1);

        // Dispatch 3: revert to the dispatch 1 values (content differs from the
        // previously recorded version, so this must also re-record).
        setBindings(block, bufA1, res3, kCount);
        computePass->dispatchCompute(1, 1, 1);

        computePass->end();
        queue->submit(commandEncoder->finish());
    }

    // Record a second command buffer with a fresh encoder, exercising that no state
    // from the first recording session leaks into it.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto computePass = commandEncoder->beginComputePass();
        auto shaderObject = computePass->bindPipeline(pipeline);
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];
        setBindings(block, bufA2, res4, 8);
        computePass->dispatchCompute(1, 1, 1);
        computePass->end();
        queue->submit(commandEncoder->finish());
    }

    queue->waitOnHost();

    std::vector<float> expected1(kCount);
    std::vector<float> expected2(kCount);
    std::vector<float> expected3(kCount);
    for (uint32_t i = 0; i < kCount; i++)
    {
        expected1[i] = dataA1[i] + dataB[i];
        expected2[i] = (i < 16) ? dataA2[i] + dataB[i] : 0.0f;
        expected3[i] = dataA1[i] + dataB[i];
    }
    compareComputeResult(device, res1, std::span<float>(expected1));
    compareComputeResult(device, res2, std::span<float>(expected2));
    compareComputeResult(device, res3, std::span<float>(expected3));
}

GPU_TEST_CASE("compute-binding-reuse-copy-hazard", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    // Guards the interaction of binding-data reuse with resource state transitions:
    // a copy between two compute dispatches moves a bound buffer into a copy state,
    // and the second dispatch reuses the first dispatch's binding data (identical
    // bindings, so the folded version is unchanged). The dispatch must still see the
    // buffer in its binding state and read the post-copy contents.
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-benchmark-command", "addkernel", program.writeRef(), &reflection));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t kCount = 32;
    float dataA[kCount];
    float dataNewA[kCount];
    float dataB[kCount];
    float zeros[kCount];
    for (uint32_t i = 0; i < kCount; i++)
    {
        dataA[i] = (float)i;
        dataNewA[i] = 500.0f + (float)i;
        dataB[i] = 10.0f * (float)i;
        zeros[i] = 0.0f;
    }

    BufferDesc bufferDesc = {};
    bufferDesc.size = kCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    BufferDesc readDesc = bufferDesc;
    readDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> bufA;
    ComPtr<IBuffer> bufB;
    ComPtr<IBuffer> bufNewA;
    REQUIRE_CALL(device->createBuffer(readDesc, dataA, bufA.writeRef()));
    REQUIRE_CALL(device->createBuffer(readDesc, dataB, bufB.writeRef()));
    REQUIRE_CALL(device->createBuffer(readDesc, dataNewA, bufNewA.writeRef()));

    BufferDesc writeDesc = bufferDesc;
    writeDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> res;
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();

    {
        auto computePass = commandEncoder->beginComputePass();
        auto shaderObject = computePass->bindPipeline(pipeline);
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];
        block["a"].setBinding(bufA);
        block["b"].setBinding(bufB);
        block["res"].setBinding(res);
        block["count"].setData(kCount);
        computePass->dispatchCompute(1, 1, 1);
        computePass->end();
    }

    // Overwrite the bound input buffer between dispatches, leaving it in a copy
    // state on backends that track resource states.
    commandEncoder->copyBuffer(bufA, 0, bufNewA, 0, kCount * sizeof(float));

    {
        auto computePass = commandEncoder->beginComputePass();
        auto shaderObject = computePass->bindPipeline(pipeline);
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];
        block["a"].setBinding(bufA);
        block["b"].setBinding(bufB);
        block["res"].setBinding(res);
        block["count"].setData(kCount);
        computePass->dispatchCompute(1, 1, 1);
        computePass->end();
    }

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    std::vector<float> expected(kCount);
    for (uint32_t i = 0; i < kCount; i++)
        expected[i] = dataNewA[i] + dataB[i];
    compareComputeResult(device, res, std::span<float>(expected));
}
