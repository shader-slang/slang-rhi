#include "testing.h"

using namespace rhi;
using namespace testing;

static ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t content)
{
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)&content, buffer.writeRef()));

    return buffer;
}

GPU_TEST_CASE("root-shader-parameter", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-root-shader-parameter",
        "computeMain",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<ComPtr<IBuffer>> buffers;

    for (uint32_t i = 0; i < 9; i++)
    {
        buffers.push_back(createBuffer(device, i == 0 ? 10 : i));
    }

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));

    ComPtr<IShaderObject> g, s1, s2;
    REQUIRE_CALL(
        device->createShaderObject(slangReflection->findTypeByName("S0"), ShaderObjectContainerType::None, g.writeRef())
    );
    REQUIRE_CALL(device->createShaderObject(
        slangReflection->findTypeByName("S1"),
        ShaderObjectContainerType::None,
        s1.writeRef()
    ));
    REQUIRE_CALL(device->createShaderObject(
        slangReflection->findTypeByName("S1"),
        ShaderObjectContainerType::None,
        s2.writeRef()
    ));

    {
        auto cursor = ShaderCursor(s1);
        cursor["c0"].setBinding(buffers[2]);
        cursor["c1"].setBinding(buffers[3]);
        cursor["c2"].setBinding(buffers[4]);
    }
    {
        auto cursor = ShaderCursor(s2);
        cursor["c0"].setBinding(buffers[5]);
        cursor["c1"].setBinding(buffers[6]);
        cursor["c2"].setBinding(buffers[7]);
    }
    {
        auto cursor = ShaderCursor(g);
        cursor["b0"].setBinding(buffers[0]);
        cursor["b1"].setBinding(buffers[1]);
        cursor["s1"].setObject(s1);
        cursor["s2"].setObject(s2);
    }
    {
        auto cursor = ShaderCursor(rootObject);
        cursor["g"].setObject(g);
        cursor["buffer"].setBinding(buffers[8]);
    }

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

    compareComputeResult(device, buffers[8], makeArray<uint32_t>(10 - 1 + 2 - 3 + 4 + 5 - 6 + 7));
}
