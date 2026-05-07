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
}
