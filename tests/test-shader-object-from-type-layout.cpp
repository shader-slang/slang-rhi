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
    slang::IComponentType* globalScope = shaderProgram->getDesc().slangGlobalScope;
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
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(typeLayout, shaderObject.writeRef()));
    REQUIRE(shaderObject != nullptr);

    // Called twice so that the check below also rules out entries accumulating across
    // repeated calls, not just an insert on the first one.
    ComPtr<IShaderObject> secondShaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(typeLayout, secondShaderObject.writeRef()));
    REQUIRE(secondShaderObject != nullptr);

    // The device must not have retained the caller's type layout.
    CHECK_EQ(getShaderObjectLayoutCacheSize(device), cacheSizeBefore);
}

// Writing uniform data at an offset past the end of the shader object must fail instead of writing out of bounds.
GPU_TEST_CASE("shader-object-set-data-out-of-range", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-from-type-layout", "computeMain", shaderProgram.writeRef()));

    slang::IComponentType* globalScope = shaderProgram->getDesc().slangGlobalScope;
    REQUIRE(globalScope != nullptr);
    slang::ProgramLayout* programLayout = globalScope->getLayout(0);
    REQUIRE(programLayout != nullptr);
    slang::TypeLayoutReflection* typeLayout = programLayout->getParameterByIndex(0)->getTypeLayout();
    REQUIRE(typeLayout != nullptr);
    if (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
        typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock)
    {
        typeLayout = typeLayout->getElementTypeLayout();
    }
    REQUIRE(typeLayout != nullptr);

    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(typeLayout, shaderObject.writeRef()));

    float value = 1.f;

    ShaderOffset validOffset = {};
    CHECK_CALL(shaderObject->setData(validOffset, &value, sizeof(value)));

    ShaderOffset invalidOffset = {};
    invalidOffset.uniformOffset = 1024 * 1024;
    CHECK(shaderObject->setData(invalidOffset, &value, sizeof(value)) == SLANG_E_INVALID_ARG);
}
