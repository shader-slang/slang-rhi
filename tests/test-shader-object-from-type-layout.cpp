#include "testing.h"

#include "device.h"

using namespace rhi;
using namespace rhi::testing;

// Regression test for shader-slang/slang#10893.
//
// The rule being defended is the m_shaderObjectLayoutCache invariant documented in
// device.h: every cache key must be a session-owned layout. This test covers the one
// entry point that takes a layout from the caller, where that rule cannot be met,
// and so must not cache at all.
//
// It asserts the invariant rather than the symptom on purpose. The symptom is a
// use-after-free that only becomes visible when the allocator hands the freed address
// back out, so testing for it amounts to testing that a sanitizer happened to stay
// quiet - which is how this issue was previously written off as fixed. Checking that
// the device did not retain the pointer fails deterministically instead, on every
// platform, with or without a sanitizer.
GPU_TEST_CASE("shader-object-from-type-layout-not-cached", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-from-type-layout", "computeMain", shaderProgram.writeRef()));

    // Reach the layout through the program's own component, which is what makes
    // the resulting TypeLayoutReflection owned by the TargetProgram rather than
    // by the session.
    slang::IComponentType* globalScope = shaderProgram->getSlangComponentType();
    REQUIRE(globalScope != nullptr);
    slang::ProgramLayout* programLayout = globalScope->getLayout(0);
    REQUIRE(programLayout != nullptr);

    // Reach the `Params` element type layout the way a caller would: through the
    // program's own layout, not through the session.
    slang::VariableLayoutReflection* paramsVar = programLayout->getParameterByIndex(0);
    REQUIRE(paramsVar != nullptr);
    slang::TypeLayoutReflection* typeLayout = paramsVar->getTypeLayout();
    REQUIRE(typeLayout != nullptr);
    if (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
        typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock)
    {
        typeLayout = typeLayout->getElementTypeLayout();
    }
    REQUIRE(typeLayout != nullptr);

    const size_t cacheSizeBefore = getShaderObjectLayoutCacheSize(device);

    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(globalScope, typeLayout, shaderObject.writeRef()));
    REQUIRE(shaderObject != nullptr);

    // Called twice so that the check below also rules out entries accumulating across
    // repeated calls, not just an insert on the first one.
    ComPtr<IShaderObject> secondShaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(globalScope, typeLayout, secondShaderObject.writeRef()));
    REQUIRE(secondShaderObject != nullptr);

    // The device must not have retained the caller's type layout.
    CHECK_EQ(getShaderObjectLayoutCacheSize(device), cacheSizeBefore);
}

GPU_TEST_CASE("shader-object-from-type-layout-retains-owner", ALL)
{
    // Use a second device so the reflected layout comes from a sibling Slang session,
    // rather than the destination device's internal session.
    ComPtr<IDevice> sourceDevice = createTestingDevice(ctx, ctx->deviceType, false);
    REQUIRE(sourceDevice != nullptr);

    ComPtr<IShaderProgram> sourceProgram;
    REQUIRE_CALL(
        loadProgram(sourceDevice, "test-shader-object-from-type-layout", "computeMain", sourceProgram.writeRef())
    );

    slang::IComponentType* owner = sourceProgram->getSlangComponentType();
    REQUIRE(owner != nullptr);
    slang::ProgramLayout* programLayout = owner->getLayout(0);
    REQUIRE(programLayout != nullptr);
    slang::VariableLayoutReflection* paramsVar = programLayout->getParameterByIndex(0);
    REQUIRE(paramsVar != nullptr);
    slang::TypeLayoutReflection* typeLayout = paramsVar->getTypeLayout();
    REQUIRE(typeLayout != nullptr);
    if (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
        typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock)
    {
        typeLayout = typeLayout->getElementTypeLayout();
    }
    REQUIRE(typeLayout != nullptr);

    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(owner, typeLayout, shaderObject.writeRef()));
    REQUIRE(shaderObject != nullptr);

    // The shader object must keep the component and its TargetProgram-owned
    // reflection data alive independently of the source RHI objects.
    sourceProgram.setNull();
    sourceDevice.setNull();

    ShaderCursor cursor(shaderObject);
    REQUIRE_CALL(cursor["a"].setData(1.0f));
    REQUIRE_CALL(cursor["b"].setData(2.0f));
    CHECK_EQ(std::strcmp(shaderObject->getElementTypeLayout()->getType()->getName(), "Params"), 0);
}

GPU_TEST_CASE("shader-object-from-type-retains-owner", ALL)
{
    ComPtr<IDevice> sourceDevice = createTestingDevice(ctx, ctx->deviceType, false);
    REQUIRE(sourceDevice != nullptr);

    ComPtr<IShaderProgram> sourceProgram;
    REQUIRE_CALL(
        loadProgram(sourceDevice, "test-shader-object-from-type-layout", "computeMain", sourceProgram.writeRef())
    );

    slang::TypeReflection* type = sourceProgram->findTypeByName("Params");
    REQUIRE(type != nullptr);

    const size_t cacheSizeBefore = getShaderObjectLayoutCacheSize(device);
    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(
        device
            ->createShaderObjectFromType(sourceProgram, type, ShaderObjectContainerType::None, shaderObject.writeRef())
    );
    REQUIRE(shaderObject != nullptr);
    CHECK_EQ(getShaderObjectLayoutCacheSize(device), cacheSizeBefore + 1);

    // A fresh source session guarantees the first call inserts a layout. The
    // second object must reuse it instead of rebuilding backend layouts.
    ComPtr<IShaderObject> secondShaderObject;
    REQUIRE_CALL(device->createShaderObjectFromType(
        sourceProgram,
        type,
        ShaderObjectContainerType::None,
        secondShaderObject.writeRef()
    ));
    REQUIRE(secondShaderObject != nullptr);
    CHECK_EQ(getShaderObjectLayoutCacheSize(device), cacheSizeBefore + 1);
    secondShaderObject.setNull();

    sourceProgram.setNull();
    sourceDevice.setNull();

    ShaderCursor cursor(shaderObject);
    REQUIRE_CALL(cursor["a"].setData(1.0f));
    REQUIRE_CALL(cursor["b"].setData(2.0f));
    CHECK_EQ(std::strcmp(shaderObject->getElementTypeLayout()->getType()->getName(), "Params"), 0);
}

GPU_TEST_CASE("root-shader-object-child-retains-owner", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-from-type-layout", "computeMain", shaderProgram.writeRef()));

    ComPtr<IShaderObject> rootObject = device->createRootShaderObject(shaderProgram);
    REQUIRE(rootObject != nullptr);

    ShaderCursor paramsCursor = ShaderCursor(rootObject)["gParams"];
    REQUIRE(paramsCursor.isValid());
    ComPtr<IShaderObject> paramsObject;
    REQUIRE_CALL(rootObject->getObject(paramsCursor.m_offset, paramsObject.writeRef()));
    REQUIRE(paramsObject != nullptr);

    rootObject.setNull();
    shaderProgram.setNull();

    ShaderCursor cursor(paramsObject);
    REQUIRE_CALL(cursor["a"].setData(1.0f));
    REQUIRE_CALL(cursor["b"].setData(2.0f));
    CHECK_EQ(std::strcmp(paramsObject->getElementTypeLayout()->getType()->getName(), "Params"), 0);
}

GPU_TEST_CASE("shader-object-session-witness-and-child-lifetime", CPU)
{
    // Prime two fresh sessions in opposite orders, so using the destination
    // device's witness ID would deterministically write the wrong header.
    ComPtr<IDevice> destination = createTestingDevice(ctx, ctx->deviceType, false);
    ComPtr<IDevice> source = createTestingDevice(ctx, ctx->deviceType, false);
    ComPtr<IShaderObject> child;
    ComPtr<slang::IComponentType> externalOwner;
    uint32_t firstIDs[2] = {};
    IDevice* devices[] = {destination.get(), source.get()};
    for (uint32_t i = 0; i < 2; ++i)
    {
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(devices[i], "test-shader-object-session", "computeMain", program.writeRef()));
        auto* owner = program->getSlangComponentType();
        REQUIRE(owner);
        auto* iface = program->findTypeByName("IValue");
        auto* first = program->findTypeByName("First");
        auto* second = program->findTypeByName("Second");
        REQUIRE(iface);
        REQUIRE(first);
        REQUIRE(second);
        if (i == 1)
        {
            externalOwner = owner;
            uint32_t secondID;
            REQUIRE_CALL(owner->getSession()->getTypeConformanceWitnessSequentialID(second, iface, &secondID));
        }
        REQUIRE_CALL(owner->getSession()->getTypeConformanceWitnessSequentialID(first, iface, &firstIDs[i]));

        auto* paramsLayout = owner->getLayout()->getParameterByIndex(0)->getTypeLayout()->getElementTypeLayout();
        ComPtr<IShaderObject> params;
        REQUIRE_CALL(destination->createShaderObjectFromTypeLayout(owner, paramsLayout, params.writeRef()));
        ComPtr<IShaderObject> value;
        REQUIRE_CALL(
            destination->createShaderObjectFromType(owner, first, ShaderObjectContainerType::None, value.writeRef())
        );
        ShaderCursor field = ShaderCursor(params)["field"];
        REQUIRE(field.isValid());
        REQUIRE_CALL(field.setObject(value));
        uint32_t storedID;
        // The witness ID occupies the second word of the Slang existential header.
        size_t witnessOffset = field.m_offset.uniformOffset + 8;
        REQUIRE(witnessOffset + sizeof(storedID) <= params->getSize());
        std::memcpy(&storedID, static_cast<const uint8_t*>(params->getRawData()) + witnessOffset, sizeof(storedID));
        CHECK_EQ(storedID, firstIDs[i]);

        auto childField = ShaderCursor(params)["child"];
        REQUIRE(childField.isValid());
        REQUIRE_CALL(params->getObject(childField.m_offset, child.writeRef()));
    }
    REQUIRE_NE(firstIDs[0], firstIDs[1]);
    source.setNull();
    // Only the child from the external session survives the parent and program.
    REQUIRE(child);
    REQUIRE(externalOwner.detach()->release() > 0);
    auto valueField = ShaderCursor(child)["value"];
    REQUIRE(valueField.isValid());
    REQUIRE_CALL(valueField.setData(42));
}

GPU_TEST_CASE("shader-object-session-validation", CPU)
{
    struct Callback : IDebugCallback
    {
        std::string error;
        std::string warning;
        void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
        {
            if (source == DebugMessageSource::Layer && type == DebugMessageType::Error)
                error = message;
            if (source == DebugMessageSource::Layer && type == DebugMessageType::Warning)
                warning = message;
        }
    } callback;
    DeviceExtraOptions options = {};
    options.enableValidation = true;
    options.debugCallback = &callback;
    ComPtr<IDevice> validated = createTestingDevice(ctx, ctx->deviceType, false, &options);
    ComPtr<IShaderProgram> externalProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-from-type-layout", "computeMain", externalProgram.writeRef()));

    // Exercise both the core rejection and the validation-layer diagnostics.
    IDevice* devices[] = {getUnderlyingDevice(validated), validated.get()};
    for (auto* target : devices)
    {
        bool expectDiagnostic = target == validated.get();
        CAPTURE(expectDiagnostic);
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(target, "test-shader-object-from-type-layout", "computeMain", program.writeRef()));
        const auto& external = externalProgram->getDesc();
        REQUIRE_EQ(external.slangEntryPointCount, 1);
        ShaderProgramDesc entryPointOnlyDesc = program->getDesc();
        entryPointOnlyDesc.linkingStyle = LinkingStyle::SeparateEntryPointCompilation;
        entryPointOnlyDesc.slangGlobalScope = nullptr;
        auto entryPointOnly = target->createShaderProgram(entryPointOnlyDesc);
        REQUIRE(entryPointOnly);
        REQUIRE(entryPointOnly->getSlangComponentType() == nullptr);
        auto* type = program->findTypeByName("Params");
        REQUIRE(type);
        IShaderProgram* missingOwners[] = {nullptr, entryPointOnly.get()};
        for (auto* missingOwner : missingOwners)
        {
            ComPtr<IShaderObject> invalid;
            callback.error.clear();
            CHECK_EQ(
                target->createShaderObjectFromType(
                    missingOwner,
                    type,
                    ShaderObjectContainerType::None,
                    invalid.writeRef()
                ),
                SLANG_E_INVALID_ARG
            );
            CHECK_FALSE(invalid);
            if (expectDiagnostic)
                CHECK(callback.error.find("A Slang component is required") != std::string::npos);
        }

        ShaderProgramDesc mixed = program->getDesc();
        mixed.slangEntryPoints = external.slangEntryPoints;
        for (auto style : {LinkingStyle::SingleProgram, LinkingStyle::SeparateEntryPointCompilation})
        {
            mixed.linkingStyle = style;
            ComPtr<IShaderProgram> invalid;
            callback.error.clear();
            CHECK_EQ(target->createShaderProgram(mixed, invalid.writeRef()), SLANG_E_INVALID_ARG);
            if (expectDiagnostic)
                CHECK(
                    callback.error.find(
                        "slangEntryPoints[0]' belongs to a different Slang session than 'slangGlobalScope"
                    ) != std::string::npos
                );
        }

        auto root = target->createRootShaderObject(program);
        REQUIRE(root);
        auto binding = ShaderCursor(root)["gParams"];
        REQUIRE(binding.isValid());
        auto createParams = [&](IShaderProgram* source)
        {
            ComPtr<IShaderObject> object;
            REQUIRE_CALL(target->createShaderObjectFromType(
                source,
                source->findTypeByName("Params"),
                ShaderObjectContainerType::None,
                object.writeRef()
            ));
            return object;
        };
        auto original = createParams(program);
        auto foreign = createParams(externalProgram);
        REQUIRE_CALL(binding.setObject(original));
        callback.error.clear();
        CHECK_EQ(binding.setObject(foreign), SLANG_E_INVALID_ARG);
        if (expectDiagnostic)
            CHECK(callback.error.find("belongs to a different Slang session") != std::string::npos);
        ComPtr<IShaderObject> actual;
        REQUIRE_CALL(root->getObject(binding.m_offset, actual.writeRef()));
        CHECK_EQ(actual.get(), original.get());

        if (expectDiagnostic)
        {
            ComPtr<IShaderObject> legacy;
            REQUIRE_CALL(target->createShaderObject(
                nullptr,
                program->findTypeByName("Params"),
                ShaderObjectContainerType::None,
                legacy.writeRef()
            ));
            CHECK(callback.warning.find("originating Slang session") != std::string::npos);
        }
    }
}
