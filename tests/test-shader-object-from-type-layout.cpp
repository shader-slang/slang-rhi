#include "testing.h"

#include "device.h"

using namespace rhi;
using namespace rhi::testing;

// Regression test for shader-slang/slang#10893.
//
// `Device::m_shaderObjectLayoutCache` is keyed on a raw
// `slang::TypeLayoutReflection*` and lives as long as the `Device`. Every other
// way into that cache supplies a key obtained from `ISession::getTypeLayout`,
// which the `Linkage` owns, so the entry's `ComPtr<slang::ISession>` covers the
// key. `createShaderObjectFromTypeLayout` is different: the key comes from the
// caller, and in practice it is a layout owned by a `TargetProgram`. Caching it
// leaves an entry that outlives the layout as soon as the caller releases its
// program, and a later allocation landing on the recycled address turns the next
// lookup into a use-after-free.
//
// The failure that follows from that is a use-after-free whose visibility
// depends on the allocator handing the freed address back out, so testing for
// it directly means testing for "ASan happened to stay quiet" - which is exactly
// how these findings were previously written off as fixed. This asserts the
// invariant instead: slang-rhi must not retain a `TypeLayoutReflection*` it was
// handed. That fails deterministically, on every platform, sanitizer or not.
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

    // The device must not have taken a reference to the caller's pointer. A
    // second call is included because a cache would only be observable on the
    // insert, and this also pins the "no reuse across calls" behaviour.
    ComPtr<IShaderObject> secondShaderObject;
    REQUIRE_CALL(device->createShaderObjectFromTypeLayout(typeLayout, secondShaderObject.writeRef()));

    CHECK_EQ(getShaderObjectLayoutCacheSize(device), cacheSizeBefore);
}
