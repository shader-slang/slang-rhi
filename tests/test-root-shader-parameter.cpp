#include "testing.h"

#include "shader-object.h"
#include "shader.h"

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

static void runRootShaderParameter(IDevice* device, slang::ISession* session = nullptr)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        session,
        "test-root-shader-parameter",
        "computeMain",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    if (device->getDeviceType() == DeviceType::D3D12)
    {
        // Only g is a subobject. Treating the [root] structured buffer as an
        // ordinary descriptor would allocate a second subobject slot for it.
        auto* layout = checked_cast<ShaderProgram*>(shaderProgram.get())->getRootShaderObjectLayout();
        CHECK_EQ(layout->getSubObjectCount(), 1u);
    }

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
    REQUIRE_CALL(device->createShaderObjectFromType(
        shaderProgram,
        slangReflection->findTypeByName("S0"),
        ShaderObjectContainerType::None,
        g.writeRef()
    ));
    REQUIRE_CALL(device->createShaderObjectFromType(
        shaderProgram,
        slangReflection->findTypeByName("S1"),
        ShaderObjectContainerType::None,
        s1.writeRef()
    ));
    REQUIRE_CALL(device->createShaderObjectFromType(
        shaderProgram,
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

GPU_TEST_CASE("root-shader-parameter", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    runRootShaderParameter(device);
}

GPU_TEST_CASE("root-shader-parameter-external-global-session", D3D12)
{
    ComPtr<slang::IGlobalSession> globalSession;
    REQUIRE_CALL(slang::createGlobalSession(globalSession.writeRef()));
    slang::TargetDesc target = {};
    target.format = SLANG_DXIL;
    target.profile = globalSession->findProfile("sm_6_0");
    const char* searchPath = getTestsDir();
    slang::SessionDesc desc = {};
    desc.targets = &target;
    desc.targetCount = 1;
    desc.searchPaths = &searchPath;
    desc.searchPathCount = 1;
    ComPtr<slang::ISession> session;
    REQUIRE_CALL(globalSession->createSession(desc, session.writeRef()));

    runRootShaderParameter(device, session);
}
