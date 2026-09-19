#include "testing.h"
#include "../src/debug-layer/debug-shader-object.h"

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace rhi;
using namespace rhi::testing;

namespace {

const char* kSource = R"(
struct Settings
{
    StructuredBuffer<uint> a;
    StructuredBuffer<uint> b;
    StructuredBuffer<uint> c;
    StructuredBuffer<uint> d;
    uint bias;
};
ParameterBlock<Settings> settings;
RWStructuredBuffer<uint> output;
uint iteration;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uniform uint entryBias)
{
    output[0] += settings.a[0] + settings.b[0] + settings.c[0] + settings.d[0] + settings.bias + iteration + entryBias;
}
)";

struct Fixture
{
    ComPtr<IShaderProgram> program;
    ComPtr<IComputePipeline> pipeline;
    ComPtr<IBuffer> input;
    ComPtr<IBuffer> output;

    void init(IDevice* device, const char* source = kSource)
    {
        REQUIRE_CALL(loadComputeProgramFromSource(device, source, program.writeRef()));
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.program = program;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        BufferDesc desc;
        desc.size = sizeof(uint32_t);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        desc.defaultState = ResourceState::ShaderResource;
        uint32_t value = 1;
        REQUIRE_CALL(device->createBuffer(desc, &value, input.writeRef()));
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination;
        desc.defaultState = ResourceState::UnorderedAccess;
        value = 0;
        REQUIRE_CALL(device->createBuffer(desc, &value, output.writeRef()));
    }

    ComPtr<IShaderObject> createRoot(IDevice* device)
    {
        ComPtr<IShaderObject> root;
        REQUIRE_CALL(device->createRootShaderObject(program, root.writeRef()));
        ShaderCursor cursor(root);
        REQUIRE_CALL(cursor["output"].setBinding(output));
        REQUIRE_CALL(cursor["iteration"].setData(uint32_t(0)));
        REQUIRE_CALL(ShaderCursor(root->getEntryPoint(0))["entryBias"].setData(uint32_t(0)));
        auto settings = cursor["settings"];
        REQUIRE_CALL(settings["a"].setBinding(input));
        REQUIRE_CALL(settings["b"].setBinding(input));
        REQUIRE_CALL(settings["c"].setBinding(input));
        REQUIRE_CALL(settings["d"].setBinding(input));
        REQUIRE_CALL(settings["bias"].setData(uint32_t(1)));
        return root;
    }
};

} // namespace

GPU_TEST_CASE("finalized-shader-object", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    Fixture fixture;
    fixture.init(device);
    auto queue = device->getQueue(QueueType::Graphics);

    for (uint32_t mode = 0; mode < 3; ++mode)
    {
        bool finalizeRoot = mode == 2;
        auto root = fixture.createRoot(device);
        auto settings = root->getObject(ShaderCursor(root)["settings"].m_offset);
        REQUIRE(settings);
        if (mode != 0)
        {
            REQUIRE_CALL(settings->finalize());
            CHECK(settings->isFinalized());
        }
        if (finalizeRoot)
        {
            REQUIRE_CALL(root->finalize());
            CHECK(root->isFinalized());
            for (uint32_t i = 0; i < root->getEntryPointCount(); ++i)
                CHECK(root->getEntryPoint(i)->isFinalized());
        }

        uint32_t expected = 0;
        for (uint32_t batch = 0; batch < 3; ++batch)
        {
            auto encoder = queue->createCommandEncoder();
            uint32_t zero = 0;
            REQUIRE_CALL(encoder->uploadBufferData(fixture.output, 0, sizeof(zero), &zero));
            uint32_t input = batch + 1;
            REQUIRE_CALL(encoder->uploadBufferData(fixture.input, 0, sizeof(input), &input));
            auto pass = encoder->beginComputePass();
            pass->bindPipeline(fixture.pipeline, root);
            expected = 0;
            for (uint32_t i = 0; i < 8; ++i)
            {
                if (!finalizeRoot)
                {
                    REQUIRE_CALL(ShaderCursor(root)["iteration"].setData(i));
                    REQUIRE_CALL(ShaderCursor(root->getEntryPoint(0))["entryBias"].setData(i));
                }
                if (mode == 0)
                    REQUIRE_CALL(ShaderCursor(settings)["bias"].setData(i + 1));
                pass->dispatchCompute(1, 1, 1);
                expected += 4 * input + (mode == 0 ? i + 1 : 1) + (finalizeRoot ? 0 : 2 * i);
            }
            pass->end();
            auto commands = encoder->finish();
            REQUIRE(commands);
            // The last command buffer must keep prepared data alive on its own.
            if (batch == 2)
            {
                settings = nullptr;
                root = nullptr;
                encoder = nullptr;
            }
            queue->submit(commands);
            queue->waitOnHost();
            compareComputeResult(device, fixture.output, makeArray<uint32_t>(expected));
        }
    }
}

GPU_TEST_CASE_EX("finalized-shader-object-immutability", ALL, DebugLayerOptions{})
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    Fixture fixture;
    fixture.init(device);
    auto root = fixture.createRoot(device);
    auto settingsOffset = ShaderCursor(root)["settings"].m_offset;
    auto settings = root->getObject(settingsOffset);
    uint32_t value = 2;
    auto biasOffset = ShaderCursor(settings)["bias"].m_offset;
    void* writable = nullptr;
    REQUIRE_CALL(settings->reserveData(biasOffset, sizeof(value), &writable));
    std::memcpy(writable, &value, sizeof(value));
    REQUIRE_CALL(root->finalize());
    CHECK(root->isFinalized());
    CHECK(settings->isFinalized());
    // Debug builds always wrap shader objects. Check base-layer rejection here;
    // intentional validation errors would abort the test via the debug callback.
    if (auto wrapper = dynamic_cast<rhi::debug::DebugShaderObject*>(root.get()))
    {
        ComPtr<IShaderObject> base = wrapper->baseObject;
        root = base;
        settings = root->getObject(settingsOffset);
    }
    CHECK(SLANG_FAILED(settings->setData(biasOffset, &value, sizeof(value))));
    CHECK(SLANG_FAILED(settings->reserveData(biasOffset, sizeof(value), &writable)));
    CHECK(SLANG_FAILED(ShaderCursor(settings)["a"].setBinding(fixture.input)));
    CHECK(SLANG_FAILED(root->setObject(settingsOffset, settings)));
    CHECK(SLANG_FAILED(root->setSpecializationArgs(settingsOffset, nullptr, 0)));
    CHECK(SLANG_FAILED(root->setDescriptorHandle({}, {})));
    CHECK(SLANG_FAILED(root->finalize()));
    for (uint32_t i = 0; i < root->getEntryPointCount(); ++i)
    {
        auto entryPoint = root->getEntryPoint(i);
        CHECK(entryPoint->isFinalized());
        CHECK(SLANG_FAILED(entryPoint->setData({}, &value, 0)));
    }
}

static void testSharedBlock(IDevice* device, bool rootDescriptors)
{
    std::string source = kSource;
    source.insert(source.find("RWStructuredBuffer<uint> output;"), "ParameterBlock<Settings> other;\n");
    source.insert(source.find("settings.a[0]"), "other.a[0] + other.bias + ");
    if (rootDescriptors)
    {
        source.insert(source.find("StructuredBuffer<uint> a;"), "[root] ");
        source.insert(source.find("RWStructuredBuffer<uint> output;"), "[root] ");
        source.insert(0, "[__AttributeUsage(_AttributeTargets.Var)] struct rootAttribute {};\n");
    }
    Fixture fixture;
    fixture.init(device, source.c_str());
    auto rootA = fixture.createRoot(device);
    auto rootB = fixture.createRoot(device);
    auto shared = rootA->getObject(ShaderCursor(rootA)["settings"].m_offset);
    REQUIRE_CALL(shared->finalize());
    REQUIRE_CALL(ShaderCursor(rootA)["other"].setObject(shared));
    REQUIRE_CALL(ShaderCursor(rootB)["other"].setObject(shared));
    REQUIRE_CALL(rootA->finalize());
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    uint32_t expected = 0;
    for (uint32_t i = 0; i < 8; ++i)
    {
        pass->bindPipeline(fixture.pipeline, rootA);
        pass->dispatchCompute(1, 1, 1);
        expected += 7;
        REQUIRE_CALL(ShaderCursor(rootB)["settings"]["bias"].setData(i));
        pass->bindPipeline(fixture.pipeline, rootB);
        pass->dispatchCompute(1, 1, 1);
        expected += 6 + i;
    }
    pass->end();
    auto commands = encoder->finish();
    REQUIRE(commands);
    shared = nullptr;
    rootA = nullptr;
    rootB = nullptr;
    fixture.input = nullptr;
    encoder = nullptr;
    queue->submit(commands);
    queue->waitOnHost();
    compareComputeResult(device, fixture.output, makeArray<uint32_t>(expected));
}

GPU_TEST_CASE("finalized-shader-object-shared-block", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    testSharedBlock(device, false);
}

GPU_TEST_CASE("finalized-shader-object-shared-root-descriptors", D3D12)
{
    testSharedBlock(device, true);
}

GPU_TEST_CASE("finalized-shader-object-block-composition", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    const char* source = R"(
        [__AttributeUsage(_AttributeTargets.Var)] struct rootAttribute {};
        struct Leaf { [root] StructuredBuffer<uint> input; uint bias; uint padding0, padding1, padding2; };
        struct Block { StructuredBuffer<uint> input; ParameterBlock<Leaf> leaf; uint bias; uint padding0, padding1, padding2; };
        [shader("compute")] [numthreads(1, 1, 1)]
        void first(uniform ParameterBlock<Block> shared, uniform RWStructuredBuffer<uint> output)
        {
            output[0] += shared.input[0] + shared.bias + shared.leaf.input[0] + shared.leaf.bias;
        }
        [shader("compute")] [numthreads(1, 1, 1)]
        void second(uniform ParameterBlock<Leaf> prefix, uniform ParameterBlock<Block> shared,
                    uniform RWStructuredBuffer<uint> output)
        {
            output[0] += prefix.input[0] + prefix.bias;
            output[0] += shared.input[0] + shared.bias + shared.leaf.input[0] + shared.leaf.bias;
        }
    )";
    auto session = device->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    auto module =
        session->loadModuleFromSourceString("finalized_block_composition", nullptr, source, diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module);
    ComPtr<IShaderProgram> programs[2];
    ComPtr<IComputePipeline> pipelines[2];
    ComPtr<IShaderObject> roots[2];
    const char* entries[] = {"first", "second"};
    BufferDesc desc;
    desc.size = desc.elementSize = sizeof(uint32_t);
    desc.usage = BufferUsage::ShaderResource;
    desc.defaultState = ResourceState::ShaderResource;
    uint32_t one = 1;
    auto input = device->createBuffer(desc, &one);
    REQUIRE(input);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination;
    desc.defaultState = ResourceState::UnorderedAccess;
    uint32_t zero = 0;
    auto output = device->createBuffer(desc, &zero);
    REQUIRE(output);
    for (uint32_t i = 0; i < 2; ++i)
    {
        REQUIRE_CALL(loadAndLinkProgram(device, "finalized_block_composition", entries[i], programs[i].writeRef()));
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.program = programs[i];
        pipelines[i] = device->createComputePipeline(pipelineDesc);
        REQUIRE(pipelines[i]);
        roots[i] = device->createRootShaderObject(programs[i]);
        REQUIRE(roots[i]);
        REQUIRE_CALL(ShaderCursor(roots[i]->getEntryPoint(0))["output"].setBinding(output));
    }
    auto entryA = roots[0]->getEntryPoint(0);
    auto shared = entryA->getObject(ShaderCursor(entryA)["shared"].m_offset);
    REQUIRE(shared);
    REQUIRE_CALL(ShaderCursor(shared)["input"].setBinding(input));
    REQUIRE_CALL(ShaderCursor(shared)["bias"].setData(2u));
    REQUIRE_CALL(ShaderCursor(shared)["leaf"]["input"].setBinding(input));
    REQUIRE_CALL(ShaderCursor(shared)["leaf"]["bias"].setData(3u));
    REQUIRE_CALL(shared->finalize());
    auto entryB = roots[1]->getEntryPoint(0);
    REQUIRE_CALL(ShaderCursor(entryB)["shared"].setObject(shared));
    REQUIRE_CALL(ShaderCursor(entryB)["prefix"]["input"].setBinding(input));
    REQUIRE_CALL(roots[0]->finalize());
    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t batch = 0; batch < 2; ++batch)
    {
        auto encoder = queue->createCommandEncoder();
        REQUIRE_CALL(encoder->uploadBufferData(output, 0, sizeof(zero), &zero));
        auto pass = encoder->beginComputePass();
        uint32_t expected = 0;
        for (uint32_t i = 0; i < 8; ++i)
        {
            REQUIRE_CALL(ShaderCursor(entryB)["prefix"]["bias"].setData(i));
            pass->bindPipeline(pipelines[0], roots[0]);
            pass->dispatchCompute(1, 1, 1);
            pass->bindPipeline(pipelines[1], roots[1]);
            pass->dispatchCompute(1, 1, 1);
            expected += 15 + i;
        }
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        if (batch == 1)
        {
            entryA = entryB = shared = nullptr;
            roots[0] = roots[1] = nullptr;
            input = nullptr;
            encoder = nullptr;
        }
        queue->submit(commands);
        queue->waitOnHost();
        compareComputeResult(device, output, makeArray<uint32_t>(expected));
    }
}

GPU_TEST_CASE("finalized-shader-object-block-push-constant", Vulkan)
{
    const char* source = R"(
        struct Values { uint value; };
        struct Block {
            StructuredBuffer<uint> input;
            [[vk::push_constant]] ConstantBuffer<Values> values;
        };
        ParameterBlock<Block> block;
        RWStructuredBuffer<uint> output;
        [shader("compute")] [numthreads(1, 1, 1)]
        void computeMain() { output[0] += block.input[0] + block.values.value; }
    )";
    Fixture fixture;
    fixture.init(device, source);
    auto root = device->createRootShaderObject(fixture.program);
    REQUIRE(root);
    REQUIRE_CALL(ShaderCursor(root)["output"].setBinding(fixture.output));
    REQUIRE_CALL(ShaderCursor(root)["block"]["input"].setBinding(fixture.input));
    REQUIRE_CALL(ShaderCursor(root)["block"]["values"]["value"].setData(7u));
    auto block = root->getObject(ShaderCursor(root)["block"].m_offset);
    REQUIRE(block);
    REQUIRE_CALL(block->finalize());
    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t batch = 0; batch < 2; ++batch)
    {
        if (batch)
            REQUIRE_CALL(root->finalize());
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(fixture.pipeline, root);
        for (uint32_t i = 0; i < 4; ++i)
            pass->dispatchCompute(1, 1, 1);
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        queue->submit(commands);
        queue->waitOnHost();
        compareComputeResult(device, fixture.output, makeArray<uint32_t>(32u * (batch + 1)));
    }
}

GPU_TEST_CASE("finalized-shader-object-concurrent-recording", D3D12 | Vulkan | CPU | WGPU)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    Fixture fixture;
    fixture.init(device);
    auto root = fixture.createRoot(device);
    REQUIRE_CALL(root->finalize());
    auto queue = device->getQueue(QueueType::Graphics);
    // Create encoders on the calling thread; exercise simultaneous first use of
    // the same finalized graph independently of queue/encoder creation support.
    constexpr uint32_t kThreads = 4;
    ComPtr<ICommandEncoder> encoders[kThreads];
    ComPtr<ICommandBuffer> commands[kThreads];
    std::thread threads[kThreads];
    std::barrier start(kThreads);
    for (auto& encoder : encoders)
        encoder = queue->createCommandEncoder();
    for (uint32_t i = 0; i < kThreads; ++i)
    {
        threads[i] = std::thread(
            [&, i]
            {
                start.arrive_and_wait();
                auto pass = encoders[i]->beginComputePass();
                pass->bindPipeline(fixture.pipeline, root);
                for (uint32_t j = 0; j < 8; ++j)
                    pass->dispatchCompute(1, 1, 1);
                pass->end();
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    for (uint32_t i = 0; i < kThreads; ++i)
    {
        commands[i] = encoders[i]->finish();
        REQUIRE(commands[i]);
    }
    root = nullptr;
    for (auto& encoder : encoders)
        encoder = nullptr;
    fixture.input = nullptr;
    fixture.pipeline = nullptr;
    fixture.program = nullptr;
    for (auto& command : commands)
    {
        queue->submit(command);
        queue->waitOnHost();
    }
    compareComputeResult(device, fixture.output, makeArray<uint32_t>(5 * 8 * kThreads));
}

GPU_TEST_CASE("finalized-shader-object-specialization", ALL)
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-shader-cache-specialization", "computeMain", program.writeRef(), &reflection)
    );
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    BufferDesc desc;
    desc.size = 4 * sizeof(float);
    desc.elementSize = sizeof(float);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination;
    desc.defaultState = ResourceState::UnorderedAccess;
    auto buffer = device->createBuffer(desc);
    REQUIRE(buffer);
    ComPtr<IShaderObject> roots[2];
    const char* types[] = {"AddTransformer", "MulTransformer"};
    for (uint32_t i = 0; i < 2; ++i)
    {
        roots[i] = device->createRootShaderObject(program);
        REQUIRE(roots[i]);
        ComPtr<IShaderObject> transformer;
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            reflection->findTypeByName(types[i]),
            ShaderObjectContainerType::None,
            transformer.writeRef()
        ));
        REQUIRE_CALL(ShaderCursor(transformer)["c"].setData(2.f));
        ShaderCursor entryPoint(roots[i]->getEntryPoint(0));
        REQUIRE_CALL(entryPoint["buffer"].setBinding(buffer));
        REQUIRE_CALL(entryPoint["transformer"].setObject(transformer));
        REQUIRE_CALL(roots[i]->finalize());
    }
    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t batch = 0; batch < 2; ++batch)
    {
        auto encoder = queue->createCommandEncoder();
        float initial[] = {1.f, 2.f, 3.f, 4.f};
        REQUIRE_CALL(encoder->uploadBufferData(buffer, 0, sizeof(initial), initial));
        auto pass = encoder->beginComputePass();
        for (uint32_t i = 0; i < 4; ++i)
        {
            pass->bindPipeline(pipeline, roots[i % 2]);
            pass->dispatchCompute(1, 1, 1);
        }
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        if (batch == 1)
        {
            roots[0] = roots[1] = nullptr;
            encoder = nullptr;
        }
        queue->submit(commands);
        queue->waitOnHost();
        compareComputeResult(device, buffer, makeArray<float>(16.f, 20.f, 24.f, 28.f));
    }
}

// Disable validation overhead for timings. Run in Release; Debug builds always enable validation.
GPU_TEST_CASE_EX("benchmark-finalized-shader-object", ALL, DebugLayerOptions{})
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    Fixture fixture;
    fixture.init(device);
    auto queue = device->getQueue(QueueType::Graphics);
    constexpr uint32_t kSamples = 7;
    using Clock = std::chrono::steady_clock;

    enum Mode
    {
        Mutable,
        FinalizedBlock,
        FinalizedRoot,
        MutableChanging,
        FinalizedBlockChanging,
        ModeCount
    };
    const char* names[] =
        {"mutable", "finalized-block", "finalized-root", "mutable-changing", "finalized-block-changing"};
    ComPtr<IShaderObject> roots[ModeCount];
    for (uint32_t mode = 0; mode < ModeCount; ++mode)
    {
        roots[mode] = fixture.createRoot(device);
        if (mode == FinalizedBlock || mode == FinalizedBlockChanging)
        {
            auto block = roots[mode]->getObject(ShaderCursor(roots[mode])["settings"].m_offset);
            REQUIRE_CALL(block->finalize());
        }
        if (mode == FinalizedRoot)
            REQUIRE_CALL(roots[mode]->finalize());
    }

    struct Times
    {
        double encode;
        double finish;
        double submit;
        double total;
    };
    for (uint32_t dispatchCount : {1u, 1000u})
    {
        std::vector<Times> samples[ModeCount];
        for (uint32_t sample = 0; sample <= kSamples; ++sample)
        {
            // Rotate the order to reduce systematic warmup/clock bias.
            for (uint32_t index = 0; index < ModeCount; ++index)
            {
                uint32_t mode = (index + sample) % ModeCount;
                auto encoder = queue->createCommandEncoder();
                uint32_t zero = 0;
                REQUIRE_CALL(encoder->uploadBufferData(fixture.output, 0, sizeof(zero), &zero));
                auto pass = encoder->beginComputePass();
                pass->bindPipeline(fixture.pipeline, roots[mode]);
                ShaderCursor iteration(roots[mode]);
                iteration = iteration["iteration"];
                Result mutationResult = SLANG_OK;
                auto start = Clock::now();
                for (uint32_t i = 0; i < dispatchCount; ++i)
                {
                    if (mode >= MutableChanging)
                        mutationResult |= iteration.setData(i & 1);
                    pass->dispatchCompute(1, 1, 1);
                }
                auto encoded = Clock::now();
                pass->end();
                auto commands = encoder->finish();
                auto finished = Clock::now();
                REQUIRE(commands);
                REQUIRE_CALL(mutationResult);
                queue->submit(commands);
                auto submitted = Clock::now();
                queue->waitOnHost();
                compareComputeResult(
                    device,
                    fixture.output,
                    makeArray<uint32_t>(5 * dispatchCount + (mode >= MutableChanging ? dispatchCount / 2 : 0))
                );
                if (sample)
                {
                    auto ns = [&](auto duration)
                    {
                        return std::chrono::duration<double, std::nano>(duration).count() / dispatchCount;
                    };
                    samples[mode].push_back(
                        {ns(encoded - start), ns(finished - encoded), ns(submitted - finished), ns(submitted - start)}
                    );
                }
            }
        }
        double baseline = 0;
        double totalBaseline = 0;
        for (uint32_t mode = 0; mode < ModeCount; ++mode)
        {
            auto median = [&](auto member)
            {
                std::vector<double> values;
                for (auto sample : samples[mode])
                    values.push_back(sample.*member);
                std::sort(values.begin(), values.end());
                return values[values.size() / 2];
            };
            double encode = median(&Times::encode);
            double total = median(&Times::total);
            if (mode == Mutable || mode == MutableChanging)
            {
                baseline = encode;
                totalBaseline = total;
            }
            std::fprintf(
                stderr,
                "binding-benchmark,%s,dispatches=%u,%s,encode-ns=%.1f,finish-ns=%.1f,submit-ns=%.1f,"
                "total-cpu-ns=%.1f,encode-speedup=%.2fx,total-cpu-speedup=%.2fx\n",
                deviceTypeToString(device->getDeviceType()),
                dispatchCount,
                names[mode],
                encode,
                median(&Times::finish),
                median(&Times::submit),
                total,
                baseline / encode,
                totalBaseline / total
            );
        }
    }
}
