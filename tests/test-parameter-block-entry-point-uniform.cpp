#include "testing.h"

using namespace rhi;
using namespace testing;

// A global `ParameterBlock` and an ordinary entry-point `uniform` must both reach the shader at once:
// the uniform becomes a push constant, and on Vulkan that has to not disturb the descriptor set the
// parameter block binds through. See shader-slang/slang#12349.
GPU_TEST_CASE("parameter-block-entry-point-uniform", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-parameter-block-entry-point-uniform",
        "computeMain",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t initialData[1] = {0};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));

    ShaderCursor(rootObject)["output"]["count"].setBinding(buffer);
    ShaderCursor(rootObject->getEntryPoint(0))["width"].setData(uint32_t(1));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<uint32_t>(1));
}
