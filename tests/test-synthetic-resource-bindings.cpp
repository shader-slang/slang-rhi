#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

namespace {

static Result loadModuleFromSource(
    slang::ISession* slangSession,
    std::string_view source,
    slang::IModule** outModule
)
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

static Result createComputeProgramWithSyntheticResource(
    IDevice* device,
    std::string_view source,
    const SyntheticResourceBindingDesc& syntheticResourceDesc,
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
    syntheticResourcesDesc.resources = &syntheticResourceDesc;
    syntheticResourcesDesc.resourceCount = 1;

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    programDesc.next = &syntheticResourcesDesc;

    Result result = device->createShaderProgram(programDesc, outProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
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
    std::vector<SyntheticResourceBindingDesc>* outSyntheticResources = nullptr
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

    ComPtr<slang::IBlob> entryPointCode;
    SLANG_RETURN_ON_FAIL(linkedProgram->getEntryPointCode(0, 0, entryPointCode.writeRef(), diagnosticsBlob.writeRef()));
    diagnoseIfNeeded(diagnosticsBlob);

    ComPtr<slang::IMetadata> metadata;
    SLANG_RETURN_ON_FAIL(
        linkedProgram->getEntryPointMetadata(0, 0, metadata.writeRef(), diagnosticsBlob.writeRef())
    );
    diagnoseIfNeeded(diagnosticsBlob);

    auto* coverageMetadata = (slang::ICoverageTracingMetadata*)metadata->castAs(
        slang::ICoverageTracingMetadata::getTypeGuid()
    );
    SLANG_RHI_ASSERT(coverageMetadata);
    auto* syntheticMetadata = (slang::ISyntheticResourceMetadata*)metadata->castAs(
        slang::ISyntheticResourceMetadata::getTypeGuid()
    );
    SLANG_RHI_ASSERT(syntheticMetadata);

    std::vector<SyntheticResourceBindingDesc> syntheticResources;
    const uint32_t resourceCount = syntheticMetadata->getResourceCount();
    syntheticResources.reserve(resourceCount);

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
        resourceDesc.debugName = info.debugName;
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

    if (outSyntheticResources)
        *outSyntheticResources = syntheticResources;
    return SLANG_OK;
}

static ComPtr<IBuffer> createTestBuffer(IDevice* device)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = 256;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    return buffer;
}

} // namespace

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
    CHECK(location.debugName != nullptr);
    CHECK(std::string_view(location.debugName) == "__syntheticCoverage");

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

GPU_TEST_CASE("synthetic-resource-bindings-from-slang-metadata", Vulkan | DontCreateDevice)
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
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(createComputeProgramFromCoverageMetadata(
        localDevice,
        kShaderSource,
        shaderProgram.writeRef(),
        &syntheticResources
    ));

    REQUIRE_EQ(syntheticResources.size(), 1u);
    CHECK_EQ(syntheticResources[0].bindingType, slang::BindingType::MutableRawBuffer);
    CHECK_EQ(syntheticResources[0].scope, SyntheticResourceScope::Global);
    CHECK_EQ(syntheticResources[0].access, SyntheticResourceAccess::ReadWrite);
    CHECK_EQ(syntheticResources[0].binding, int32_t(kCoverageBinding));
    CHECK_EQ(syntheticResources[0].space, int32_t(kCoverageSpace));
    CHECK(std::string_view(syntheticResources[0].debugName) == "__slang_coverage");

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
    CHECK(std::string_view(location.debugName) == "__slang_coverage");

    ComPtr<IComputePipeline> pipeline;
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    REQUIRE_CALL(localDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(localDevice->createRootShaderObject(shaderProgram.get(), rootObject.writeRef()));

    auto buffer = createTestBuffer(localDevice);
    REQUIRE_CALL(bindSyntheticResource(shaderProgram.get(), rootObject.get(), syntheticResources[0].id, Binding(buffer)));

    localDevice->getQueue(QueueType::Graphics)->waitOnHost();

    buffer.setNull();
    rootObject.setNull();
    pipeline.setNull();
    syntheticProgram.setNull();
    shaderProgram.setNull();
    localDevice.setNull();
}
