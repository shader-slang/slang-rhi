#include "testing.h"
#include "debug-layer/debug-device.h"

#include <algorithm>
#include <string>
#include <vector>

#if SLANG_RHI_ENABLE_CUDA
#include "../src/cuda/cuda-device.h"
#include "../src/cuda/cuda-utils.h"
#include "core/platform.h"
#endif

using namespace rhi;
using namespace rhi::testing;

namespace {

struct ErrorCapture : IDebugCallback
{
    std::string errors;

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        SLANG_UNUSED(source);
        if (type == DebugMessageType::Error)
            errors += message;
    }
};

ComPtr<IComputePipeline> createCooperativePipeline(
    IDevice* device,
    PipelineCompilationPolicy compilationPolicy = PipelineCompilationPolicy::Default
)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cooperative-launch", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    pipelineDesc.compilationPolicy = compilationPolicy;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    return pipeline;
}

ComPtr<IBuffer> createGroupBuffer(IDevice* device, uint32_t elementCount)
{
    std::vector<uint32_t> initialData(elementCount, 0);
    BufferDesc bufferDesc = {};
    bufferDesc.size = elementCount * sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData.data(), buffer.writeRef()));
    return buffer;
}

ComPtr<ICommandBuffer> recordGridSum(IDevice* device, IComputePipeline* pipeline, IBuffer* buffer, uint32_t groupCount)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor cursor(rootObject);
    cursor["buffer"].setBinding(buffer);
    cursor["groupCount"].setData(groupCount);
    passEncoder->dispatchCompute(groupCount, 1, 1);
    passEncoder->end();
    return commandEncoder->finish();
}

ComPtr<IComputePipeline> createEntryPointParamsPipeline(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cooperative-launch", "computeEntryPointParams", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    return pipeline;
}

// Records the grid sum of computeEntryPointParams, whose inputs are all entry-point parameters.
void encodeEntryPointParamsSum(
    IComputePassEncoder* passEncoder,
    IComputePipeline* pipeline,
    IBuffer* buffer,
    uint32_t groupCount,
    uint32_t scale
)
{
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
    entryPointCursor["scale"].setData(scale);
    entryPointCursor["groupBuffer"].setBinding(buffer);
    entryPointCursor["count"].setData(groupCount);
    passEncoder->dispatchCompute(groupCount, 1, 1);
}

void dispatchGridSum(IDevice* device, IComputePipeline* pipeline, IBuffer* buffer, uint32_t groupCount)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->submit(recordGridSum(device, pipeline, buffer, groupCount)));
    REQUIRE_CALL(queue->waitOnHost());
}

uint32_t readUint(IDevice* device, IBuffer* buffer, uint32_t index)
{
    uint32_t value = 0;
    REQUIRE_CALL(device->readBuffer(buffer, index * sizeof(uint32_t), sizeof(uint32_t), &value));
    return value;
}

ComPtr<IDevice> createDeviceWithCallback(GpuTestContext* ctx, ErrorCapture& callback, bool enableValidation)
{
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    extraOptions.enableValidation = enableValidation;
    ComPtr<IDevice> device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(device);
    return device;
}

bool hasDebugLayer(IDevice* device)
{
    return dynamic_cast<debug::DebugDevice*>(device) != nullptr;
}

// A specializable program: the group values come from an IGroupValue entry-point parameter.
// Runs the grid sum with the largest group count the pipeline reports for the bound root object.
void testSpecializable(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-cooperative-launch",
        "computeSpecialized",
        shaderProgram.writeRef(),
        &slangReflection
    ));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderObject> groupValue;
    REQUIRE_CALL(device->createShaderObjectFromType(
        shaderProgram,
        slangReflection->findTypeByName("ScaledGroupIndex"),
        ShaderObjectContainerType::None,
        groupValue.writeRef()
    ));
    ShaderCursor(groupValue)["scale"].setData(uint32_t(3));

    // Without a root object the specialization arguments are unknown.
    uint32_t maxGroupCount = 1;
    CHECK_EQ(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount), SLANG_E_INVALID_ARG);
    CHECK_EQ(maxGroupCount, 0);

    // Ahead of recording, with a root object of our own.
    uint32_t aheadMaxGroupCount = 0;
    {
        ComPtr<IShaderObject> rootObject = device->createRootShaderObject(pipeline);
        REQUIRE(rootObject);
        ShaderCursor(rootObject->getEntryPoint(0))["groupValue"].setObject(groupValue);
        REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(rootObject, &aheadMaxGroupCount));
        REQUIRE(aheadMaxGroupCount > 0);
    }

    // During recording, with the root object returned by bindPipeline.
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor(rootObject->getEntryPoint(0))["groupValue"].setObject(groupValue);
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(rootObject, &maxGroupCount));
    CHECK_EQ(maxGroupCount, aheadMaxGroupCount);

    uint32_t groupCount = maxGroupCount;
    ComPtr<IBuffer> buffer = createGroupBuffer(device, groupCount + 1);
    ShaderCursor cursor(rootObject);
    cursor["buffer"].setBinding(buffer);
    cursor["groupCount"].setData(groupCount);
    passEncoder->dispatchCompute(groupCount, 1, 1);
    passEncoder->end();
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    CHECK_EQ(readUint(device, buffer, groupCount), 3 * groupCount * (groupCount + 1) / 2);
}

} // namespace

// A grid-wide barrier gives the exact sum at 1 group, half and the maximum co-resident group count.
GPU_TEST_CASE("cooperative-launch-grid-sum", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");

    ComPtr<IComputePipeline> pipeline = createCooperativePipeline(device);
    uint32_t maxGroupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount));
    REQUIRE(maxGroupCount > 0);

    ComPtr<IBuffer> buffer = createGroupBuffer(device, maxGroupCount + 1);
    uint32_t groupCounts[] = {1, std::max(maxGroupCount / 2, 1u), maxGroupCount};
    for (uint32_t groupCount : groupCounts)
    {
        CAPTURE(groupCount);
        dispatchGridSum(device, pipeline, buffer, groupCount);
        uint32_t expected = groupCount * (groupCount + 1) / 2;
        CHECK_EQ(readUint(device, buffer, groupCount), expected);
    }
}

// A deferred pipeline reports its limit on the first query, before anything has been dispatched,
// and the dispatch reuses the concrete pipeline the query created.
GPU_TEST_CASE("cooperative-launch-deferred", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");

    ComPtr<IComputePipeline> immediate = createCooperativePipeline(device, PipelineCompilationPolicy::Immediate);
    uint32_t immediateMaxGroupCount = 0;
    REQUIRE_CALL(immediate->getMaxCooperativeThreadGroupCount(nullptr, &immediateMaxGroupCount));

    ComPtr<IComputePipeline> pipeline = createCooperativePipeline(device, PipelineCompilationPolicy::Deferred);
    NativeHandle nativeHandle = {};
    CHECK_EQ(pipeline->getNativeHandle(&nativeHandle), SLANG_E_NOT_AVAILABLE);
    uint32_t maxGroupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount));
    CHECK_EQ(maxGroupCount, immediateMaxGroupCount);

    ComPtr<IBuffer> buffer = createGroupBuffer(device, maxGroupCount + 1);
    dispatchGridSum(device, pipeline, buffer, maxGroupCount);
    CHECK_EQ(readUint(device, buffer, maxGroupCount), maxGroupCount * (maxGroupCount + 1) / 2);
}

// Entry-point parameters of mixed size reach a cooperative kernel intact.
GPU_TEST_CASE("cooperative-launch-entry-point-params", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");
    ComPtr<IComputePipeline> pipeline = createEntryPointParamsPipeline(device);
    uint32_t maxGroupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount));
    for (uint32_t groupCount : {1u, maxGroupCount})
    {
        CAPTURE(groupCount);
        ComPtr<IBuffer> buffer = createGroupBuffer(device, groupCount + 1);
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        encodeEntryPointParamsSum(passEncoder, pipeline, buffer, groupCount, 5);
        passEncoder->end();
        REQUIRE_CALL(queue->submit(commandEncoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        CHECK_EQ(readUint(device, buffer, groupCount), 5 * groupCount * (groupCount + 1) / 2);
    }
}

GPU_TEST_CASE("cooperative-launch-specializable", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");
    testSpecializable(device);
}

// With the debug layer the pipeline and root objects are wrapped; the debug pipeline unwraps the root object.
GPU_TEST_CASE("cooperative-launch-specializable-debug-layer", CUDA | DontCreateDevice)
{
    ErrorCapture callback;
    ComPtr<IDevice> testDevice = createDeviceWithCallback(ctx, callback, true);
    REQUIRE(hasDebugLayer(testDevice));
    if (!testDevice->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");
    testSpecializable(testDevice);
    CHECK_EQ(callback.errors, "");
}

// A pipeline without the flag reports no cooperative group limit, and a deferred one is not compiled for it.
GPU_TEST_CASE("cooperative-launch-limit-without-flag", CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));
    for (bool deferred : {false, true})
    {
        CAPTURE(deferred);
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.compilationPolicy =
            deferred ? PipelineCompilationPolicy::Deferred : PipelineCompilationPolicy::Immediate;
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        uint32_t maxGroupCount = 1;
        CHECK_EQ(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount), SLANG_E_NOT_AVAILABLE);
        CHECK_EQ(maxGroupCount, 0);
    }
}

// A null outCount is rejected by every pipeline, with and without the debug layer.
GPU_TEST_CASE("cooperative-launch-null-count", CUDA | DontCreateDevice)
{
    for (bool enableValidation : {false, true})
    {
        CAPTURE(enableValidation);
        ErrorCapture callback;
        ComPtr<IDevice> testDevice = createDeviceWithCallback(ctx, callback, enableValidation);
        if (!testDevice->hasFeature(Feature::CooperativeLaunch))
            SKIP("cooperative launch not supported");

        ComPtr<IShaderProgram> trivialProgram;
        REQUIRE_CALL(loadProgram(testDevice, "test-compute-trivial", "computeMain", trivialProgram.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = trivialProgram.get();
        ComPtr<IComputePipeline> withoutFlag;
        REQUIRE_CALL(testDevice->createComputePipeline(pipelineDesc, withoutFlag.writeRef()));

        ComPtr<IComputePipeline> pipelines[] = {
            createCooperativePipeline(testDevice, PipelineCompilationPolicy::Immediate),
            createCooperativePipeline(testDevice, PipelineCompilationPolicy::Deferred),
            withoutFlag,
        };
        for (IComputePipeline* pipeline : pipelines)
        {
            CHECK_EQ(pipeline->getMaxCooperativeThreadGroupCount(nullptr, nullptr), SLANG_E_INVALID_ARG);
        }
        if (enableValidation)
            CHECK(callback.errors.find("'outCount' must not be null") != std::string::npos);
        else
            CHECK(callback.errors.empty());

        // The pipeline forwards its description and program, wrapped or not.
        CHECK(is_set(pipelines[0]->getDesc().flags, ComputePipelineFlags::CooperativeLaunch));
        CHECK(pipelines[0]->getProgram() != nullptr);
    }
}

// A dispatch with more groups than can be co-resident reports an error and is not launched.
// The backend reports it at submit; the debug layer reports it in dispatchCompute.
GPU_TEST_CASE("cooperative-launch-over-limit", CUDA | DontCreateDevice)
{
    for (bool enableValidation : {false, true})
    {
        CAPTURE(enableValidation);
        ErrorCapture callback;
        ComPtr<IDevice> testDevice = createDeviceWithCallback(ctx, callback, enableValidation);
        if (!testDevice->hasFeature(Feature::CooperativeLaunch))
            SKIP("cooperative launch not supported");

        ComPtr<IComputePipeline> pipeline = createCooperativePipeline(testDevice);
        uint32_t maxGroupCount = 0;
        REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &maxGroupCount));

        uint32_t groupCount = maxGroupCount + 1;
        ComPtr<IBuffer> buffer = createGroupBuffer(testDevice, groupCount + 1);
        REQUIRE(callback.errors.empty());
        ComPtr<ICommandBuffer> commandBuffer = recordGridSum(testDevice, pipeline, buffer, groupCount);
        if (hasDebugLayer(testDevice))
            CHECK(callback.errors.find("getMaxCooperativeThreadGroupCount") != std::string::npos);
        else
            CHECK(callback.errors.empty());

        auto queue = testDevice->getQueue(QueueType::Graphics);
        REQUIRE_CALL(queue->submit(commandBuffer));
        REQUIRE_CALL(queue->waitOnHost());
        CHECK(callback.errors.find("exceeds the limit") != std::string::npos);
        CHECK_EQ(readUint(testDevice, buffer, 0), 0);
        CHECK_EQ(readUint(testDevice, buffer, groupCount), 0);
    }
}

#if SLANG_RHI_ENABLE_CUDA

namespace {

rhi::cuda::DeviceImpl* getCudaDeviceImpl(IDevice* device)
{
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return checked_cast<rhi::cuda::DeviceImpl*>(debugDevice->baseObject.get());
    return checked_cast<rhi::cuda::DeviceImpl*>(device);
}

// CUDA graph entry points, which slang-rhi itself does not use.
typedef struct CUgraph_st* CUgraph;
typedef struct CUgraphExec_st* CUgraphExec;

struct CudaGraphApi
{
    SharedLibraryHandle library = nullptr;
    CUresult (*cuStreamBeginCapture)(CUstream, CUstreamCaptureMode) = nullptr;
    CUresult (*cuStreamEndCapture)(CUstream, CUgraph*) = nullptr;
    CUresult (*cuGraphInstantiateWithFlags)(CUgraphExec*, CUgraph, unsigned long long) = nullptr;
    CUresult (*cuGraphLaunch)(CUgraphExec, CUstream) = nullptr;
    CUresult (*cuGraphExecDestroy)(CUgraphExec) = nullptr;
    CUresult (*cuGraphDestroy)(CUgraph) = nullptr;

    bool load()
    {
#if SLANG_WINDOWS_FAMILY
        const char* path = "nvcuda.dll";
#else
        const char* path = "libcuda.so.1";
#endif
        if (SLANG_FAILED(loadSharedLibrary(path, library)))
            return false;
#define LOAD(name, symbol) name = decltype(name)(findSymbolAddressByName(library, symbol))
        LOAD(cuStreamBeginCapture, "cuStreamBeginCapture_v2");
        LOAD(cuStreamEndCapture, "cuStreamEndCapture");
        LOAD(cuGraphInstantiateWithFlags, "cuGraphInstantiateWithFlags");
        LOAD(cuGraphLaunch, "cuGraphLaunch");
        LOAD(cuGraphExecDestroy, "cuGraphExecDestroy");
        LOAD(cuGraphDestroy, "cuGraphDestroy");
#undef LOAD
        return cuStreamBeginCapture && cuStreamEndCapture && cuGraphInstantiateWithFlags && cuGraphLaunch &&
               cuGraphExecDestroy && cuGraphDestroy;
    }

    ~CudaGraphApi()
    {
        if (library)
            unloadSharedLibrary(library);
    }
};

} // namespace

// A cooperative dispatch submitted to a capturing stream is recorded into a CUDA graph, and
// launching the graph runs it with the grid-wide barrier intact. The driver rejects
// CU_LAUNCH_PARAM_BUFFER_POINTER on a cooperative launch under capture, so the second dispatch
// checks that entry-point parameters passed one by one survive capture and replay.
GPU_TEST_CASE("cooperative-launch-graph-capture", CUDA)
{
    if (!device->hasFeature(Feature::CooperativeLaunch))
        SKIP("cooperative launch not supported");
    CudaGraphApi graphApi;
    if (!graphApi.load())
        SKIP("CUDA graph entry points not available");

    ComPtr<IComputePipeline> pipeline = createCooperativePipeline(device);
    uint32_t groupCount = 0;
    REQUIRE_CALL(pipeline->getMaxCooperativeThreadGroupCount(nullptr, &groupCount));
    ComPtr<IBuffer> buffer = createGroupBuffer(device, groupCount + 1);
    ComPtr<IComputePipeline> paramsPipeline = createEntryPointParamsPipeline(device);
    uint32_t paramsGroupCount = 0;
    REQUIRE_CALL(paramsPipeline->getMaxCooperativeThreadGroupCount(nullptr, &paramsGroupCount));
    ComPtr<IBuffer> paramsBuffer = createGroupBuffer(device, paramsGroupCount + 1);
    auto queue = device->getQueue(QueueType::Graphics);
    // One command buffer with a dispatch of each kernel: global parameters only, and entry-point
    // parameters only. The command buffer must outlive the graph.
    ComPtr<ICommandBuffer> commandBuffer;
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(pipeline));
        cursor["buffer"].setBinding(buffer);
        cursor["groupCount"].setData(groupCount);
        passEncoder->dispatchCompute(groupCount, 1, 1);
        encodeEntryPointParamsSum(passEncoder, paramsPipeline, paramsBuffer, paramsGroupCount, 7);
        passEncoder->end();
        commandBuffer = commandEncoder->finish();
    }

    rhi::cuda::DeviceImpl* cudaDevice = getCudaDeviceImpl(device);
    SLANG_CUDA_CTX_SCOPE(cudaDevice);
    CUstream stream = nullptr;
    REQUIRE_EQ(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), CUDA_SUCCESS);

    REQUIRE_EQ(graphApi.cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_RELAXED), CUDA_SUCCESS);
    ICommandBuffer* commandBuffers[] = {commandBuffer};
    SubmitDesc submitDesc = {};
    submitDesc.commandBuffers = commandBuffers;
    submitDesc.commandBufferCount = 1;
    submitDesc.cudaStream = stream;
    Result submitResult = queue->submit(submitDesc);
    CUgraph graph = nullptr;
    CUresult endResult = graphApi.cuStreamEndCapture(stream, &graph);
    REQUIRE_CALL(submitResult);
    REQUIRE_EQ(endResult, CUDA_SUCCESS);
    REQUIRE(graph);

    // Capturing records the launches without running them.
    CHECK_EQ(readUint(device, buffer, groupCount), 0);
    CHECK_EQ(readUint(device, paramsBuffer, paramsGroupCount), 0);

    CUgraphExec graphExec = nullptr;
    REQUIRE_EQ(graphApi.cuGraphInstantiateWithFlags(&graphExec, graph, 0), CUDA_SUCCESS);
    for (int launch = 0; launch < 2; ++launch)
    {
        CAPTURE(launch);
        REQUIRE_EQ(graphApi.cuGraphLaunch(graphExec, stream), CUDA_SUCCESS);
        REQUIRE_EQ(cuStreamSynchronize(stream), CUDA_SUCCESS);
        CHECK_EQ(readUint(device, buffer, groupCount), groupCount * (groupCount + 1) / 2);
        CHECK_EQ(readUint(device, paramsBuffer, paramsGroupCount), 7 * paramsGroupCount * (paramsGroupCount + 1) / 2);
        // Clear the sums, so that the next launch has to write them again.
        uint32_t zero = 0;
        REQUIRE_EQ(
            cuMemcpyHtoD(buffer->getDeviceAddress() + groupCount * sizeof(uint32_t), &zero, sizeof(zero)),
            CUDA_SUCCESS
        );
        REQUIRE_EQ(
            cuMemcpyHtoD(paramsBuffer->getDeviceAddress() + paramsGroupCount * sizeof(uint32_t), &zero, sizeof(zero)),
            CUDA_SUCCESS
        );
    }

    CHECK_EQ(graphApi.cuGraphExecDestroy(graphExec), CUDA_SUCCESS);
    CHECK_EQ(graphApi.cuGraphDestroy(graph), CUDA_SUCCESS);
    CHECK_EQ(cuStreamDestroy(stream), CUDA_SUCCESS);
}

#endif // SLANG_RHI_ENABLE_CUDA

// Backends without a forward-progress guarantee across groups do not report the feature and refuse the flag.
GPU_TEST_CASE("cooperative-launch-not-supported", D3D11 | D3D12 | Vulkan | Metal | CPU | WGPU | DontCreateDevice)
{
    ErrorCapture callback;
    ComPtr<IDevice> testDevice = createDeviceWithCallback(ctx, callback, false);
    CHECK_FALSE(testDevice->hasFeature(Feature::CooperativeLaunch));

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(testDevice, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.flags = ComputePipelineFlags::CooperativeLaunch;
    ComPtr<IComputePipeline> pipeline;
    CHECK_EQ(testDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()), SLANG_E_NOT_AVAILABLE);
    CHECK_FALSE(pipeline);
    CHECK(callback.errors.find("Cooperative launch is not supported") != std::string::npos);
}
