#include "testing.h"

#include <slang-rhi/synthetic-bindings.h>

#include <cstring>
#include <string>

using namespace rhi;
using namespace rhi::testing;

namespace {

static Result loadModuleFromSource(slang::ISession* slangSession, std::string_view source, slang::IModule** outModule)
{
    static uint64_t counter = 0;
    std::string moduleName = "synthetic_resource_module_" + std::to_string(counter++);
    auto srcBlob = UnownedBlob::create(source.data(), source.size());
    ComPtr<slang::IBlob> diagnosticsBlob;
    *outModule =
        slangSession->loadModuleFromSource(moduleName.data(), moduleName.data(), srcBlob, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!*outModule)
        return SLANG_FAIL;
    return SLANG_OK;
}

static Result createComputeProgramWithSyntheticResources(
    IDevice* device,
    std::string_view source,
    const SyntheticResourceBindingDesc* syntheticResourceDescs,
    uint32_t syntheticResourceCount,
    IShaderProgram** outProgram
)
{
    auto slangSession = device->getSlangSession();
    slang::IModule* module = nullptr;
    SLANG_RETURN_ON_FAIL(loadModuleFromSource(slangSession, source, &module));

    std::vector<ComPtr<slang::IComponentType>> componentTypes;
    componentTypes.push_back(ComPtr<slang::IComponentType>(module));

    for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); ++i)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->getDefinedEntryPoint(i, entryPoint.writeRef()));
        componentTypes.push_back(ComPtr<slang::IComponentType>(entryPoint.get()));
    }

    std::vector<slang::IComponentType*> rawComponentTypes;
    for (auto& componentType : componentTypes)
        rawComponentTypes.push_back(componentType.get());

    ComPtr<slang::IComponentType> linkedProgram;
    ComPtr<slang::IBlob> diagnosticsBlob;
    SLANG_RETURN_ON_FAIL(slangSession->createCompositeComponentType(
        rawComponentTypes.data(),
        rawComponentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    ));
    diagnoseIfNeeded(diagnosticsBlob);

    ShaderProgramSyntheticResourcesDesc syntheticResourcesDesc = {};
    syntheticResourcesDesc.resources = syntheticResourceDescs;
    syntheticResourcesDesc.resourceCount = syntheticResourceCount;

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    if (syntheticResourceDescs || syntheticResourceCount)
        programDesc.next = &syntheticResourcesDesc;

    Result result = device->createShaderProgram(programDesc, outProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
}

static Result createComputeProgramWithSyntheticResource(
    IDevice* device,
    std::string_view source,
    const SyntheticResourceBindingDesc& syntheticResourceDesc,
    IShaderProgram** outProgram
)
{
    return createComputeProgramWithSyntheticResources(device, source, &syntheticResourceDesc, 1, outProgram);
}

static Result createComputeProgram(IDevice* device, std::string_view source, IShaderProgram** outProgram)
{
    return createComputeProgramWithSyntheticResources(device, source, nullptr, 0, outProgram);
}

static SyntheticResourceScope mapSyntheticResourceScope(slang::SyntheticResourceScope scope)
{
    switch (scope)
    {
    case slang::SyntheticResourceScope::Global:
        return SyntheticResourceScope::Global;
    case slang::SyntheticResourceScope::EntryPoint:
        return SyntheticResourceScope::EntryPoint;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unhandled synthetic resource scope");
        return SyntheticResourceScope::Global;
    }
}

static SyntheticResourceAccess mapSyntheticResourceAccess(slang::SyntheticResourceAccess access)
{
    switch (access)
    {
    case slang::SyntheticResourceAccess::Read:
        return SyntheticResourceAccess::Read;
    case slang::SyntheticResourceAccess::Write:
        return SyntheticResourceAccess::Write;
    case slang::SyntheticResourceAccess::ReadWrite:
        return SyntheticResourceAccess::ReadWrite;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unhandled synthetic resource access");
        return SyntheticResourceAccess::Read;
    }
}

static Result createComputeProgramFromCoverageMetadata(
    IDevice* device,
    std::string_view source,
    IShaderProgram** outProgram,
    std::vector<SyntheticResourceBindingDesc>* outSyntheticResources = nullptr,
    std::vector<std::string>* outSyntheticResourceDebugNames = nullptr,
    uint32_t* outCoverageCounterCount = nullptr
)
{
    auto slangSession = device->getSlangSession();
    slang::IModule* module = nullptr;
    SLANG_RETURN_ON_FAIL(loadModuleFromSource(slangSession, source, &module));

    std::vector<ComPtr<slang::IComponentType>> componentTypes;
    componentTypes.push_back(ComPtr<slang::IComponentType>(module));

    for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); ++i)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->getDefinedEntryPoint(i, entryPoint.writeRef()));
        componentTypes.push_back(ComPtr<slang::IComponentType>(entryPoint.get()));
    }

    std::vector<slang::IComponentType*> rawComponentTypes;
    for (auto& componentType : componentTypes)
        rawComponentTypes.push_back(componentType.get());

    ComPtr<slang::IComponentType> linkedProgram;
    ComPtr<slang::IBlob> diagnosticsBlob;
    SLANG_RETURN_ON_FAIL(slangSession->createCompositeComponentType(
        rawComponentTypes.data(),
        rawComponentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    ));
    diagnoseIfNeeded(diagnosticsBlob);

    ComPtr<slang::IMetadata> metadata;
    SLANG_RETURN_ON_FAIL(linkedProgram->getEntryPointMetadata(0, 0, metadata.writeRef(), diagnosticsBlob.writeRef()));
    diagnoseIfNeeded(diagnosticsBlob);

    auto* coverageMetadata =
        (slang::ICoverageTracingMetadata*)metadata->castAs(slang::ICoverageTracingMetadata::getTypeGuid());
    REQUIRE(coverageMetadata != nullptr);
    auto* syntheticMetadata =
        (slang::ISyntheticResourceMetadata*)metadata->castAs(slang::ISyntheticResourceMetadata::getTypeGuid());
    REQUIRE(syntheticMetadata != nullptr);

    std::vector<SyntheticResourceBindingDesc> syntheticResources;
    std::vector<std::string> syntheticResourceDebugNames;
    const uint32_t resourceCount = syntheticMetadata->getResourceCount();
    syntheticResources.reserve(resourceCount);
    syntheticResourceDebugNames.reserve(resourceCount);

    for (uint32_t i = 0; i < resourceCount; ++i)
    {
        slang::SyntheticResourceInfo info = {};
        SLANG_RETURN_ON_FAIL(syntheticMetadata->getResourceInfo(i, &info));

        SyntheticResourceBindingDesc resourceDesc = {};
        resourceDesc.id = info.id;
        resourceDesc.bindingType = info.bindingType;
        resourceDesc.arraySize = info.arraySize;
        resourceDesc.scope = mapSyntheticResourceScope(info.scope);
        resourceDesc.access = mapSyntheticResourceAccess(info.access);
        resourceDesc.entryPointIndex = info.entryPointIndex;
        resourceDesc.space = info.space;
        resourceDesc.binding = info.binding;
        resourceDesc.uniformOffset = info.uniformOffset;
        resourceDesc.uniformStride = info.uniformStride;
        if (info.debugName)
        {
            syntheticResourceDebugNames.push_back(info.debugName);
            resourceDesc.debugName = syntheticResourceDebugNames.back().c_str();
        }
        else
        {
            syntheticResourceDebugNames.emplace_back();
            resourceDesc.debugName = nullptr;
        }
        syntheticResources.push_back(resourceDesc);
    }

    ShaderProgramSyntheticResourcesDesc syntheticResourcesDesc = {};
    syntheticResourcesDesc.resources = syntheticResources.data();
    syntheticResourcesDesc.resourceCount = (uint32_t)syntheticResources.size();

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    programDesc.next = &syntheticResourcesDesc;

    Result result = device->createShaderProgram(programDesc, outProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (SLANG_FAILED(result))
        return result;

    if (outSyntheticResourceDebugNames)
        *outSyntheticResourceDebugNames = syntheticResourceDebugNames;
    if (outSyntheticResources)
    {
        *outSyntheticResources = syntheticResources;
        for (size_t i = 0; i < outSyntheticResources->size(); ++i)
        {
            (*outSyntheticResources)[i].debugName =
                outSyntheticResourceDebugNames && !(*outSyntheticResourceDebugNames)[i].empty()
                    ? (*outSyntheticResourceDebugNames)[i].c_str()
                    : nullptr;
        }
    }
    if (outCoverageCounterCount)
        *outCoverageCounterCount = coverageMetadata->getCounterCount();
    return SLANG_OK;
}

static ComPtr<IBuffer> createTestBuffer(IDevice* device, size_t size = 256, const void* initData = nullptr)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initData, buffer.writeRef()));
    return buffer;
}

} // namespace

GPU_TEST_CASE("synthetic-resource-bindings-interface-is-opt-in", ALL)
{
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[0] = tid.x;
}
)";

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(createComputeProgram(device, kShaderSource, shaderProgram.writeRef()));

    ComPtr<ISyntheticShaderProgram> syntheticProgram;
    CHECK_EQ(
        shaderProgram->queryInterface(ISyntheticShaderProgram::getTypeGuid(), (void**)syntheticProgram.writeRef()),
        SLANG_E_NO_INTERFACE
    );
}

GPU_TEST_CASE("synthetic-resource-bindings-unsupported-backends", ALL & ~Vulkan & ~CUDA)
{
    static constexpr uint32_t kSyntheticResourceID = 17;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[0] = tid.x;
}
)";

    SyntheticResourceBindingDesc syntheticResourceDesc = {};
    syntheticResourceDesc.id = kSyntheticResourceID;
    syntheticResourceDesc.bindingType = slang::BindingType::MutableRawBuffer;
    syntheticResourceDesc.arraySize = 1;
    syntheticResourceDesc.scope = SyntheticResourceScope::Global;
    syntheticResourceDesc.access = SyntheticResourceAccess::ReadWrite;
    syntheticResourceDesc.space = 0;
    syntheticResourceDesc.binding = 11;
    syntheticResourceDesc.uniformOffset = 16;
    syntheticResourceDesc.uniformStride = 16;
    syntheticResourceDesc.debugName = "__syntheticUnsupported";

    ComPtr<IShaderProgram> shaderProgram;
    CHECK_EQ(
        createComputeProgramWithSyntheticResource(
            device,
            kShaderSource,
            syntheticResourceDesc,
            shaderProgram.writeRef()
        ),
        SLANG_E_NOT_IMPLEMENTED
    );
}

GPU_TEST_CASE("synthetic-resource-bindings", Vulkan | CUDA)
{
    static constexpr uint32_t kSyntheticResourceID = 17;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;
uint4 gPad0;
uint4 gPad1;

[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[0] = tid.x + gPad0.x + gPad1.x;
}
)";

    SyntheticResourceBindingDesc syntheticResourceDesc = {};
    syntheticResourceDesc.id = kSyntheticResourceID;
    syntheticResourceDesc.bindingType = slang::BindingType::MutableRawBuffer;
    syntheticResourceDesc.arraySize = 1;
    syntheticResourceDesc.scope = SyntheticResourceScope::Global;
    syntheticResourceDesc.access = SyntheticResourceAccess::ReadWrite;
    syntheticResourceDesc.space = 0;
    syntheticResourceDesc.binding = 11;
    syntheticResourceDesc.uniformOffset = 16;
    syntheticResourceDesc.uniformStride = 16;
    syntheticResourceDesc.debugName = "__syntheticCoverage";

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(createComputeProgramWithSyntheticResource(
        device,
        kShaderSource,
        syntheticResourceDesc,
        shaderProgram.writeRef()
    ));

    ComPtr<ISyntheticShaderProgram> syntheticProgram;
    REQUIRE_CALL(
        shaderProgram->queryInterface(ISyntheticShaderProgram::getTypeGuid(), (void**)syntheticProgram.writeRef())
    );
    CHECK_EQ(syntheticProgram->getSyntheticBindingCount(), 1u);

    SyntheticBindingLocation location = {};
    location.structSize = sizeof(SyntheticBindingLocation);
    REQUIRE_CALL(syntheticProgram->getSyntheticBindingLocation(0, &location));
    CHECK_EQ(location.syntheticResourceID, kSyntheticResourceID);
    CHECK_EQ(location.bindingType, slang::BindingType::MutableRawBuffer);
    CHECK_EQ(location.arraySize, 1u);
    CHECK_EQ(location.scope, SyntheticResourceScope::Global);
    CHECK_EQ(location.entryPointIndex, -1);
    REQUIRE(location.debugName != nullptr);
    CHECK_EQ(std::strcmp(location.debugName, "__syntheticCoverage"), 0);

    SyntheticBindingLocation foundLocation = {};
    foundLocation.structSize = sizeof(SyntheticBindingLocation);
    REQUIRE_CALL(syntheticProgram->findSyntheticBindingLocationByID(kSyntheticResourceID, &foundLocation));
    CHECK_EQ(foundLocation.offset, location.offset);

    if (device->getDeviceType() == DeviceType::CUDA)
    {
        CHECK_EQ(location.offset.uniformOffset, 16u);
    }

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram.get(), rootObject.writeRef()));

    auto buffer = createTestBuffer(device);
    REQUIRE_CALL(bindSyntheticResource(shaderProgram.get(), rootObject.get(), kSyntheticResourceID, Binding(buffer)));

    buffer.setNull();
    rootObject.setNull();
    syntheticProgram.setNull();
    shaderProgram.setNull();
}

GPU_TEST_CASE("synthetic-resource-bindings-invalid-descs", Vulkan | CUDA)
{
    static constexpr uint32_t kSyntheticResourceID = 17;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[0] = tid.x;
}
)";

    auto makeSyntheticResourceDesc = []()
    {
        SyntheticResourceBindingDesc desc = {};
        desc.id = kSyntheticResourceID;
        desc.bindingType = slang::BindingType::MutableRawBuffer;
        desc.arraySize = 1;
        desc.scope = SyntheticResourceScope::Global;
        desc.access = SyntheticResourceAccess::ReadWrite;
        desc.space = 0;
        desc.binding = 11;
        desc.uniformOffset = 16;
        desc.uniformStride = 16;
        desc.debugName = "__syntheticInvalid";
        return desc;
    };

    auto checkCreateFails = [&](const SyntheticResourceBindingDesc& desc, Result expectedResult)
    {
        ComPtr<IShaderProgram> shaderProgram;
        Result result =
            createComputeProgramWithSyntheticResource(device, kShaderSource, desc, shaderProgram.writeRef());
        CHECK_EQ(result, expectedResult);
    };

    SyntheticResourceBindingDesc desc = makeSyntheticResourceDesc();
    desc.bindingType = slang::BindingType::Unknown;
    checkCreateFails(desc, SLANG_E_INVALID_ARG);

    desc = makeSyntheticResourceDesc();
    desc.space = -2;
    checkCreateFails(desc, SLANG_E_INVALID_ARG);

    desc = makeSyntheticResourceDesc();
    desc.binding = -2;
    checkCreateFails(desc, SLANG_E_INVALID_ARG);

    desc = makeSyntheticResourceDesc();
    desc.uniformOffset = -2;
    checkCreateFails(desc, SLANG_E_INVALID_ARG);

    desc = makeSyntheticResourceDesc();
    desc.uniformStride = -1;
    checkCreateFails(desc, SLANG_E_INVALID_ARG);

    if (device->getDeviceType() == DeviceType::CUDA)
    {
        desc = makeSyntheticResourceDesc();
        desc.bindingType = slang::BindingType::Sampler;
        checkCreateFails(desc, SLANG_E_NOT_IMPLEMENTED);
    }
}

GPU_TEST_CASE("synthetic-resource-bindings-layout-failure", Vulkan | CUDA)
{
    static constexpr uint32_t kFirstSyntheticResourceID = 17;
    static constexpr uint32_t kSecondSyntheticResourceID = 18;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[0] = tid.x;
}
)";

    auto makeSyntheticResourceDesc = [](uint32_t id)
    {
        SyntheticResourceBindingDesc desc = {};
        desc.id = id;
        desc.bindingType = slang::BindingType::MutableRawBuffer;
        desc.arraySize = 1;
        desc.scope = SyntheticResourceScope::Global;
        desc.access = SyntheticResourceAccess::ReadWrite;
        desc.space = 0;
        desc.binding = 11;
        desc.uniformOffset = 16;
        desc.uniformStride = 16;
        desc.debugName = "__syntheticLayoutFailure";
        return desc;
    };

    SyntheticResourceBindingDesc descs[] = {
        makeSyntheticResourceDesc(kFirstSyntheticResourceID),
        makeSyntheticResourceDesc(kSecondSyntheticResourceID),
    };

    Result expectedResult = SLANG_OK;
    if (device->getDeviceType() == DeviceType::Vulkan)
    {
        // Two synthetic resources at the same descriptor binding pass the generic
        // descriptor validation but fail when the Vulkan layout builder adds the
        // second resource.
        expectedResult = SLANG_E_INVALID_ARG;
    }
    else if (device->getDeviceType() == DeviceType::CUDA)
    {
        // CUDA accepts the first synthetic raw buffer, then fails on the second
        // resource because samplers are not supported as synthetic CUDA bindings.
        descs[1].bindingType = slang::BindingType::Sampler;
        expectedResult = SLANG_E_NOT_IMPLEMENTED;
    }
    else
    {
        return;
    }

    ComPtr<IShaderProgram> shaderProgram;
    {
        SLANG_RHI_DISABLE_ASSERT_SCOPE();
        Result result = createComputeProgramWithSyntheticResources(
            device,
            kShaderSource,
            descs,
            (uint32_t)SLANG_COUNT_OF(descs),
            shaderProgram.writeRef()
        );
        CHECK_EQ(result, expectedResult);
    }
    CHECK(shaderProgram == nullptr);
}

GPU_TEST_CASE("synthetic-resource-bindings-from-slang-metadata", Vulkan | CUDA | DontCreateDevice)
{
    static constexpr uint32_t kCoverageBinding = 11;
    static constexpr uint32_t kCoverageSpace = 3;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint accum = tid.x;
    for (uint i = 0; i < 4; ++i)
    {
        if ((i & 1u) == 0u)
            accum += i;
        else
            accum += i * 2u;
    }
    outBuffer[0] = accum;
}
)";

    DeviceExtraOptions extraOptions = {};

    slang::CompilerOptionEntry traceCoverageOption = {};
    traceCoverageOption.name = slang::CompilerOptionName::TraceCoverage;
    traceCoverageOption.value.kind = slang::CompilerOptionValueKind::Int;
    traceCoverageOption.value.intValue0 = 1;
    extraOptions.compilerOptions.push_back(traceCoverageOption);

    slang::CompilerOptionEntry traceCoverageBindingOption = {};
    traceCoverageBindingOption.name = slang::CompilerOptionName::TraceCoverageBinding;
    traceCoverageBindingOption.value.kind = slang::CompilerOptionValueKind::Int;
    traceCoverageBindingOption.value.intValue0 = kCoverageBinding;
    traceCoverageBindingOption.value.intValue1 = kCoverageSpace;
    extraOptions.compilerOptions.push_back(traceCoverageBindingOption);

    auto localDevice = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);

    std::vector<SyntheticResourceBindingDesc> syntheticResources;
    std::vector<std::string> syntheticResourceDebugNames;
    uint32_t coverageCounterCount = 0;
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(createComputeProgramFromCoverageMetadata(
        localDevice,
        kShaderSource,
        shaderProgram.writeRef(),
        &syntheticResources,
        &syntheticResourceDebugNames,
        &coverageCounterCount
    ));

    REQUIRE_EQ(syntheticResources.size(), 1u);
    REQUIRE_GT(coverageCounterCount, 0u);
    CHECK_EQ(syntheticResources[0].bindingType, slang::BindingType::MutableRawBuffer);
    CHECK_EQ(syntheticResources[0].scope, SyntheticResourceScope::Global);
    CHECK_EQ(syntheticResources[0].access, SyntheticResourceAccess::ReadWrite);
    CHECK_EQ(syntheticResources[0].binding, int32_t(kCoverageBinding));
    CHECK_EQ(syntheticResources[0].space, int32_t(kCoverageSpace));
    REQUIRE(syntheticResources[0].debugName != nullptr);
    CHECK_EQ(std::strcmp(syntheticResources[0].debugName, "__slang_coverage"), 0);

    ComPtr<ISyntheticShaderProgram> syntheticProgram;
    REQUIRE_CALL(
        shaderProgram->queryInterface(ISyntheticShaderProgram::getTypeGuid(), (void**)syntheticProgram.writeRef())
    );
    REQUIRE_EQ(syntheticProgram->getSyntheticBindingCount(), 1u);

    SyntheticBindingLocation location = {};
    location.structSize = sizeof(SyntheticBindingLocation);
    REQUIRE_CALL(syntheticProgram->getSyntheticBindingLocation(0, &location));
    CHECK_EQ(location.syntheticResourceID, syntheticResources[0].id);
    CHECK_EQ(location.bindingType, syntheticResources[0].bindingType);
    CHECK_EQ(location.scope, syntheticResources[0].scope);
    REQUIRE(location.debugName != nullptr);
    CHECK_EQ(std::strcmp(location.debugName, "__slang_coverage"), 0);

    ComPtr<IComputePipeline> pipeline;
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    REQUIRE_CALL(localDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(localDevice->createRootShaderObject(shaderProgram.get(), rootObject.writeRef()));

    const uint32_t initialOutput = 0;
    auto outputBuffer = createTestBuffer(localDevice, sizeof(uint32_t), &initialOutput);

    std::vector<uint32_t> zeroCoverage(coverageCounterCount, 0);
    auto coverageBuffer = createTestBuffer(localDevice, zeroCoverage.size() * sizeof(uint32_t), zeroCoverage.data());

    ShaderCursor(rootObject)["outBuffer"].setBinding(outputBuffer);
    REQUIRE_CALL(
        bindSyntheticResource(shaderProgram.get(), rootObject.get(), syntheticResources[0].id, Binding(coverageBuffer))
    );

    auto queue = localDevice->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    passEncoder->bindPipeline(pipeline, rootObject);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    compareComputeResult(localDevice, outputBuffer, std::array<uint32_t, 1>{10u});

    ComPtr<ISlangBlob> coverageBlob;
    REQUIRE_CALL(
        localDevice->readBuffer(coverageBuffer, 0, zeroCoverage.size() * sizeof(uint32_t), coverageBlob.writeRef())
    );
    const uint32_t* coverageData = reinterpret_cast<const uint32_t*>(coverageBlob->getBufferPointer());
    uint64_t totalHits = 0;
    for (uint32_t i = 0; i < coverageCounterCount; ++i)
        totalHits += coverageData[i];
    CHECK_GT(totalHits, 0u);

    coverageBuffer.setNull();
    outputBuffer.setNull();
    rootObject.setNull();
    pipeline.setNull();
    syntheticProgram.setNull();
    shaderProgram.setNull();
    localDevice.setNull();
}
