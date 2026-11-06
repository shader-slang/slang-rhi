#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("compute-smoke", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndSpecializeProgram(
        device,
        "test-compute-smoke",
        "computeMain",
        "AddTransformer",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // Now we can use this type to create a shader object that can be bound to the root object.
        ComPtr<IShaderObject> transformer;
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            slangReflection->findTypeByName("AddTransformer"),
            ShaderObjectContainerType::None,
            transformer.writeRef()
        ));
        // Set the `c` field of the `AddTransformer`.
        float c = 1.0f;
        ShaderCursor(transformer)["c"].setData(&c, sizeof(float));

        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        cursor["buffer"].setBinding(buffer);
        // Bind the previously created transformer object to root object.
        cursor["transformer"].setObject(transformer);

        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

GPU_TEST_CASE("automatic-specialization-is-rejected", ALL)
{
    struct Callback : IDebugCallback
    {
        std::string errors;
        void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char* message) override
        {
            if (type == DebugMessageType::Error)
                errors += message;
        }
    } callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    auto testDevice = createTestingDevice(ctx, device->getDeviceType(), false, &extraOptions);
    REQUIRE(testDevice);

    std::string parameters;
    std::string globals;
    std::string expression;
    SUBCASE("entrypoint interface")
    {
        parameters = "uniform ITransform transform";
        expression = "transform.apply(1)";
    }
    SUBCASE("global interface")
    {
        globals = "ITransform transform;";
        expression = "transform.apply(1)";
    }
    SUBCASE("nested interface in parameter block")
    {
        globals = "struct Params { ITransform transform; }; ParameterBlock<Params> params;";
        expression = "params.transform.apply(1)";
    }
    std::string source =
        "interface ITransform { uint apply(uint x); };"
        "struct Transform : ITransform { uint apply(uint x) { return x + 1; } };" +
        globals +
        " RWStructuredBuffer<uint> output;"
        "[shader(\"compute\")][numthreads(1,1,1)] void computeMain(" +
        parameters + ") { output[0] = " + expression + "; }";
    auto session = testDevice->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    auto module = session->loadModuleFromSourceString(
        "automatic_specialization",
        "automatic_specialization.slang",
        source.c_str(),
        diagnostics.writeRef()
    );
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module);
    ComPtr<slang::IEntryPoint> entryPoint;
    REQUIRE_CALL(module->findEntryPointByName("computeMain", entryPoint.writeRef()));
    slang::IComponentType* components[] = {module, entryPoint};
    ComPtr<slang::IComponentType> composite;
    REQUIRE_CALL(session->createCompositeComponentType(components, 2, composite.writeRef()));
    REQUIRE(composite->getSpecializationParamCount() > 0);

    for (uint32_t mode = 0; mode < 3; ++mode)
    {
        CAPTURE(mode);
        ShaderProgramDesc desc = {};
        slang::IComponentType* entryPoints[] = {entryPoint};
        if (mode == 0)
            desc.slangGlobalScope = composite;
        else
        {
            desc.slangGlobalScope = module;
            desc.slangEntryPoints = entryPoints;
            desc.slangEntryPointCount = 1;
            if (mode == 2)
                desc.linkingStyle = LinkingStyle::SeparateEntryPointCompilation;
        }
        callback.errors.clear();
        ComPtr<IShaderProgram> program;
        CHECK_EQ(testDevice->createShaderProgram(desc, program.writeRef()), SLANG_E_INVALID_ARG);
        CHECK_FALSE(program);
        CHECK(callback.errors.find("Automatic shader specialization is not supported") != std::string::npos);
    }

    // The same program is accepted when the application resolves the type through Slang first.
    auto type = composite->getLayout()->findTypeByName("Transform");
    REQUIRE(type);
    auto arg = slang::SpecializationArg::fromType(type);
    ComPtr<slang::IComponentType> specialized;
    REQUIRE_CALL(composite->specialize(&arg, 1, specialized.writeRef()));
    CHECK_EQ(specialized->getSpecializationParamCount(), 0);
    ShaderProgramDesc desc = {};
    desc.slangGlobalScope = specialized;
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(testDevice->createShaderProgram(desc, program.writeRef()));
}
