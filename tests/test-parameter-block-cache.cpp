#include "testing.h"
#include "../src/parameter-block-cache.h"
#include <slang-rhi/synthetic-bindings.h>

using namespace rhi;
using namespace rhi::testing;

namespace {

ComPtr<IShaderObject> getObject(const ShaderCursor& cursor)
{
    return cursor.m_baseObject->getObject(cursor.m_offset);
}

ComPtr<IShaderProgram> makeProgram(IDevice* device, const std::string& source, const void* next = nullptr)
{
    static uint32_t moduleID = 0;
    std::string name = "parameter_block_cache_" + std::to_string(moduleID++);
    auto session = device->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    auto blob = UnownedBlob::create(source.data(), source.size());
    auto module = session->loadModuleFromSource(name.c_str(), name.c_str(), blob, diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module);
    ComPtr<slang::IEntryPoint> entry;
    REQUIRE_CALL(module->findEntryPointByName("computeMain", entry.writeRef()));
    slang::IComponentType* components[] = {module, entry};
    ComPtr<slang::IComponentType> composite;
    REQUIRE_CALL(session->createCompositeComponentType(components, 2, composite.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);
    ShaderProgramDesc desc;
    desc.next = next;
    desc.slangGlobalScope = composite;
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(device->createShaderProgram(desc, program.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);
    return program;
}

ComPtr<IBuffer> makeBuffer(IDevice* device, uint32_t value)
{
    BufferDesc desc;
    desc.size = sizeof(value);
    desc.elementSize = sizeof(value);
    desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                 BufferUsage::CopyDestination;
    desc.defaultState = ResourceState::ShaderResource;
    auto buffer = device->createBuffer(desc, &value);
    REQUIRE(buffer);
    return buffer;
}

std::string sceneSource(bool shifted)
{
    std::string source = R"(
struct Leaf { uint value; StructuredBuffer<uint> data; }
struct Scene { uint value; ParameterBlock<Leaf> leaf; }
struct Padding { uint value; }
)";
    if (shifted)
        source += "ParameterBlock<Padding> padding;\n";
    source += R"(
ParameterBlock<Scene> scene;
uint delta;
RWStructuredBuffer<uint> output;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uniform uint bias)
{
    output[0] = scene.value + scene.leaf.value + scene.leaf.data[0] + delta + bias
)";
    if (shifted)
        source += " + padding.value";
    return source + ";\n}\n";
}

void testReuse(IDevice* device, bool frozen, bool synthetic = false)
{
    SyntheticResourceBindingDesc resource;
    resource.id = 1;
    resource.bindingType = slang::BindingType::MutableRawBuffer;
    resource.arraySize = 1;
    resource.space = 6;
    resource.binding = 11;
    resource.access = SyntheticResourceAccess::ReadWrite;
    ShaderProgramSyntheticResourcesDesc extra;
    extra.resources = &resource;
    extra.resourceCount = 1;
    ComPtr<IShaderProgram> programs[2];
    ComPtr<IComputePipeline> pipelines[2];
    ComPtr<IShaderObject> roots[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        programs[i] = makeProgram(device, sceneSource(i != 0), synthetic && i == 1 ? &extra : nullptr);
        ComputePipelineDesc desc;
        desc.program = programs[i];
        pipelines[i] = device->createComputePipeline(desc);
        REQUIRE(pipelines[i]);
        roots[i] = device->createRootShaderObject(programs[i]);
        REQUIRE_CALL(ShaderCursor(roots[i]->getEntryPoint(0))["bias"].setData(2u));
    }
    auto data = makeBuffer(device, 3);
    auto replacement = makeBuffer(device, 9);
    if (synthetic)
    {
        REQUIRE_CALL(bindSyntheticResource(programs[1], roots[1], resource.id, Binding(replacement)));
    }
    auto scene = getObject(ShaderCursor(roots[0])["scene"]);
    auto leaf = getObject(ShaderCursor(scene)["leaf"]);
    REQUIRE(scene);
    REQUIRE(leaf);
    REQUIRE_CALL(ShaderCursor(scene)["value"].setData(10u));
    REQUIRE_CALL(ShaderCursor(leaf)["value"].setData(20u));
    REQUIRE_CALL(ShaderCursor(leaf)["data"].setBinding(data));
    REQUIRE_CALL(ShaderCursor(roots[1])["scene"].setObject(scene));
    REQUIRE_CALL(ShaderCursor(roots[1])["padding"]["value"].setData(0u));
    if (frozen)
    {
        REQUIRE_CALL(scene->finalize());
        CHECK(scene->isFinalized());
        CHECK(leaf->isFinalized());
    }

    auto queue = device->getQueue(QueueType::Graphics);
    std::vector<ComPtr<IBuffer>> outputs;
    std::vector<uint32_t> expected;
    ComPtr<ICommandBuffer> commands[2];
    for (uint32_t recording = 0; recording < 2; ++recording)
    {
        ParameterBlockCacheStats stats;
        ScopedParameterBlockCacheStats capture(stats);
        auto encoder = queue->createCommandEncoder();
        size_t firstWrites = 0;
        size_t firstTracking = 0;
        for (uint32_t i = 0; i < 6; ++i)
        {
            // Copies and new passes must not invalidate descriptor reuse or suppress transitions.
            if (i == 3 && recording == 0)
                encoder->copyBuffer(data, 0, replacement, 0, sizeof(uint32_t));
            if (!frozen)
                REQUIRE_CALL(ShaderCursor(leaf)["value"].setData(20u + i));
            auto output = makeBuffer(device, 0);
            auto root = roots[i % 2];
            REQUIRE_CALL(ShaderCursor(root)["delta"].setData(i));
            REQUIRE_CALL(ShaderCursor(root)["output"].setBinding(output));
            auto pass = encoder->beginComputePass();
            pass->bindPipeline(pipelines[i % 2], root);
            pass->dispatchCompute(1, 1, 1);
            pass->end();
            outputs.push_back(output);
            expected.push_back(35 + i + (recording || i >= 3 ? 6 : 0) + (frozen ? 0 : i));
            if (i == 0)
            {
                firstWrites = stats.frozenOrdinaryDataWrites;
                firstTracking = stats.frozenResourceTraversals;
            }
        }
        REQUIRE_CALL(encoder->finish(commands[recording].writeRef()));
        if (frozen)
        {
            CHECK(stats.preparations == 2); // Scene and its nested leaf, even across programs.
            CHECK(stats.hits == 5);
            CHECK(stats.frozenOrdinaryDataWrites == firstWrites);
            CHECK(stats.frozenResourceTraversals == firstTracking);
        }
        else
        {
            CHECK(stats.preparations == 0);
            CHECK(stats.hits == 0);
        }
    }
    // The recorded commands own the frozen graph and its resources, including CUDA buffers.
    scene.setNull();
    leaf.setNull();
    if (frozen)
        data.setNull();
    replacement.setNull();
    for (auto& root : roots)
        root.setNull();
    for (auto& command : commands)
        queue->submit(command);
    queue->waitOnHost();
    for (size_t i = 0; i < outputs.size(); ++i)
        compareComputeResult(device, outputs[i], makeArray<uint32_t>(expected[i]));
}

struct ExpectedErrors : IDebugCallback
{
    void SLANG_MCALL handleMessage(DebugMessageType, DebugMessageSource, const char*) override {}
};

void testFinalization(GpuTestContext* ctx, DeviceType type)
{
    ExpectedErrors errors;
    DeviceExtraOptions options;
    options.debugCallback = &errors;
    auto device = createTestingDevice(ctx, type, false, &options);
    auto program = makeProgram(device, sceneSource(false));
    auto root = device->createRootShaderObject(program);
    auto entry = root->getEntryPoint(0);
    auto scene = getObject(ShaderCursor(root)["scene"]);
    auto leaf = getObject(ShaderCursor(scene)["leaf"]);
    auto otherRoot = device->createRootShaderObject(program);
    REQUIRE_CALL(ShaderCursor(otherRoot)["scene"].setObject(scene));
    REQUIRE_CALL(root->finalize());
    CHECK(root->isFinalized());
    CHECK(entry->isFinalized());
    CHECK(scene->isFinalized());
    CHECK(leaf->isFinalized());
    CHECK_FALSE(otherRoot->isFinalized());
    for (auto object : {root.get(), entry.get(), scene.get(), leaf.get()})
    {
        uint32_t value = 7;
        void* reserved = nullptr;
        CHECK(SLANG_FAILED(object->setData({}, &value, sizeof(value))));
        CHECK(SLANG_FAILED(object->reserveData({}, sizeof(value), &reserved)));
        CHECK(SLANG_FAILED(object->setBinding({}, Binding{})));
        CHECK(SLANG_FAILED(object->setObject({}, leaf)));
        CHECK(SLANG_FAILED(object->setDescriptorHandle({}, DescriptorHandle{})));
        CHECK(SLANG_FAILED(object->setSpecializationArgs({}, nullptr, 0)));
        CHECK(SLANG_FAILED(object->setConstantBufferOverride(nullptr)));
        CHECK(SLANG_FAILED(object->finalize()));
    }
}

} // namespace

GPU_TEST_CASE("parameter-block-cache-reuse", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks unavailable");
    testReuse(device, true);
}

GPU_TEST_CASE("parameter-block-cache-mutable", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks unavailable");
    testReuse(device, false);
}

GPU_TEST_CASE("parameter-block-cache-vulkan-sparse-sets", Vulkan)
{
    testReuse(device, true, true);
}

GPU_TEST_CASE_EX("parameter-block-finalization-no-debug", ALL | DontCreateDevice, DebugLayerOptions{})
{
    testFinalization(ctx, ctx->deviceType);
}

GPU_TEST_CASE("parameter-block-finalization-debug", ALL | DontCreateDevice)
{
    testFinalization(ctx, ctx->deviceType);
}

GPU_TEST_CASE("parameter-block-cache-layout-variants", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks unavailable");
    ComPtr<IShaderProgram> programs[2];
    ComPtr<IComputePipeline> pipelines[2];
    ComPtr<IShaderObject> roots[2];
    auto a = makeBuffer(device, 4);
    auto b = makeBuffer(device, 7);
    auto result = makeBuffer(device, 0);
    for (uint32_t i = 0; i < 2; ++i)
    {
        std::string source = "struct Scene { uint value; ";
        source += i ? "RWStructuredBuffer<uint> a; StructuredBuffer<uint> b; "
                    : "StructuredBuffer<uint> a; RWStructuredBuffer<uint> b; ";
        source += R"(}
ParameterBlock<Scene> scene;
RWStructuredBuffer<uint> result;
[shader("compute")][numthreads(1, 1, 1)]
void computeMain() { result[0] = scene.value + scene.a[0] + scene.b[0]; }
)";
        programs[i] = makeProgram(device, source);
        ComputePipelineDesc desc;
        desc.program = programs[i];
        pipelines[i] = device->createComputePipeline(desc);
        REQUIRE(pipelines[i]);
        roots[i] = device->createRootShaderObject(programs[i]);
        REQUIRE_CALL(ShaderCursor(roots[i])["result"].setBinding(result));
    }
    auto scene = getObject(ShaderCursor(roots[0])["scene"]);
    REQUIRE_CALL(ShaderCursor(scene)["value"].setData(10u));
    REQUIRE_CALL(ShaderCursor(scene)["a"].setBinding(a));
    REQUIRE_CALL(ShaderCursor(scene)["b"].setBinding(b));
    REQUIRE_CALL(scene->finalize());
    REQUIRE_CALL(ShaderCursor(roots[1])["scene"].setObject(scene));
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    ParameterBlockCacheStats stats;
    ScopedParameterBlockCacheStats capture(stats);
    for (uint32_t i : {0u, 1u, 0u})
    {
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(pipelines[i], roots[i]);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
    }
    auto command = encoder->finish();
    REQUIRE(command);
    CHECK(stats.preparations == 2);
    CHECK(stats.hits == 1);
    queue->submit(command);
    queue->waitOnHost();
    compareComputeResult(device, result, makeArray<uint32_t>(21));
}

GPU_TEST_CASE("parameter-block-cache-repeated-child-and-empty-parent", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks unavailable");
    auto program = makeProgram(device, R"(
struct Leaf { uint value; }
struct Parent { ParameterBlock<Leaf> first; ParameterBlock<Leaf> second; }
ParameterBlock<Parent> scene;
RWStructuredBuffer<uint> result;
[shader("compute")][numthreads(1, 1, 1)]
void computeMain() { result[0] = scene.first.value + scene.second.value; }
)");
    ComputePipelineDesc desc;
    desc.program = program;
    auto pipeline = device->createComputePipeline(desc);
    REQUIRE(pipeline);
    auto root = device->createRootShaderObject(program);
    auto scene = getObject(ShaderCursor(root)["scene"]);
    auto leaf = getObject(ShaderCursor(scene)["first"]);
    REQUIRE_CALL(ShaderCursor(leaf)["value"].setData(17u));
    REQUIRE_CALL(ShaderCursor(scene)["second"].setObject(leaf));
    REQUIRE_CALL(scene->finalize());
    auto result = makeBuffer(device, 0);
    REQUIRE_CALL(ShaderCursor(root)["result"].setBinding(result));
    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t recording = 0; recording < 3; ++recording)
    {
        ParameterBlockCacheStats stats;
        ScopedParameterBlockCacheStats capture(stats);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(pipeline, root);
        pass->dispatchCompute(1, 1, 1);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        auto command = encoder->finish();
        REQUIRE(command);
        CHECK(stats.preparations == 2);
        CHECK(stats.hits == 2); // Same child twice, then the entire parent subtree.
        queue->submit(command);
        queue->waitOnHost();
        compareComputeResult(device, result, makeArray<uint32_t>(34));
    }
}

GPU_TEST_CASE("parameter-block-cache-compute-ray-tracing", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock) || !device->hasFeature(Feature::RayTracing))
        SKIP("parameter blocks or ray tracing unavailable");
    auto computeProgram = makeProgram(device, sceneSource(false));
    std::string raySource = sceneSource(true);
    auto stage = raySource.find("[shader(\"compute\")]\n[numthreads(1, 1, 1)]");
    raySource.replace(stage, strlen("[shader(\"compute\")]\n[numthreads(1, 1, 1)]"), "[shader(\"raygeneration\")]");
    auto rayProgram = makeProgram(device, raySource);
    ComputePipelineDesc computeDesc;
    computeDesc.program = computeProgram;
    auto compute = device->createComputePipeline(computeDesc);
    REQUIRE(compute);
    RayTracingPipelineDesc rayDesc;
    rayDesc.program = rayProgram;
    rayDesc.maxRecursion = 1;
    auto ray = device->createRayTracingPipeline(rayDesc);
    REQUIRE(ray);
    const char* raygen = "computeMain";
    ShaderTableDesc tableDesc;
    tableDesc.program = rayProgram;
    tableDesc.rayGenShaderCount = 1;
    tableDesc.rayGenShaderEntryPointNames = &raygen;
    ComPtr<IShaderTable> table;
    REQUIRE_CALL(device->createShaderTable(tableDesc, table.writeRef()));
    REQUIRE(table);
    auto computeRoot = device->createRootShaderObject(computeProgram);
    auto rayRoot = device->createRootShaderObject(rayProgram);
    auto scene = getObject(ShaderCursor(computeRoot)["scene"]);
    auto data = makeBuffer(device, 3);
    auto result = makeBuffer(device, 0);
    REQUIRE_CALL(ShaderCursor(scene)["value"].setData(10u));
    REQUIRE_CALL(ShaderCursor(scene)["leaf"]["value"].setData(20u));
    REQUIRE_CALL(ShaderCursor(scene)["leaf"]["data"].setBinding(data));
    REQUIRE_CALL(scene->finalize());
    REQUIRE_CALL(ShaderCursor(rayRoot)["scene"].setObject(scene));
    REQUIRE_CALL(ShaderCursor(rayRoot)["padding"]["value"].setData(0u));
    for (auto root : {computeRoot, rayRoot})
    {
        REQUIRE_CALL(ShaderCursor(root)["delta"].setData(1u));
        REQUIRE_CALL(ShaderCursor(root)["output"].setBinding(result));
        REQUIRE_CALL(ShaderCursor(root->getEntryPoint(0))["bias"].setData(2u));
    }
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    ParameterBlockCacheStats stats;
    ScopedParameterBlockCacheStats capture(stats);
    auto pass = encoder->beginComputePass();
    pass->bindPipeline(compute, computeRoot);
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    auto rayPass = encoder->beginRayTracingPass();
    rayPass->bindPipeline(ray, table, rayRoot);
    rayPass->dispatchRays(0, 1, 1, 1);
    rayPass->end();
    auto command = encoder->finish();
    REQUIRE(command);
    CHECK(stats.preparations == 2);
    CHECK(stats.hits == 1);
    queue->submit(command);
    queue->waitOnHost();
    compareComputeResult(device, result, makeArray<uint32_t>(36));
}

GPU_TEST_CASE("parameter-block-cache-uav-dependencies", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks unavailable");
    auto program = makeProgram(device, R"(
struct Counter { RWStructuredBuffer<uint> value; }
ParameterBlock<Counter> counter;
[shader("compute")][numthreads(1, 1, 1)]
void computeMain() { counter.value[0] += 1; }
)");
    ComputePipelineDesc desc;
    desc.program = program;
    auto pipeline = device->createComputePipeline(desc);
    REQUIRE(pipeline);
    auto root = device->createRootShaderObject(program);
    auto block = getObject(ShaderCursor(root)["counter"]);
    auto counter = makeBuffer(device, 0);
    auto snapshot = makeBuffer(device, 0);
    IndirectDispatchArguments arguments = {1, 1, 1};
    BufferDesc argsDesc;
    argsDesc.size = sizeof(arguments);
    argsDesc.usage = BufferUsage::IndirectArgument | BufferUsage::CopyDestination;
    argsDesc.defaultState = ResourceState::IndirectArgument;
    auto indirect = device->createBuffer(argsDesc, &arguments);
    REQUIRE(indirect);
    REQUIRE_CALL(ShaderCursor(block)["value"].setBinding(counter));
    REQUIRE_CALL(block->finalize());
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    ParameterBlockCacheStats stats;
    ScopedParameterBlockCacheStats capture(stats);
    for (uint32_t passIndex = 0; passIndex < 2; ++passIndex)
    {
        if (passIndex)
            encoder->copyBuffer(snapshot, 0, counter, 0, sizeof(uint32_t));
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(pipeline, root);
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (i % 2 && device->getDeviceType() != DeviceType::CPU)
                pass->dispatchComputeIndirect({indirect, 0});
            else
                pass->dispatchCompute(1, 1, 1);
        }
        pass->end();
    }
    auto command = encoder->finish();
    REQUIRE(command);
    CHECK(stats.preparations == 1);
    CHECK(stats.hits == 7);
    queue->submit(command);
    queue->waitOnHost();
    compareComputeResult(device, counter, makeArray<uint32_t>(8));
    compareComputeResult(device, snapshot, makeArray<uint32_t>(4));
}
