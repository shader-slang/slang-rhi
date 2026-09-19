// Keep this benchmark restricted to public APIs so the exact source can run on older revisions.
#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

namespace {

using Clock = std::chrono::steady_clock;
enum class Workload
{
    Compute,
    Graphics,
    RayTracing
};
enum Mode
{
    StaticMutable,
    StaticFrozen,
    RootMutable,
    RootOneFrozen,
    RootTwoFrozen,
    MutateBlocks,
    SwapMutable,
    SwapFrozen,
    FreshMutable,
    FreshFrozen,
    RotateMutable,
    RotateFrozen,
    BulkRoot,
    ModeCount
};
const char* kModes[] = {
    "static-mutable",
    "static-frozen-root",
    "root-mutable-blocks",
    "root-one-frozen-block",
    "root-two-frozen-blocks",
    "mutate-blocks",
    "swap-mutable-blocks",
    "swap-frozen-blocks",
    "fresh-mutable-blocks",
    "fresh-frozen-blocks",
    "rotate-mutable-roots",
    "rotate-frozen-roots",
    "bulk-root-two-frozen-blocks"
};
constexpr uint32_t kMaterials = 64;

uint32_t setting(const char* name, uint32_t fallback, uint32_t minimum = 1)
{
#ifdef _WIN32
    char value[32];
    size_t length = 0;
    const char* text = getenv_s(&length, value, sizeof(value), name) == 0 && length ? value : nullptr;
#else
    const char* text = std::getenv(name);
#endif
    return text ? uint32_t(std::max(int(minimum), std::atoi(text))) : fallback;
}

struct Fixture
{
    IDevice* device;
    Workload workload;
    ComPtr<ICommandQueue> queue;
    ComPtr<IShaderProgram> program;
    ComPtr<IComputePipeline> compute;
    ComPtr<IRenderPipeline> graphics;
    ComPtr<IRayTracingPipeline> rayTracing;
    ComPtr<IShaderTable> table;
    ComPtr<IBuffer> inputs[2], output;
    ComPtr<ITexture> target;
    ComPtr<ITextureView> targetView;
    ComPtr<IQueryPool> timestamps;
    std::unique_ptr<SingleTriangleBLAS> blas;
    std::unique_ptr<TLAS> tlas;
    slang::TypeReflection* materialType = nullptr;

    void init()
    {
        queue = device->getQueue(QueueType::Graphics);
        const char* module = workload == Workload::Compute    ? "test-benchmark-bindings-compute"
                             : workload == Workload::Graphics ? "test-benchmark-bindings-graphics"
                                                              : "test-benchmark-bindings-ray-tracing";
        std::vector<const char*> entries = workload == Workload::Compute ? std::vector<const char*>{"computeMain"}
                                           : workload == Workload::Graphics
                                               ? std::vector<const char*>{"vertexMain", "fragmentMain"}
                                               : std::vector<const char*>{"rayGen", "closestHit", "miss"};
        REQUIRE_CALL(loadProgram(device, module, entries, program.writeRef()));
        if (workload == Workload::Compute)
        {
            ComputePipelineDesc desc;
            desc.program = program;
            REQUIRE_CALL(device->createComputePipeline(desc, compute.writeRef()));
        }
        else if (workload == Workload::Graphics)
        {
            ColorTargetDesc color;
            color.format = Format::RGBA32Float;
            color.enableBlend = true;
            color.color.dstFactor = BlendFactor::One;
            color.alpha.dstFactor = BlendFactor::One;
            RenderPipelineDesc desc;
            desc.program = program;
            auto inputLayout = device->createInputLayout({});
            REQUIRE(inputLayout);
            desc.inputLayout = inputLayout;
            desc.targets = &color;
            desc.targetCount = 1;
            desc.rasterizer.cullMode = CullMode::None;
            desc.depthStencil.depthTestEnable = false;
            desc.depthStencil.depthWriteEnable = false;
            REQUIRE_CALL(device->createRenderPipeline(desc, graphics.writeRef()));
            TextureDesc texture;
            texture.size = {1, 1, 1};
            texture.format = color.format;
            texture.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
            texture.defaultState = ResourceState::RenderTarget;
            target = device->createTexture(texture);
            REQUIRE(target);
            REQUIRE_CALL(device->createTextureView(target, {}, targetView.writeRef()));
        }
        else
        {
            HitGroupDesc hit;
            hit.hitGroupName = "hit";
            hit.closestHitEntryPoint = "closestHit";
            RayTracingPipelineDesc desc;
            desc.program = program;
            desc.hitGroups = &hit;
            desc.hitGroupCount = 1;
            desc.maxRecursion = 1;
            desc.maxRayPayloadSize = 4;
            REQUIRE_CALL(device->createRayTracingPipeline(desc, rayTracing.writeRef()));
            const char* rayGen[] = {"rayGen"};
            const char* hits[] = {"hit"};
            const char* misses[] = {"miss"};
            ShaderTableDesc shaderTable;
            shaderTable.program = program;
            shaderTable.rayGenShaderCount = shaderTable.hitGroupCount = shaderTable.missShaderCount = 1;
            shaderTable.rayGenShaderEntryPointNames = rayGen;
            shaderTable.hitGroupNames = hits;
            shaderTable.missShaderEntryPointNames = misses;
            REQUIRE_CALL(device->createShaderTable(shaderTable, table.writeRef()));
            blas = std::make_unique<SingleTriangleBLAS>(device, queue);
            tlas = std::make_unique<TLAS>(device, queue, blas->blas);
        }
        BufferDesc buffer;
        buffer.size = sizeof(uint32_t);
        buffer.elementSize = sizeof(uint32_t);
        buffer.usage = BufferUsage::ShaderResource;
        buffer.defaultState = ResourceState::ShaderResource;
        for (uint32_t i = 0; i < 2; ++i)
        {
            uint32_t value = i + 1;
            REQUIRE_CALL(device->createBuffer(buffer, &value, inputs[i].writeRef()));
        }
        buffer.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination;
        buffer.defaultState = ResourceState::UnorderedAccess;
        output = device->createBuffer(buffer);
        REQUIRE(output);
        auto root = device->createRootShaderObject(program);
        REQUIRE(root);
        auto block = root->getObject(ShaderCursor(root)["first"].m_offset);
        REQUIRE(block);
        materialType = block->getElementTypeLayout()->getType();
        if (device->hasFeature(Feature::TimestampQuery))
        {
            QueryPoolDesc desc;
            desc.count = 2;
            REQUIRE_CALL(device->createQueryPool(desc, timestamps.writeRef()));
        }
    }

    Result fillBlock(IShaderObject* block, uint32_t id)
    {
        ShaderCursor c(block);
        for (auto name : {"a", "b", "c", "d"})
            SLANG_RETURN_ON_FAIL(c[name].setBinding(inputs[id & 1]));
        SLANG_RETURN_ON_FAIL(c["bias"].setData(id));
        std::array<uint32_t, 60> data;
        data.fill(1);
        return c["data"].setData(data.data(), sizeof(data));
    }

    Result makeBlock(uint32_t id, bool frozen, ComPtr<IShaderObject>& block)
    {
        SLANG_RETURN_ON_FAIL(
            device->createShaderObject(nullptr, materialType, ShaderObjectContainerType::None, block.writeRef())
        );
        SLANG_RETURN_ON_FAIL(fillBlock(block, id));
        return frozen ? block->finalize() : SLANG_OK;
    }

    ComPtr<IShaderObject> makeRoot(uint32_t iteration, IShaderObject* first, IShaderObject* second, bool frozen)
    {
        auto root = device->createRootShaderObject(program);
        REQUIRE(root);
        ShaderCursor c(root);
        REQUIRE_CALL(c["first"].setObject(first));
        REQUIRE_CALL(c["second"].setObject(second));
        REQUIRE_CALL(c["iteration"].setData(iteration));
        std::array<uint32_t, 64> payload;
        payload.fill(1);
        REQUIRE_CALL(c["payload"].setData(payload.data(), sizeof(payload)));
        if (workload != Workload::Graphics)
            REQUIRE_CALL(c["output"].setBinding(output));
        if (workload == Workload::RayTracing)
            REQUIRE_CALL(c["scene"].setBinding(tlas->tlas));
        if (frozen)
            REQUIRE_CALL(root->finalize());
        return root;
    }
};

struct State
{
    std::vector<ComPtr<IShaderObject>> blocks;
    std::vector<ComPtr<IShaderObject>> roots;
};

void run(IDevice* device, Workload workload)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("parameter blocks not supported");
    Fixture f{device, workload};
    f.init();
    const char* workloadName = workload == Workload::Compute    ? "compute"
                               : workload == Workload::Graphics ? "graphics"
                                                                : "ray-tracing";
    uint32_t count = setting("SLANG_RHI_BINDING_BENCHMARK_COUNT", 1024);
    uint32_t samples = setting("SLANG_RHI_BINDING_BENCHMARK_SAMPLES", 7);
    uint32_t selectedMode = setting("SLANG_RHI_BINDING_BENCHMARK_MODE", ModeCount, 0);
    State states[ModeCount];
    for (uint32_t mode = 0; mode < ModeCount; ++mode)
    {
        if (selectedMode < ModeCount && mode != selectedMode)
            continue;
        auto& s = states[mode];
        s.blocks.resize(kMaterials);
        for (uint32_t i = 0; i < kMaterials; ++i)
        {
            bool frozen = mode == StaticFrozen || mode == RootTwoFrozen || mode == SwapFrozen || mode == RotateFrozen ||
                          mode == BulkRoot || (mode == RootOneFrozen && i == 0);
            REQUIRE_CALL(f.makeBlock(i, frozen, s.blocks[i]));
        }
        uint32_t rootCount = mode == RotateMutable || mode == RotateFrozen ? kMaterials : 1;
        for (uint32_t i = 0; i < rootCount; ++i)
            s.roots.push_back(
                f.makeRoot(i, s.blocks[i], s.blocks[(i + 1) % kMaterials], mode == StaticFrozen || mode == RotateFrozen)
            );
    }
    for (uint32_t sample = 0; sample <= samples; ++sample)
    {
        // Rotate mode order, while retaining each mode's objects across command buffers.
        for (uint32_t index = 0; index < ModeCount; ++index)
        {
            uint32_t mode = (index + sample) % ModeCount;
            if (selectedMode < ModeCount && mode != selectedMode)
                continue;
            auto& s = states[mode];
            bool rotating = mode == RotateMutable || mode == RotateFrozen;
            bool swapping = mode == SwapMutable || mode == SwapFrozen;
            bool fresh = mode == FreshMutable || mode == FreshFrozen;
            bool changing = mode != StaticMutable && mode != StaticFrozen && !rotating;
            // Compute expected output independently, outside the measured section.
            uint32_t expected = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t it = rotating ? i % kMaterials : changing ? i & 63 : 0;
                uint32_t a = rotating || swapping || fresh || mode == MutateBlocks ? i % kMaterials : 0;
                uint32_t b = (a + 1) % kMaterials;
                auto value = [](uint32_t id)
                {
                    return 4 * (1 + (id & 1)) + id + 2;
                };
                expected += it + (mode == BulkRoot ? 2 * (1 + (i & 3)) : 2) + value(a) + value(b);
                if (workload == Workload::RayTracing)
                    expected += 2 + (it & 1);
            }
            if (f.timestamps)
                REQUIRE_CALL(f.timestamps->reset());
            auto start = Clock::now();
            auto encoder = f.queue->createCommandEncoder();
            Result result = SLANG_OK;
            uint32_t zero = 0;
            if (workload != Workload::Graphics)
                result |= encoder->uploadBufferData(f.output, 0, sizeof(zero), &zero);
            if (f.timestamps)
                encoder->writeTimestamp(f.timestamps, 0);
            IComputePassEncoder* compute = nullptr;
            IRenderPassEncoder* graphics = nullptr;
            IRayTracingPassEncoder* rays = nullptr;
            if (workload == Workload::Compute)
                compute = encoder->beginComputePass();
            else if (workload == Workload::Graphics)
            {
                RenderPassColorAttachment color;
                color.view = f.targetView;
                color.loadOp = LoadOp::Clear;
                color.storeOp = StoreOp::Store;
                RenderPassDesc desc;
                desc.colorAttachments = &color;
                desc.colorAttachmentCount = 1;
                graphics = encoder->beginRenderPass(desc);
                RenderState state;
                state.viewports[0] = Viewport::fromSize(1, 1);
                state.viewportCount = 1;
                state.scissorRects[0] = ScissorRect::fromSize(1, 1);
                state.scissorRectCount = 1;
                graphics->setRenderState(state);
            }
            else
                rays = encoder->beginRayTracingPass();
            auto initialized = Clock::now();
            for (uint32_t i = 0; i < count; ++i)
            {
                auto root = s.roots[rotating ? i % kMaterials : 0].get();
                ShaderCursor c(root);
                if (changing)
                    result |= c["iteration"].setData(i & 63);
                if (mode == BulkRoot)
                {
                    std::array<uint32_t, 64> payload;
                    payload.fill(1 + (i & 3));
                    result |= c["payload"].setData(payload.data(), sizeof(payload));
                }
                if (mode == MutateBlocks)
                {
                    result |= f.fillBlock(s.blocks[0], i % kMaterials);
                    result |= f.fillBlock(s.blocks[1], (i + 1) % kMaterials);
                }
                if (swapping || fresh)
                {
                    ComPtr<IShaderObject> first, second;
                    if (fresh)
                    {
                        result |= f.makeBlock(i % kMaterials, mode == FreshFrozen, first);
                        result |= f.makeBlock((i + 1) % kMaterials, mode == FreshFrozen, second);
                    }
                    else
                    {
                        first = s.blocks[i % kMaterials];
                        second = s.blocks[(i + 1) % kMaterials];
                    }
                    result |= c["first"].setObject(first);
                    result |= c["second"].setObject(second);
                }
                if (compute)
                {
                    compute->bindPipeline(f.compute, root);
                    compute->dispatchCompute(1, 1, 1);
                }
                else if (graphics)
                {
                    graphics->bindPipeline(f.graphics, root);
                    DrawArguments args;
                    args.vertexCount = 3;
                    graphics->draw(args);
                }
                else
                {
                    rays->bindPipeline(f.rayTracing, f.table, root);
                    rays->dispatchRays(0, 1, 1, 1);
                }
            }
            auto encoded = Clock::now();
            if (compute)
                compute->end();
            if (graphics)
                graphics->end();
            if (rays)
                rays->end();
            if (f.timestamps)
                encoder->writeTimestamp(f.timestamps, 1);
            auto commands = encoder->finish();
            auto finished = Clock::now();
            result |= f.queue->submit(commands);
            auto submitted = Clock::now();
            result |= f.queue->waitOnHost();
            auto completed = Clock::now();
            commands = nullptr;
            encoder = nullptr;
            auto retired = Clock::now();
            REQUIRE_CALL(result);
            double gpu = -1;
            if (f.timestamps)
            {
                uint64_t ticks[2];
                REQUIRE_CALL(f.timestamps->getResult(0, 2, ticks));
                gpu = double(ticks[1] - ticks[0]) * 1e9 / device->getInfo().timestampFrequency / count;
            }
            CAPTURE(mode);
            CAPTURE(sample);
            CAPTURE(workloadName);
            if (workload == Workload::Graphics)
            {
                ComPtr<ISlangBlob> pixels;
                SubresourceLayout layout;
                REQUIRE_CALL(device->readTexture(f.target, 0, 0, pixels.writeRef(), &layout));
                auto p = static_cast<const float*>(pixels->getBufferPointer());
                CHECK(p[0] == float(expected));
                CHECK(p[1] == float(count));
            }
            else
                compareComputeResult(device, f.output, makeArray<uint32_t>(expected));
            if (sample)
            {
                auto ns = [&](auto duration)
                {
                    return std::chrono::duration<double, std::nano>(duration).count() / count;
                };
                std::printf(
                    "binding-matrix,%s,%s,%s,%u,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                    workloadName,
                    deviceTypeToString(device->getDeviceType()),
                    kModes[mode],
                    count,
                    sample,
                    ns(initialized - start),
                    ns(encoded - initialized),
                    ns(finished - encoded),
                    ns(submitted - finished),
                    ns(retired - completed),
                    ns(submitted - start) + ns(retired - completed),
                    ns(retired - start),
                    gpu
                );
            }
        }
    }
}
} // namespace

GPU_TEST_CASE_EX("benchmark-bindings-compute", ALL | DontCacheDevice, DebugLayerOptions{})
{
    run(device, Workload::Compute);
}
GPU_TEST_CASE_EX(
    "benchmark-bindings-graphics",
    D3D11 | D3D12 | Vulkan | Metal | WGPU | DontCacheDevice,
    DebugLayerOptions{}
)
{
    run(device, Workload::Graphics);
}
GPU_TEST_CASE_EX("benchmark-bindings-ray-tracing", D3D12 | Vulkan | CUDA | DontCacheDevice, DebugLayerOptions{})
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    run(device, Workload::RayTracing);
}
