#include "testing.h"

#include <slang-rhi/synthetic-bindings.h>

#include <cstring>
#include <string>
#include <algorithm>

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-shader-object-layout.h"
#include "vulkan/vk-shader-program.h"
#endif

using namespace rhi;
using namespace rhi::testing;

namespace {

// Negative tests capture expected layout errors without disabling assertions.
struct SyntheticLayoutDebugCallback : IDebugCallback
{
    std::string errors;

    void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        if (type == DebugMessageType::Error)
        {
            CHECK(source == DebugMessageSource::Layer);
            errors += message;
            errors += '\n';
        }
    }
};

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
    IShaderProgram** outProgram,
    bool forceSyntheticResourcesDesc = false
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
    if (forceSyntheticResourcesDesc || syntheticResourceDescs || syntheticResourceCount)
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

static Result createComputeProgramWithEmptySyntheticResourceDesc(
    IDevice* device,
    std::string_view source,
    IShaderProgram** outProgram
)
{
    return createComputeProgramWithSyntheticResources(device, source, nullptr, 0, outProgram, true);
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
    uint32_t* outCoverageCounterCount = nullptr,
    uint32_t* outCounterByteWidth = nullptr
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
    if (outCounterByteWidth)
    {
        slang::CoverageBufferInfo bufferInfo = {};
        SLANG_RETURN_ON_FAIL(coverageMetadata->getBufferInfo(&bufferInfo));
        *outCounterByteWidth = bufferInfo.elementByteWidth;
    }
    if (outCoverageCounterCount)
        *outCoverageCounterCount = coverageMetadata->getCounterCount();
    return SLANG_OK;
}

static ComPtr<IBuffer> createTestBuffer(
    IDevice* device,
    size_t size = 256,
    const void* initData = nullptr,
    uint32_t elementSize = sizeof(uint32_t)
)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = elementSize;
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initData, buffer.writeRef()));
    return buffer;
}

} // namespace

#if SLANG_RHI_ENABLE_VULKAN
// Compare the reflected portion of an augmented layout without relying on Vulkan
// handle identity. The same shader must retain the same slots and offsets whether
// the extension is absent, empty, or adds a binding alongside a child set.
static void compareReflectedBindingLocations(vk::ShaderObjectLayoutImpl* a, vk::ShaderObjectLayoutImpl* b)
{
    REQUIRE_LE(a->getBindingRangeCount(), b->getBindingRangeCount());
    for (uint32_t i = 0; i < a->getBindingRangeCount(); ++i)
    {
        const auto& x = a->getBindingRange(i);
        const auto& y = b->getBindingRange(i);
        CHECK(x.bindingType == y.bindingType);
        CHECK_EQ(x.count, y.count);
        CHECK_EQ(x.slotIndex, y.slotIndex);
        CHECK_EQ(x.subObjectIndex, y.subObjectIndex);
        CHECK_EQ(x.bindingOffset, y.bindingOffset);
        CHECK_EQ(x.setOffset, y.setOffset);
    }
    REQUIRE_EQ(a->getSubObjectRanges().size(), b->getSubObjectRanges().size());
    for (size_t i = 0; i < a->getSubObjectRanges().size(); ++i)
    {
        const auto& x = a->getSubObjectRange(uint32_t(i));
        const auto& y = b->getSubObjectRange(uint32_t(i));
        CHECK_EQ(x.bindingRangeIndex, y.bindingRangeIndex);
        if (x.layout)
        {
            REQUIRE(y.layout);
            compareReflectedBindingLocations(x.layout, y.layout);
        }
    }
}

GPU_TEST_CASE_EX("synthetic-resource-bindings-layout-composition", Vulkan, DebugLayerOptions{})
{
    bool array = false;
    bool specialize = false;
    SUBCASE("named parameter blocks") {}
    SUBCASE("resource arrays in parameter blocks")
    {
        array = true;
    }
    SUBCASE("specialized program")
    {
        specialize = true;
    }
    std::string source =
        array ? "struct Params { StructuredBuffer<uint> inputs[2]; };\n" : "struct Params { uint value; };\n";
    source += "ParameterBlock<Params> left; ParameterBlock<Params> right;\n";
    if (specialize)
        source +=
            "interface ITransform { uint apply(uint x); }; "
            "struct Transform : ITransform { uint apply(uint x) { return x + 5; } };\n";
    source += "RWStructuredBuffer<uint> output;\n[shader(\"compute\")][numthreads(1,1,1)]\n";
    source += specialize ? "void computeMain(uniform ITransform transform) { output[0] = transform.apply("
                         : "void computeMain() { output[0] = ";
    source += array ? "left.inputs[0][0] + 2 * right.inputs[1][0]" : "left.value + 2 * right.value";
    source += specialize ? "); }" : "; }";
    SyntheticResourceBindingDesc resource = {};
    resource.id = 1;
    resource.bindingType = slang::BindingType::MutableRawBuffer;
    resource.arraySize = 1;
    resource.space = 1;
    resource.binding = 11;
    resource.access = SyntheticResourceAccess::ReadWrite;

    ComPtr<IShaderProgram> programs[3];
    REQUIRE_CALL(createComputeProgram(device, source, programs[0].writeRef()));
    REQUIRE_CALL(createComputeProgramWithEmptySyntheticResourceDesc(device, source, programs[1].writeRef()));
    REQUIRE_CALL(createComputeProgramWithSyntheticResource(device, source, resource, programs[2].writeRef()));
    vk::RootShaderObjectLayoutImpl* layouts[3];
    for (uint32_t i = 0; i < 3; ++i)
        layouts[i] = static_cast<vk::ShaderProgramImpl*>(programs[i].get())->m_rootShaderObjectLayout;

    CHECK_FALSE(layouts[0]->m_descriptorSetComposition);
    CHECK_FALSE(layouts[1]->m_descriptorSetComposition);
    REQUIRE(layouts[2]->m_descriptorSetComposition);
    CHECK_EQ(layouts[0]->getOwnDescriptorSetCount(), layouts[1]->getOwnDescriptorSetCount());
    CHECK_EQ(layouts[0]->getTotalDescriptorSetCount(), layouts[1]->getTotalDescriptorSetCount());
    CHECK_EQ(layouts[0]->getTotalDescriptorSetCount(), layouts[2]->getTotalDescriptorSetCount());
    compareReflectedBindingLocations(layouts[0], layouts[1]);
    compareReflectedBindingLocations(layouts[0], layouts[2]);

    // Force a different runtime traversal order without changing any slot or
    // placement. A sequential child-set cursor would swap left and right here.
    if (!specialize)
        std::reverse(layouts[2]->m_subObjectRanges.begin(), layouts[2]->m_subObjectRanges.end());
    for (uint32_t i = 0; i < 3; ++i)
    {
        CAPTURE(i);
        ComPtr<IShaderObject> root;
        REQUIRE_CALL(device->createRootShaderObject(programs[i], root.writeRef()));
        if (specialize)
        {
            auto* type = layouts[i]->getSlangProgramLayout()->findTypeByName("Transform");
            REQUIRE(type);
            ComPtr<IShaderObject> transform;
            REQUIRE_CALL(
                device->createShaderObject(nullptr, type, ShaderObjectContainerType::None, transform.writeRef())
            );
            REQUIRE_CALL(ShaderCursor(root->getEntryPoint(0))["transform"].setObject(transform));
        }
        uint32_t left = 3, right = 11, zero = 0;
        if (array)
        {
            auto leftInput = createTestBuffer(device, sizeof(left), &left);
            auto rightInput = createTestBuffer(device, sizeof(right), &right);
            for (const char* path : {"left.inputs[0]", "left.inputs[1]"})
                REQUIRE_CALL(ShaderCursor(root).getPath(path).setBinding(leftInput));
            for (const char* path : {"right.inputs[0]", "right.inputs[1]"})
                REQUIRE_CALL(ShaderCursor(root).getPath(path).setBinding(rightInput));
        }
        else
        {
            REQUIRE_CALL(ShaderCursor(root).getPath("left.value").setData(&left, sizeof(left)));
            REQUIRE_CALL(ShaderCursor(root).getPath("right.value").setData(&right, sizeof(right)));
        }
        auto output = createTestBuffer(device, sizeof(zero), &zero);
        REQUIRE_CALL(ShaderCursor(root).getPath("output").setBinding(output));
        if (i == 2)
            REQUIRE_CALL(bindSyntheticResource(programs[i], root, resource.id, Binding(output)));
        ComputePipelineDesc desc = {};
        desc.program = programs[i];
        auto pipeline = device->createComputePipeline(desc);
        REQUIRE(pipeline);
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(pipeline, root);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        queue->submit(encoder->finish());
        queue->waitOnHost();
        compareComputeResult(device, output, std::array<uint32_t, 1>{specialize ? 30u : 25u});
    }
}
#endif

// Ordinary shader programs must not expose ISyntheticShaderProgram.
// The synthetic-resource path is opt-in and should add no observable API
// surface or per-program binding state for normal programs.
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

// An attached but empty synthetic-resource descriptor is still a no-op.
// This checks that ShaderProgramDesc.next alone does not enable the
// synthetic-resource interface or unsupported-backend failure paths.
GPU_TEST_CASE("synthetic-resource-bindings-empty-desc-is-no-op", ALL)
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
    REQUIRE_CALL(createComputeProgramWithEmptySyntheticResourceDesc(device, kShaderSource, shaderProgram.writeRef()));

    ComPtr<ISyntheticShaderProgram> syntheticProgram;
    CHECK_EQ(
        shaderProgram->queryInterface(ISyntheticShaderProgram::getTypeGuid(), (void**)syntheticProgram.writeRef()),
        SLANG_E_NO_INTERFACE
    );
}

// Backends that do not implement synthetic resources should reject only
// non-empty synthetic-resource descriptors, and should fail at program
// creation before any binding work is attempted.
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

// Verifies the explicit host-provided synthetic-resource path. The test creates
// one hidden resource, checks the query API, binds it through the helper, and
// confirms Vulkan/CUDA can create the root object with the resolved location.
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

// Invalid records and unsupported scopes/types must fail program creation.
// Collisions and descriptor-set limits must return errors with assertions enabled.
GPU_TEST_CASE("synthetic-resource-bindings-invalid-descs", Vulkan | CUDA)
{
    SyntheticLayoutDebugCallback callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    ComPtr<IDevice> testDevice = createTestingDevice(ctx, device->getDeviceType(), false, &extraOptions);
    REQUIRE(testDevice);
    static constexpr uint32_t kSyntheticResourceID = 17;
    static constexpr char kShaderSource[] = R"(
RWStructuredBuffer<uint> outBuffer;

[shader("compute")]
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
            createComputeProgramWithSyntheticResource(testDevice, kShaderSource, desc, shaderProgram.writeRef());
        CHECK_EQ(result, expectedResult);
        CHECK(shaderProgram == nullptr);
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

    desc = makeSyntheticResourceDesc();
    desc.scope = SyntheticResourceScope::EntryPoint;
    desc.entryPointIndex = 0;
    checkCreateFails(desc, SLANG_E_NOT_IMPLEMENTED);

    if (testDevice->getDeviceType() == DeviceType::Vulkan)
    {
        for (auto bindingType :
             {slang::BindingType::ConstantBuffer,
              slang::BindingType::InputRenderTarget,
              slang::BindingType::InlineUniformData,
              slang::BindingType::ParameterBlock,
              slang::BindingType::PushConstant})
        {
            desc = makeSyntheticResourceDesc();
            desc.bindingType = bindingType;
            checkCreateFails(desc, SLANG_E_NOT_IMPLEMENTED);
        }

        desc = makeSyntheticResourceDesc();
        desc.space = INT32_MAX;
        checkCreateFails(desc, SLANG_E_INVALID_ARG);
        CHECK(callback.errors.find("Descriptor set space exceeds") != std::string::npos);
        callback.errors.clear();

        desc = makeSyntheticResourceDesc();
        desc.binding = 0;
        checkCreateFails(desc, SLANG_E_INVALID_ARG);
        CHECK(callback.errors.find("Duplicate Vulkan descriptor binding") != std::string::npos);
    }

    if (testDevice->getDeviceType() == DeviceType::CUDA)
    {
        desc = makeSyntheticResourceDesc();
        desc.bindingType = slang::BindingType::Sampler;
        checkCreateFails(desc, SLANG_E_NOT_IMPLEMENTED);

        // Typed buffers are unsupported regardless of whether the descriptor
        // reserves a handle-sized slot or a pointer/count-sized slot.
        for (auto bindingType : {slang::BindingType::TypedBuffer, slang::BindingType::MutableTypedBuffer})
        {
            for (int32_t stride : {8, 16})
            {
                CAPTURE(bindingType);
                CAPTURE(stride);
                desc = makeSyntheticResourceDesc();
                desc.bindingType = bindingType;
                desc.uniformOffset = 4096;
                desc.uniformStride = stride;
                checkCreateFails(desc, SLANG_E_NOT_IMPLEMENTED);
            }
        }

        // Reject descriptors that would allocate less than the CUDA binding
        // writer's pointer/count pair or texture/acceleration-structure handle.
        for (auto bindingType :
             {slang::BindingType::RawBuffer,
              slang::BindingType::MutableRawBuffer,
              slang::BindingType::Texture,
              slang::BindingType::MutableTexture,
              slang::BindingType::CombinedTextureSampler,
              slang::BindingType::RayTracingAccelerationStructure})
        {
            desc = makeSyntheticResourceDesc();
            desc.bindingType = bindingType;
            desc.uniformOffset = 4096;
            const bool isBuffer =
                bindingType == slang::BindingType::RawBuffer || bindingType == slang::BindingType::MutableRawBuffer;
            desc.uniformStride = isBuffer ? 15 : 7;
            checkCreateFails(desc, SLANG_E_INVALID_ARG);
            desc.uniformStride++;
            ComPtr<IShaderProgram> program;
            REQUIRE_CALL(
                createComputeProgramWithSyntheticResource(testDevice, kShaderSource, desc, program.writeRef())
            );
        }
    }
}

// Layout-stage failures must propagate out of createShaderProgram instead of
// becoming asserts or later binding errors. Vulkan rejects duplicate descriptor
// bindings; CUDA rejects the unsupported second synthetic binding type.
GPU_TEST_CASE("synthetic-resource-bindings-layout-failure", Vulkan | CUDA)
{
    SyntheticLayoutDebugCallback callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    ComPtr<IDevice> testDevice = createTestingDevice(ctx, device->getDeviceType(), false, &extraOptions);
    REQUIRE(testDevice);
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
    if (testDevice->getDeviceType() == DeviceType::Vulkan)
    {
        // Two synthetic resources at the same descriptor binding pass the generic
        // descriptor validation but fail when the Vulkan layout builder adds the
        // second resource.
        expectedResult = SLANG_E_INVALID_ARG;
    }
    else if (testDevice->getDeviceType() == DeviceType::CUDA)
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
        Result result = createComputeProgramWithSyntheticResources(
            testDevice,
            kShaderSource,
            descs,
            (uint32_t)SLANG_COUNT_OF(descs),
            shaderProgram.writeRef()
        );
        CHECK_EQ(result, expectedResult);
    }
    CHECK(shaderProgram == nullptr);
    if (testDevice->getDeviceType() == DeviceType::Vulkan)
        CHECK(callback.errors.find("Duplicate Vulkan descriptor binding") != std::string::npos);
}

// End-to-end coverage metadata path: ask Slang for coverage synthetic-resource
// metadata, translate it into RHI descriptors, create the program, bind the
// hidden coverage buffer, dispatch, and verify counters were written.
static void testCoverageMetadata(
    GpuTestContext* ctx,
    uint32_t requestedCounterByteWidth,
    const char* declarations = "RWStructuredBuffer<uint> outBuffer;",
    const char* outputPath = "outBuffer",
    int32_t coverageSpace = 3,
    bool entryPointParameter = false,
    const char* inputPath = nullptr,
    uint32_t expectedChildSet = 0
)
{
    static constexpr uint32_t kCoverageBinding = 11;
    std::string shaderSource = declarations;
    shaderSource += R"(

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)";
    if (entryPointParameter)
        shaderSource += ", uniform ParameterBlock<Params> params";
    shaderSource += R"()
{
    uint accum = tid.x;
    for (uint i = 0; i < 4; ++i)
    {
        if ((i & 1u) == 0u)
            accum += i;
        else
            accum += i * 2u;
    }
    )";
    shaderSource += outputPath;
    shaderSource += "[0] = accum";
    if (inputPath)
        shaderSource += std::string(" + ") + inputPath;
    shaderSource += ";\n}\n";

    DeviceExtraOptions extraOptions = {};
    if (requestedCounterByteWidth == 4)
    {
        slang::CompilerOptionEntry width = {};
        width.name = slang::CompilerOptionName::TraceCoverageCounterByteWidth;
        width.value.kind = slang::CompilerOptionValueKind::Int;
        width.value.intValue0 = 4;
        extraOptions.compilerOptions.push_back(width);
    }
    // The 64-bit case intentionally uses the compiler default.

    slang::CompilerOptionEntry traceCoverageOption = {};
    traceCoverageOption.name = slang::CompilerOptionName::TraceCoverage;
    traceCoverageOption.value.kind = slang::CompilerOptionValueKind::Int;
    traceCoverageOption.value.intValue0 = 1;
    extraOptions.compilerOptions.push_back(traceCoverageOption);

    slang::CompilerOptionEntry traceCoverageBindingOption = {};
    traceCoverageBindingOption.name = slang::CompilerOptionName::TraceCoverageBinding;
    traceCoverageBindingOption.value.kind = slang::CompilerOptionValueKind::Int;
    traceCoverageBindingOption.value.intValue0 = kCoverageBinding;
    traceCoverageBindingOption.value.intValue1 = coverageSpace;
    if (coverageSpace >= 0)
        extraOptions.compilerOptions.push_back(traceCoverageBindingOption);

    auto localDevice = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);

    if (requestedCounterByteWidth == 8 && !localDevice->hasFeature(Feature::AtomicInt64))
        SKIP("64-bit buffer atomics are not supported");

    std::vector<SyntheticResourceBindingDesc> syntheticResources;
    std::vector<std::string> syntheticResourceDebugNames;
    uint32_t coverageCounterCount = 0;
    uint32_t counterByteWidth = 0;
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(createComputeProgramFromCoverageMetadata(
        localDevice,
        shaderSource,
        shaderProgram.writeRef(),
        &syntheticResources,
        &syntheticResourceDebugNames,
        &coverageCounterCount,
        &counterByteWidth
    ));

    REQUIRE_EQ(syntheticResources.size(), 1u);
    REQUIRE_GT(coverageCounterCount, 0u);
    REQUIRE_EQ(counterByteWidth, requestedCounterByteWidth);
    CHECK_EQ(syntheticResources[0].bindingType, slang::BindingType::MutableRawBuffer);
    CHECK_EQ(syntheticResources[0].scope, SyntheticResourceScope::Global);
    CHECK_EQ(syntheticResources[0].access, SyntheticResourceAccess::ReadWrite);
    if (ctx->deviceType == DeviceType::Vulkan && coverageSpace >= 0)
    {
        CHECK_EQ(syntheticResources[0].binding, int32_t(kCoverageBinding));
        CHECK_EQ(syntheticResources[0].space, coverageSpace);
    }
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

#if SLANG_RHI_ENABLE_VULKAN
    if (inputPath && ctx->deviceType == DeviceType::Vulkan)
    {
        // Check placement before creating a pipeline: a wrong set would make
        // the pipeline invalid, so it must never reach dispatch in this test.
        auto* layout = static_cast<vk::ShaderProgramImpl*>(shaderProgram.get())->m_rootShaderObjectLayout.get();
        REQUIRE(layout->m_descriptorSetComposition);
        const auto& placement = entryPointParameter ? layout->m_descriptorSetComposition->entryPoints[0]
                                                    : layout->m_descriptorSetComposition->root;
        REQUIRE_GT(placement.subObjects.size(), 0u);
        REQUIRE_EQ(placement.subObjects[0].firstSet, expectedChildSet);
    }
#endif

    ComPtr<IComputePipeline> pipeline;
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    REQUIRE_CALL(localDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(localDevice->createRootShaderObject(shaderProgram.get(), rootObject.writeRef()));
    const uint32_t inputValue = 7;
    if (inputPath)
    {
        REQUIRE_CALL(ShaderCursor(rootObject).getPath(inputPath).setData(&inputValue, sizeof(inputValue)));
    }

    const uint32_t initialOutput = 0;
    auto outputBuffer = createTestBuffer(localDevice, sizeof(uint32_t), &initialOutput);

    const uint64_t initialCounter = counterByteWidth == 8 ? uint64_t(UINT32_MAX) : 0;
    std::vector<uint8_t> initialCoverage(size_t(coverageCounterCount) * counterByteWidth);
    for (uint32_t i = 0; i < coverageCounterCount; ++i)
        std::memcpy(initialCoverage.data() + size_t(i) * counterByteWidth, &initialCounter, counterByteWidth);
    auto coverageBuffer =
        createTestBuffer(localDevice, initialCoverage.size(), initialCoverage.data(), counterByteWidth);

    ShaderCursor outputCursor(rootObject);
    REQUIRE_CALL(outputCursor.getPath(outputPath).setBinding(outputBuffer));
    REQUIRE_CALL(
        bindSyntheticResource(shaderProgram.get(), rootObject.get(), syntheticResources[0].id, Binding(coverageBuffer))
    );

    auto queue = localDevice->getQueue(QueueType::Graphics);
    std::vector<uint64_t> firstDeltas(coverageCounterCount);
    for (uint32_t dispatch = 0; dispatch < 2; ++dispatch)
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        compareComputeResult(localDevice, outputBuffer, std::array<uint32_t, 1>{10u + (inputPath ? inputValue : 0u)});

        ComPtr<ISlangBlob> coverageBlob;
        REQUIRE_CALL(localDevice->readBuffer(coverageBuffer, 0, initialCoverage.size(), coverageBlob.writeRef()));
        uint64_t totalHits = 0;
        for (uint32_t i = 0; i < coverageCounterCount; ++i)
        {
            uint64_t value = 0;
            std::memcpy(
                &value,
                (const uint8_t*)coverageBlob->getBufferPointer() + size_t(i) * counterByteWidth,
                counterByteWidth
            );
            REQUIRE_GE(value, initialCounter);
            if (dispatch == 0)
                firstDeltas[i] = value - initialCounter;
            else
                CHECK_EQ(value, initialCounter + 2 * firstDeltas[i]);
            totalHits += value - initialCounter;
        }
        // With 64-bit counters seeded at UINT32_MAX, a positive delta also
        // proves that incrementing crossed the 32-bit boundary without wrapping.
        CHECK_GT(totalHits, 0u);
    }

    coverageBuffer.setNull();
    outputBuffer.setNull();
    rootObject.setNull();
    pipeline.setNull();
    syntheticProgram.setNull();
    shaderProgram.setNull();
    localDevice.setNull();
}

// Explicit 32-bit mode supports MoltenVK; the default 64-bit mode must preserve
// increments past UINT32_MAX and both modes must accumulate across dispatches.
GPU_TEST_CASE("synthetic-resource-bindings-from-slang-metadata", Vulkan | CUDA | DontCreateDevice)
{
    testCoverageMetadata(ctx, 4);
}

GPU_TEST_CASE("synthetic-resource-bindings-from-slang-metadata-64", Vulkan | CUDA | DontCreateDevice)
{
    testCoverageMetadata(ctx, 8);
}

// Exercise compiler-assigned and explicit coverage sets alongside a child set,
// including sharing that set at a distinct binding and nested ParameterBlocks.
GPU_TEST_CASE("synthetic-resource-bindings-parameter-block", Vulkan | DontCreateDevice)
{
    for (int32_t space : {-1, 0, 3})
    {
        CAPTURE(space);
        testCoverageMetadata(
            ctx,
            4,
            "struct Params { RWStructuredBuffer<uint> outBuffer; }; ParameterBlock<Params> params;",
            "params.outBuffer",
            space
        );
    }
}

// A reflected root binding at set 3 must not move the compiler-assigned child
// from set 0 to set 4. Use real coverage metadata sharing the root's set.
GPU_TEST_CASE("synthetic-resource-bindings-sparse-root-child", Vulkan | DontCreateDevice)
{
    std::string declarations;
    const char* inputPath = "params.value";
    uint32_t expectedChildSet = 0;
    bool entryPoint = false;
    SUBCASE("child before reflected root set")
    {
        declarations = "struct Params { uint value; }; ParameterBlock<Params> params; ";
    }
    SUBCASE("child between reflected root sets")
    {
        // The global uniform buffer occupies set 0, the child set 1, and
        // outBuffer set 3. Root vector size is not the child's set number.
        declarations = "uniform uint globalPadding; struct Params { uint value; }; ParameterBlock<Params> params; ";
        expectedChildSet = 1;
    }
    SUBCASE("nested children before reflected root set")
    {
        declarations =
            "struct Inner { uint value; }; "
            "struct Params { uint padding; ParameterBlock<Inner> inner; }; ParameterBlock<Params> params; ";
        inputPath = "params.inner.value";
    }
    SUBCASE("entry-point child before reflected root set")
    {
        declarations = "struct Params { uint value; }; ";
        entryPoint = true;
    }
    declarations += "[[vk::binding(0, 3)]] RWStructuredBuffer<uint> outBuffer;";
    testCoverageMetadata(ctx, 4, declarations.c_str(), "outBuffer", 3, entryPoint, inputPath, expectedChildSet);
}

GPU_TEST_CASE("synthetic-resource-bindings-nested-parameter-block", Vulkan | DontCreateDevice)
{
    testCoverageMetadata(
        ctx,
        4,
        "struct Inner { RWStructuredBuffer<uint> outBuffer; }; "
        "struct Outer { uint padding; ParameterBlock<Inner> inner; }; ParameterBlock<Outer> params; uint "
        "globalPadding;",
        "params.inner.outBuffer"
    );
}

// Entry-point descriptor ranges must be collected before coverage is inserted,
// just like global ParameterBlocks, and use the same runtime allocation order.
GPU_TEST_CASE("synthetic-resource-bindings-entry-point-parameter-block", Vulkan | DontCreateDevice)
{
    testCoverageMetadata(ctx, 4, "struct Params { RWStructuredBuffer<uint> outBuffer; };", "params.outBuffer", 3, true);
}

// A synthetic binding cannot overwrite an ordinary binding in a child set.
GPU_TEST_CASE("synthetic-resource-bindings-child-binding-collision", Vulkan)
{
    SyntheticLayoutDebugCallback callback;
    DeviceExtraOptions extraOptions;
    extraOptions.debugCallback = &callback;
    ComPtr<IDevice> testDevice = createTestingDevice(ctx, device->getDeviceType(), false, &extraOptions);
    REQUIRE(testDevice);
    const char* source = R"(
struct Params { RWStructuredBuffer<uint> outBuffer; };
ParameterBlock<Params> params;
[shader("compute")][numthreads(1, 1, 1)]
void computeMain() { params.outBuffer[0] = 10; }
)";
    SyntheticResourceBindingDesc desc = {};
    desc.id = 17;
    desc.bindingType = slang::BindingType::MutableRawBuffer;
    desc.space = 0;
    desc.binding = 0;
    desc.access = SyntheticResourceAccess::ReadWrite;
    ComPtr<IShaderProgram> program;
    CHECK_EQ(
        createComputeProgramWithSyntheticResource(testDevice, source, desc, program.writeRef()),
        SLANG_E_INVALID_ARG
    );
    CHECK(program == nullptr);
    CHECK(callback.errors.find("Duplicate Vulkan descriptor binding") != std::string::npos);
}
