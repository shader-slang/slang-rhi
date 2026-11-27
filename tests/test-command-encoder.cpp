#include "testing.h"

#include "shader-object.h"

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

    // Uses the dedicated test-binding-reuse shader (res[i] = a[i] + b[i] for
    // i < count, with a, b, res, count living in a nested ParameterBlock). The test
    // dispatches several times from one
    // compute pass while mutating bindings and nested data between dispatches, then
    // validates that every dispatch used the parameter values that were current when
    // it was recorded. This guards the reuse of recorded binding data across
    // dispatches: a miss here means a mutation was not picked up.

    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-binding-reuse", "addkernel", program.writeRef(), &reflection));
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
    ComPtr<IBuffer> res5;
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res1.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res2.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res3.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res4.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res5.writeRef()));

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
        // Hold a strong reference: binding a different root below makes the
        // encoder drop the previous one, and the pointers into it (cursor, block,
        // rootObject) would dangle without this.
        ComPtr<IShaderObject> shaderObject(computePass->bindPipeline(pipeline));
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];

        // Dispatch 1: a = A1, res = res1, count = 32.
        setBindings(block, bufA1, res1, kCount);
        computePass->dispatchCompute(1, 1, 1);

        // The folded version advances on a content change and stays put on a
        // redundant write - this is the key the recording-side reuse relies on.
        // The public pointer may be a debug-layer wrapper, so unwrap it first.
        auto* rootObject = getUnderlyingRootShaderObject(shaderObject);
        uint64_t version0 = rootObject->getRecursiveVersion();
        setBindings(block, bufA2, res2, 16);
        uint64_t version1 = rootObject->getRecursiveVersion();
        CHECK(version1 != version0);
        setBindings(block, bufA2, res2, 16);
        CHECK(rootObject->getRecursiveVersion() == version1);

        // Dispatch 2: mutate a binding and the nested data between dispatches.
        computePass->dispatchCompute(1, 1, 1);

        // Dispatch 2b: re-binding the pipeline must return the previously created
        // root object, and a dispatch with no parameter writes at all must use the
        // bindings carried over from dispatch 2 - the res2 comparison validates the
        // carried binding values, and a regression that binds a fresh, empty root
        // leaves res2 holding its initial zeros instead.
        CHECK(computePass->bindPipeline(pipeline) == shaderObject);
        computePass->dispatchCompute(1, 1, 1);

        // Dispatch 3: revert to the dispatch 1 values (content differs from the
        // previously recorded state, so this must also re-record).
        setBindings(block, bufA1, res3, kCount);
        computePass->dispatchCompute(1, 1, 1);

        // The public getFieldByIndex must reject invalid cursors and out-of-range
        // indices (the reflection layer indexes its field array unchecked).
        {
            ShaderCursor content = block.getDereferenced();
            ShaderCursor field;
            CHECK(SLANG_FAILED(content.getFieldByIndex(-1, field)));
            CHECK(SLANG_FAILED(content.getFieldByIndex(1000, field)));
            CHECK(SLANG_SUCCEEDED(content.getFieldByIndex(0, field)));
            ShaderCursor invalid;
            CHECK(SLANG_FAILED(invalid.getFieldByIndex(0, field)));
        }

        // The version contract of the object's data paths. The offsets are chosen
        // independently of the backend's data-size conventions (the reflected
        // stride and the allocated size need not agree).
        {
            uint8_t scratch[16] = {};
            IShaderObject* subObject = block.getDereferenced().m_baseObject;
            // A setData offset far past the end of the object's data is rejected.
            CHECK(
                SLANG_FAILED(subObject->setData(ShaderOffset{.uniformOffset = 0x7FFFFFF0}, scratch, sizeof(scratch)))
            );
            // A zero-length write is a no-op and must not advance the version.
            uint64_t versionBefore = rootObject->getRecursiveVersion();
            REQUIRE_CALL(subObject->setData(ShaderOffset{.uniformOffset = 0}, scratch, 0));
            CHECK(rootObject->getRecursiveVersion() == versionBefore);
        }

        // setSpecializationArgs advances the version even when its arguments fail
        // validation (the bump is unconditional and precedes validation).
        {
            uint64_t versionBefore = rootObject->getRecursiveVersion();
            slang::SpecializationArg bogusArg = {};
            ShaderOffset bogusOffset = {};
            bogusOffset.bindingRangeIndex = 0x7FFFFFFF;
            (void)shaderObject->setSpecializationArgs(bogusOffset, &bogusArg, 1);
            CHECK(rootObject->getRecursiveVersion() != versionBefore);
        }

        // A raw setData that clobbers a bound resource slot's derived bytes followed
        // by a redundant re-bind must advance the version: the re-bind's hook repair
        // is a content change even though the slot is identical. The snapshot is
        // taken after the clobber (whose own version bump is not what is under
        // test), and only backends with a set-binding hook can repair, so the
        // discriminating check is gated to them - elsewhere a redundant re-bind is a
        // true no-op.
        {
            uint8_t garbage[16] = {};
            block["a"].setData(garbage, sizeof(garbage));
            uint64_t versionAfterClobber = rootObject->getRecursiveVersion();
            block["a"].setBinding(bufA1);
            if (device->getDeviceType() == DeviceType::CUDA || device->getDeviceType() == DeviceType::CPU)
                CHECK(rootObject->getRecursiveVersion() != versionAfterClobber);
        }

        // Dispatch 4: the two-argument bindPipeline adopts a caller-provided root
        // object, and the one-argument form returns it afterwards.
        ComPtr<IShaderObject> root2;
        REQUIRE_CALL(device->createRootShaderObject(program, root2.writeRef()));
        {
            ShaderCursor root2Cursor(root2);
            ShaderCursor root2Block = root2Cursor["addKernelData"];
            root2Block["a"].setBinding(bufA2);
            root2Block["b"].setBinding(bufB);
            root2Block["res"].setBinding(res5);
            root2Block["count"].setData(kCount);
        }
        computePass->bindPipeline(pipeline, root2);
        computePass->dispatchCompute(1, 1, 1);
        // Compare through the unwrapping helper: the debug layer keeps its own
        // wrapper identity, so raw pointer comparison only matches without it.
        CHECK(
            getUnderlyingRootShaderObject(computePass->bindPipeline(pipeline)) == getUnderlyingRootShaderObject(root2)
        );

        computePass->end();
        queue->submit(commandEncoder->finish());
    }

    // Record a second command buffer with a fresh encoder: the reuse cache is
    // per-encoder and starts empty, so this exercises that no state from the first
    // recording session leaks into it.
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
    std::vector<float> expected4(kCount);
    std::vector<float> expected5(kCount);
    for (uint32_t i = 0; i < kCount; i++)
    {
        expected1[i] = dataA1[i] + dataB[i];
        expected2[i] = (i < 16) ? dataA2[i] + dataB[i] : 0.0f;
        expected3[i] = dataA1[i] + dataB[i];
        expected4[i] = (i < 8) ? dataA2[i] + dataB[i] : 0.0f;
        expected5[i] = dataA2[i] + dataB[i];
    }
    compareComputeResult(device, res1, std::span<float>(expected1));
    compareComputeResult(device, res2, std::span<float>(expected2));
    compareComputeResult(device, res3, std::span<float>(expected3));
    compareComputeResult(device, res4, std::span<float>(expected4));
    compareComputeResult(device, res5, std::span<float>(expected5));
    // The zero tails of expected2 and expected4 hold because the buffers are
    // initialized with zeros and the kernel writes exactly `count` elements.
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
    REQUIRE_CALL(loadAndLinkProgram(device, "test-binding-reuse", "addkernel", program.writeRef(), &reflection));
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

    // The new contents are copied from this buffer between the two passes, so it
    // needs the copy-source usage.
    BufferDesc newADesc = bufferDesc;
    newADesc.usage = BufferUsage::ShaderResource | BufferUsage::CopySource;
    REQUIRE_CALL(device->createBuffer(newADesc, dataNewA, bufNewA.writeRef()));

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
        // Re-binding the same pipeline within one encoder returns the previously
        // created root object.
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

GPU_TEST_CASE("compute-binding-reuse-entry-point-mutation", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    // Guards the entry-point half of the version fold: an entry-point parameter is
    // not part of any ParameterBlock, and only RootShaderObject's fold over entry
    // point objects makes its mutation visible to the recording-side reuse. A
    // mutation between two dispatches of one pass must invalidate the recorded
    // binding data, or the second dispatch would run with the stale value.
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-binding-reuse-ep", "addkernel", program.writeRef(), &reflection));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t kCount = 32;
    float dataA[kCount];
    float dataB[kCount];
    float zeros[kCount];
    for (uint32_t i = 0; i < kCount; i++)
    {
        dataA[i] = (float)i;
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
    REQUIRE_CALL(device->createBuffer(readDesc, dataA, bufA.writeRef()));
    REQUIRE_CALL(device->createBuffer(readDesc, dataB, bufB.writeRef()));

    BufferDesc writeDesc = bufferDesc;
    writeDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> res1;
    ComPtr<IBuffer> res2;
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res1.writeRef()));
    REQUIRE_CALL(device->createBuffer(writeDesc, zeros, res2.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();

    {
        auto computePass = commandEncoder->beginComputePass();
        auto shaderObject = computePass->bindPipeline(pipeline);
        ShaderCursor cursor(shaderObject);
        ShaderCursor block = cursor["addKernelData"];
        block["a"].setBinding(bufA);
        block["b"].setBinding(bufB);
        block["res"].setBinding(res1);
        block["count"].setData(kCount);

        ComPtr<IShaderObject> entryPoint;
        REQUIRE_CALL(shaderObject->getEntryPoint(0, entryPoint.writeRef()));
        ShaderCursor entryPointCursor(entryPoint);
        REQUIRE_CALL(entryPointCursor["offset"].setData(1.0f));

        computePass->dispatchCompute(1, 1, 1);

        // The root's folded version must advance when an entry-point parameter
        // changes, even though the root's own and sub-object versions do not.
        auto* rootObject = getUnderlyingRootShaderObject(shaderObject);
        uint64_t version0 = rootObject->getRecursiveVersion();
        REQUIRE_CALL(entryPointCursor["offset"].setData(100.0f));
        CHECK(rootObject->getRecursiveVersion() != version0);

        block["res"].setBinding(res2);
        computePass->dispatchCompute(1, 1, 1);
        computePass->end();
    }

    // Only the execution is defective on Vulkan: recording and the host-side
    // version assertions above run on every backend, but with an entry-point
    // uniform parameter present the very first dispatch (the full-bind path,
    // before any reuse is involved) reads the parameter block's count as 0.
    if (device->getDeviceType() == DeviceType::Vulkan)
        SKIP(
            "vulkan: entry-point uniform parameters break ParameterBlock data binding "
            "(pre-existing, tracked in slang-rhi#915)"
        );

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    std::vector<float> expected1(kCount);
    std::vector<float> expected2(kCount);
    for (uint32_t i = 0; i < kCount; i++)
    {
        expected1[i] = dataA[i] + dataB[i] + 1.0f;
        expected2[i] = dataA[i] + dataB[i] + 100.0f;
    }
    compareComputeResult(device, res1, std::span<float>(expected1));
    compareComputeResult(device, res2, std::span<float>(expected2));
}
