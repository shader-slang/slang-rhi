// Coverage for the CUDA global-parameter copy-skip and packet interning: identical
// packed global packets are uploaded once per command buffer and reused, while
// changed global data, changed resources, per-launch entry-point arguments, and a
// native callback that mutates the symbol must all still take effect. Every case
// asserts output identical to a plain per-dispatch copy.
//
// The optimization under test lives entirely in src/cuda/, so it is only exercised
// on a real CUDA device. The CUDA|CPU cases run on CPU as a correctness control (a
// reference that the expected outputs are themselves right); the CPU device shares
// none of the CUDA copy-skip path.

#include "testing.h"

#include <vector>

#if SLANG_RHI_ENABLE_CUDA
#include <slang-rhi/cuda-driver-api.h>
#include "../src/cuda/cuda-command.h"
#include "debug-layer/debug-command-buffer.h"
#endif

using namespace rhi;
using namespace rhi::testing;

static ComPtr<IBuffer> createFloatBuffer(IDevice* device, const float* initialData, size_t count)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = count * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));
    return buffer;
}

static ComPtr<IComputePipeline> createGlobalParamsPipeline(
    IDevice* device,
    ComPtr<IShaderProgram>& outProgram,
    const char* entryPoint = "computeMain"
)
{
    REQUIRE_CALL(loadProgram(device, "test-cuda-global-params-copy-skip", entryPoint, outProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = outProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    return pipeline;
}

// Record one dispatch of computeMain in its own compute pass:
// outputBuffer[globalIndex] = globalAddend + epArg. A fresh root object per dispatch
// means identical arguments yield a byte-identical global packet.
static void recordDispatch(
    ICommandEncoder* encoder,
    IDevice* device,
    IComputePipeline* pipeline,
    IBuffer* buffer,
    float globalAddend,
    uint32_t globalIndex,
    float epArg
)
{
    auto rootObject = device->createRootShaderObject(pipeline);
    ShaderCursor root(rootObject);
    root["outputBuffer"].setBinding(buffer);
    root["globalAddend"].setData(globalAddend);
    root["globalIndex"].setData(globalIndex);
    ShaderCursor(rootObject->getEntryPoint(0))["epArg"].setData(epArg);

    auto passEncoder = encoder->beginComputePass();
    passEncoder->bindPipeline(pipeline, rootObject);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
}

// Record one dispatch of computeMainBig, whose global packet embeds a large bigData
// array: outputBuffer[globalIndex] = bigData[globalIndex].
static void recordBigDispatch(
    ICommandEncoder* encoder,
    IDevice* device,
    IComputePipeline* pipeline,
    IBuffer* buffer,
    const float* bigData,
    size_t bigCount,
    uint32_t globalIndex
)
{
    auto rootObject = device->createRootShaderObject(pipeline);
    ShaderCursor root(rootObject);
    root["outputBuffer"].setBinding(buffer);
    root["globalIndex"].setData(globalIndex);
    root["bigData"].setData(bigData, bigCount * sizeof(float));

    auto passEncoder = encoder->beginComputePass();
    passEncoder->bindPipeline(pipeline, rootObject);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
}

// A repeated identical packet then changed global data, in one command buffer: the
// result must match a plain per-dispatch copy.
GPU_TEST_CASE("cuda-global-params-copy-skip", CUDA | CPU)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f, 0.f, 0.f, 0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        encoder->globalBarrier();
        // Identical global packet: eligible to reuse the prior upload; still writes 10.
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        encoder->globalBarrier();
        // Changed global data must take effect.
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 20.f, /*index*/ 1, /*epArg*/ 0.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 30.f, /*index*/ 2, /*epArg*/ 0.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(10.f, 20.f, 30.f, 0.f));
}

// A byte-identical global packet with a changing entry-point argument: the final
// value must reflect the second launch's epArg, since entry-point arguments are
// supplied on every launch independent of any reuse of the global packet.
GPU_TEST_CASE("cuda-global-params-entry-point-args", CUDA | CPU)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 5.f);
        encoder->globalBarrier();
        // Same global packet, different entry-point argument.
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 7.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(17.f));
}

// Changing the bound buffer (a resource in the global packet) on the same pipeline
// changes the packet, so each buffer must receive its own result.
GPU_TEST_CASE("cuda-global-params-changed-resource", CUDA | CPU)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f};
    auto bufferA = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));
    auto bufferB = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        // Same global scalar and index for both dispatches, so the global packet differs
        // only by the bound buffer -- this isolates the resource change. The distinct
        // expected outputs come from the per-launch entry-point argument, which is not
        // part of the global packet, so a defect that ignored the resource change (and
        // wrongly reused bufferA's upload for bufferB) would leave bufferB unwritten.
        recordDispatch(encoder, device, pipeline, bufferA, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipeline, bufferB, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 5.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, bufferA, makeArray<float>(10.f));
    compareComputeResult(device, bufferB, makeArray<float>(15.f));
}

// The per-command-buffer state must not survive its command buffer: a second,
// separately submitted command buffer with different global data must upload afresh.
GPU_TEST_CASE("cuda-global-params-per-command-buffer", CUDA | CPU)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    auto queue = device->getQueue(QueueType::Graphics);
    {
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }
    {
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 99.f, /*index*/ 0, /*epArg*/ 0.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(99.f));
}

// A -> B -> A on the same destination symbol. The state tracks the last source, so
// after B overwrites the symbol, returning to A's (distinct) packet must upload
// again rather than be treated as unchanged.
GPU_TEST_CASE("cuda-global-params-source-change", CUDA | CPU)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 20.f, /*index*/ 0, /*epArg*/ 0.f);
        encoder->globalBarrier();
        // Back to A's packet: the symbol currently holds B, so this must upload again.
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 0.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(10.f));
}

// Two pipelines with byte-identical global packets, interleaved in one command
// buffer. Because interning gives both packets the same canonical source address, a
// copy-skip that was NOT keyed per destination symbol would skip pipeline B's upload
// (its source matches A's) and leave B's own SLANG_globalParams symbol unpopulated.
// The entry-point argument is not part of the global packet, so it distinguishes the
// two launches: B's result is correct only if its own symbol was uploaded.
GPU_TEST_CASE("cuda-global-params-interleaved-pipelines", CUDA | CPU)
{
    ComPtr<IShaderProgram> programA;
    ComPtr<IShaderProgram> programB;
    auto pipelineA = createGlobalParamsPipeline(device, programA, "computeMain");
    auto pipelineB = createGlobalParamsPipeline(device, programB, "computeMain2");

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        // Identical global packet (same buffer/addend/index) for both pipelines; only
        // the entry-point epArg differs. out = globalAddend + epArg.
        recordDispatch(encoder, device, pipelineA, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 100.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipelineB, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 200.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipelineA, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 100.f);
        encoder->globalBarrier();
        recordDispatch(encoder, device, pipelineB, buffer, /*addend*/ 10.f, /*index*/ 0, /*epArg*/ 200.f);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    // Last launch is pipeline B: 10 (from its own symbol) + 200. A copy-skip not keyed
    // per destination symbol would leave B's symbol unpopulated, so the result would
    // not be 210.
    compareComputeResult(device, buffer, makeArray<float>(210.f));
}

// Large (8 KB) packets: several distinct ones grow the constant-buffer pool's page
// vector while the intern map holds keys into earlier pages. Every dispatch targets
// index 0 with a distinct value, and the final repeat re-sends packet 0's bytes; an
// incorrect canonical match (a stale key aliasing another packet) would leave a
// wrong value at index 0.
GPU_TEST_CASE("cuda-global-params-large-packet", CUDA | CPU)
{
    constexpr uint32_t kElems = 2048;   // 8 KB packet
    constexpr uint32_t kDispatches = 5; // > 16 KB total -> pool page-vector growth

    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program, "computeMainBig");

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    std::vector<float> big(kElems, 0.f);
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        for (uint32_t d = 0; d < kDispatches; ++d)
        {
            big[0] = float(10 + d);
            recordBigDispatch(encoder, device, pipeline, buffer, big.data(), kElems, 0);
            encoder->globalBarrier();
        }
        // Repeat packet 0's exact bytes (big[0] == 10): must resolve to packet 0.
        big[0] = 10.f;
        recordBigDispatch(encoder, device, pipeline, buffer, big.data(), kElems, 0);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(10.f));
}

// A nested parameter block, packed as its own interned child packet. A repeated
// identical block is eligible for reuse; a changed block must take effect.
GPU_TEST_CASE("cuda-global-params-nested-block", CUDA | CPU)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-cuda-global-params-copy-skip",
        "computeMainNested",
        program.writeRef(),
        &reflection
    ));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.f, 0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto recordNested = [&](float materialValue, uint32_t index)
    {
        ComPtr<IShaderObject> materialObject;
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            reflection->findTypeByName("Material"),
            ShaderObjectContainerType::None,
            materialObject.writeRef()
        ));
        ShaderCursor(materialObject)["value"].setData(materialValue);
        materialObject->finalize();

        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor root(rootObject);
        root["outputBuffer"].setBinding(buffer);
        root["globalIndex"].setData(index);
        root["material"].setObject(materialObject);

        auto passEncoder = encoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
    };

    recordNested(5.f, 0);
    encoder->globalBarrier();
    recordNested(5.f, 0); // identical nested block -> eligible for reuse
    encoder->globalBarrier();
    recordNested(7.f, 1); // changed nested block -> must take effect
    queue->submit(encoder->finish());
    queue->waitOnHost();

    compareComputeResult(device, buffer, makeArray<float>(5.f, 7.f));
}

#if SLANG_RHI_ENABLE_CUDA
namespace {
// Unwrap the concrete CUDA command buffer (past the debug-layer wrapper the test
// harness installs) so a test can inspect its BindingCache directly.
cuda::CommandBufferImpl* asCudaCommandBuffer(ICommandBuffer* commandBuffer)
{
    if (auto* debugCommandBuffer = dynamic_cast<debug::DebugCommandBuffer*>(commandBuffer))
        return static_cast<cuda::CommandBufferImpl*>(debugCommandBuffer->baseObject.get());
    return static_cast<cuda::CommandBufferImpl*>(commandBuffer);
}

struct GlobalParamsCorruption
{
    CUdeviceptr device = 0;
    size_t size = 0;
    CUresult* result = nullptr; // callback records whether the zeroing was enqueued
};

// Native callback that overwrites the whole SLANG_globalParams symbol with zeros on
// the command stream. The shader's global block is a single scalar (the output
// buffer is an entry-point argument), so no device pointer is disturbed. The struct
// is passed via userData (copied by value); its `result` pointer still refers to the
// test's stack, so the enqueue status is observable after submission.
void SLANG_MCALL zeroGlobalParams(const ExecuteCallbackContext* context, void*, const void* userData, Size)
{
    auto* corruption = static_cast<const GlobalParamsCorruption*>(userData);
    CUstream stream = reinterpret_cast<CUstream>(context->nativeHandle.value);
    *corruption->result = cuMemsetD8Async(corruption->device, 0, corruption->size, stream);
}
} // namespace

// A native callback may mutate module state, so the copy-skip state is cleared at
// every ExecuteCallback. Here the callback zeroes SLANG_globalParams between two
// identical dispatches; without the clear the second upload would be skipped and the
// dispatch would read zero instead of the intended value.
GPU_TEST_CASE("cuda-global-params-callback-clear", CUDA)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program, "computeMainCallback");

    NativeHandle moduleHandle = {};
    REQUIRE_CALL(pipeline->getNativeHandle(&moduleHandle));
    CUmodule module = reinterpret_cast<CUmodule>(moduleHandle.value);
    GlobalParamsCorruption corruption;
    REQUIRE(cuModuleGetGlobal(&corruption.device, &corruption.size, module, "SLANG_globalParams") == CUDA_SUCCESS);
    CUresult memsetResult = CUDA_ERROR_UNKNOWN;
    corruption.result = &memsetResult;

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();

    auto recordCallbackDispatch = [&](float addend)
    {
        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor(rootObject)["callbackAddend"].setData(addend);
        ShaderCursor(rootObject->getEntryPoint(0))["cbOut"].setBinding(buffer);
        auto passEncoder = encoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
    };

    recordCallbackDispatch(42.f);
    encoder->globalBarrier();

    ExecuteCallbackDesc callbackDesc = {};
    callbackDesc.callback = zeroGlobalParams;
    callbackDesc.userData = &corruption;
    callbackDesc.userDataSize = sizeof(corruption);
    encoder->executeCallback(callbackDesc);
    encoder->globalBarrier();

    // Identical global packet, but the callback zeroed the symbol; the clear forces a
    // fresh upload so the result is 42, not 0.
    recordCallbackDispatch(42.f);
    queue->submit(encoder->finish());
    queue->waitOnHost();

    // cuMemsetD8Async returns success on enqueue, not completion; this REQUIRE only
    // confirms the zeroing was submitted. The memset is recorded between the two
    // dispatches on the single m_stream, so stream ordering (not this guard) is what
    // guarantees the zero lands before the second dispatch reads the symbol.
    REQUIRE(memsetResult == CUDA_SUCCESS);
    compareComputeResult(device, buffer, makeArray<float>(42.f));
}

// A byte-identical global packet submitted from several sequential command buffers on
// one queue, each writing its own index via an entry-point argument. This directly
// verifies the BindingCache::reset()-clears-the-intern-map invariant by inspecting the
// unwrapped CommandBufferImpl: recording interns the packet, so the map is non-empty
// after finish(); waitOnHost() retires the command buffer, which calls reset() and
// must clear the map before its ConstantBufferPool releases the pages the string_view
// keys borrow from. (Output alone cannot catch a missing clear: the pool recycles
// suballocations without releasing memory, so a stale key would stay byte-valid.)
GPU_TEST_CASE("cuda-global-params-recycled-command-buffers", CUDA)
{
    constexpr uint32_t kSubmissions = 4;
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program, "computeMainSeq");

    std::vector<float> initialData(kSubmissions, 0.f);
    auto buffer = createFloatBuffer(device, initialData.data(), kSubmissions);

    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t i = 0; i < kSubmissions; ++i)
    {
        auto encoder = queue->createCommandEncoder();
        auto rootObject = device->createRootShaderObject(pipeline);
        // Byte-identical global packet on every submission.
        ShaderCursor(rootObject)["seqAddend"].setData(42.f);
        ShaderCursor entryPoint(rootObject->getEntryPoint(0));
        entryPoint["seqOut"].setBinding(buffer);
        entryPoint["seqIndex"].setData(i);

        auto passEncoder = encoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        cuda::CommandBufferImpl* cudaCommandBuffer = asCudaCommandBuffer(commandBuffer);
        // Recording interned the packet.
        CHECK(!cudaCommandBuffer->m_bindingCache.internedGlobalParams.empty());

        queue->submit(commandBuffer);
        queue->waitOnHost();
        // Retirement reset the recycled command buffer, clearing the intern map.
        CHECK(cudaCommandBuffer->m_bindingCache.internedGlobalParams.empty());
    }

    compareComputeResult(device, buffer, makeArray<float>(42.f, 42.f, 42.f, 42.f));
}

// Directly asserts that interning fires: recording the same global packet many times in
// one command buffer must not grow the intern map beyond a single build's footprint,
// because each repeat resolves to the already-interned canonical entry. That collapse to
// shared canonical source addresses is exactly what lets the dispatch-time copy-skip
// recognize the packets as unchanged. Comparing against a measured single-build baseline
// (rather than a hard-coded count) keeps the test robust to how many distinct global
// packets a build happens to produce -- e.g. a root packet plus a default
// constant-buffer sub-object. Asserted on the unwrapped CommandBufferImpl since the
// elision is not observable from output alone.
GPU_TEST_CASE("cuda-global-params-interning-dedup", CUDA)
{
    ComPtr<IShaderProgram> program;
    auto pipeline = createGlobalParamsPipeline(device, program);

    float initialData[] = {0.f};
    auto buffer = createFloatBuffer(device, initialData, SLANG_COUNT_OF(initialData));

    auto queue = device->getQueue(QueueType::Graphics);

    // Baseline: how many distinct global packets a single build produces for this pipeline.
    size_t singleBuildEntries = 0;
    {
        auto encoder = queue->createCommandEncoder();
        recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 5.f, /*index*/ 0, /*epArg*/ 0.f);
        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        singleBuildEntries = asCudaCommandBuffer(commandBuffer)->m_bindingCache.internedGlobalParams.size();
        queue->submit(commandBuffer);
        queue->waitOnHost();
    }
    CHECK(singleBuildEntries >= 1);

    // Four byte-identical builds in one command buffer: interning reuses the first, so
    // the map stays at the single-build footprint (without dedup it would be 4x larger).
    {
        auto encoder = queue->createCommandEncoder();
        for (int i = 0; i < 4; ++i)
        {
            recordDispatch(encoder, device, pipeline, buffer, /*addend*/ 5.f, /*index*/ 0, /*epArg*/ 0.f);
            encoder->globalBarrier();
        }
        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        CHECK(asCudaCommandBuffer(commandBuffer)->m_bindingCache.internedGlobalParams.size() == singleBuildEntries);
        queue->submit(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(5.f));
}
#endif // SLANG_RHI_ENABLE_CUDA
