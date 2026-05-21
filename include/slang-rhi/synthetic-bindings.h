#pragma once

#include <slang-rhi.h>

namespace rhi {

enum class SyntheticResourceScope
{
    /// The synthetic resource is bound on the program's root shader object.
    Global,

    /// The synthetic resource is bound on a specific entry-point shader object.
    EntryPoint,
};

enum class SyntheticResourceAccess
{
    /// The shader only reads from the synthetic resource.
    Read,

    /// The shader only writes to the synthetic resource.
    Write,

    /// The shader both reads and writes the synthetic resource.
    ReadWrite,
};

/// Declares one synthetic resource that should be merged into a shader program's internal binding
/// layout even though the resource does not appear in normal Slang reflection.
struct SyntheticResourceBindingDesc
{
    /// Stable synthetic resource identifier.
    ///
    /// This value must be non-zero and unique within the containing
    /// `ShaderProgramSyntheticResourcesDesc`.
    uint32_t id = 0;

    /// Slang binding type used to create descriptor or marshaling state for this resource.
    slang::BindingType bindingType = slang::BindingType::Unknown;

    /// Number of array elements in the synthetic resource binding range.
    uint32_t arraySize = 1;

    /// Whether the resource is bound on the root shader object or an entry-point object.
    SyntheticResourceScope scope = SyntheticResourceScope::Global;

    /// Access mode for the synthetic resource in generated shader code.
    SyntheticResourceAccess access = SyntheticResourceAccess::Read;

    /// Entry-point index for `scope == EntryPoint`, otherwise `-1`.
    int32_t entryPointIndex = -1;

    /// Descriptor-space index for descriptor-backed backends, or `-1` if not applicable.
    int32_t space = -1;

    /// Descriptor binding index for descriptor-backed backends, or `-1` if not applicable.
    int32_t binding = -1;

    /// Byte offset in the root uniform data block for CPU/CUDA-style marshaling, or `-1`
    /// if not applicable.
    int32_t uniformOffset = -1;

    /// Byte stride between array elements for CPU/CUDA-style marshaling. This must be zero
    /// when `uniformOffset == -1`.
    int32_t uniformStride = 0;

    /// Optional debug label. The pointed-to string must remain valid for the duration of
    /// `createShaderProgram()`.
    const char* debugName = nullptr;
};

/// Optional `ShaderProgramDesc.next` extension that declares synthetic resources to merge into the
/// program's internal binding layout.
///
/// If omitted, no synthetic-resource layout work is performed and the resulting program does not
/// expose `ISyntheticShaderProgram`. Backends that do not support synthetic resources return
/// `SLANG_E_NOT_IMPLEMENTED` when this descriptor contains resources.
struct ShaderProgramSyntheticResourcesDesc
{
    StructType structType = StructType::ShaderProgramSyntheticResourcesDesc;
    const void* next = nullptr;

    const SyntheticResourceBindingDesc* resources = nullptr;
    uint32_t resourceCount = 0;
};

struct SyntheticBindingLocation
{
    size_t structSize = sizeof(SyntheticBindingLocation);

    /// Synthetic resource identifier that resolved to this binding location.
    uint32_t syntheticResourceID = 0;

    /// Resolved Slang binding type for the synthetic resource.
    slang::BindingType bindingType = slang::BindingType::Unknown;

    /// Number of array elements described by `offset`.
    uint32_t arraySize = 1;

    /// Whether the binding should be applied on the root shader object or a specific entry point.
    SyntheticResourceScope scope = SyntheticResourceScope::Global;

    /// Entry-point index for `scope == EntryPoint`, otherwise `-1`.
    int32_t entryPointIndex = -1;

    /// Resolved `ShaderOffset` that should be passed to `IShaderObject::setBinding()`.
    ShaderOffset offset = {};

    /// Optional debug label owned by the shader program. The pointer remains valid for the
    /// lifetime of the program object.
    const char* debugName = nullptr;
};

/// Optional program interface that exposes resolved `ShaderOffset`s for synthetic resources.
/// Only programs created with supported synthetic-resource descriptors expose this interface.
class ISyntheticShaderProgram : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x4a9f28a3, 0x286e, 0x4b07, {0x9a, 0xfa, 0x56, 0x62, 0xf7, 0xaa, 0x28, 0x95});

public:
    /// Returns the number of resolved synthetic binding locations on the program.
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getSyntheticBindingCount() = 0;

    /// Returns the resolved synthetic binding location at `index`.
    ///
    /// Returns:
    /// - `SLANG_OK` on success
    /// - `SLANG_E_INVALID_ARG` if `outLocation` is null, too small, or `index` is out of range
    virtual SLANG_NO_THROW Result SLANG_MCALL getSyntheticBindingLocation(
        uint32_t index,
        SyntheticBindingLocation* outLocation
    ) = 0;

    /// Finds the resolved binding location for `syntheticResourceID`.
    ///
    /// Returns:
    /// - `SLANG_OK` on success
    /// - `SLANG_E_INVALID_ARG` if `outLocation` is null, too small, or the ID is unknown
    virtual SLANG_NO_THROW Result SLANG_MCALL findSyntheticBindingLocationByID(
        uint32_t syntheticResourceID,
        SyntheticBindingLocation* outLocation
    ) = 0;
};

/// Resolves `syntheticResourceID` through `ISyntheticShaderProgram` and binds `binding`
/// through the existing `IShaderObject::setBinding()` path.
///
/// Returns:
/// - `SLANG_OK` on success
/// - `SLANG_E_INVALID_ARG` for null objects, invalid entry-point binding data, or an unknown ID
/// - any error returned by `queryInterface()`, `getEntryPoint()`, or `setBinding()`
inline Result bindSyntheticResource(
    IShaderProgram* program,
    IShaderObject* rootObject,
    uint32_t syntheticResourceID,
    const Binding& binding
)
{
    if (!program || !rootObject)
        return SLANG_E_INVALID_ARG;

    ComPtr<ISyntheticShaderProgram> syntheticProgramPtr;
    SLANG_RETURN_ON_FAIL(
        program->queryInterface(ISyntheticShaderProgram::getTypeGuid(), (void**)syntheticProgramPtr.writeRef())
    );
    SyntheticBindingLocation location = {};
    location.structSize = sizeof(SyntheticBindingLocation);
    SLANG_RETURN_ON_FAIL(syntheticProgramPtr->findSyntheticBindingLocationByID(syntheticResourceID, &location));

    if (location.scope == SyntheticResourceScope::EntryPoint)
    {
        if (location.entryPointIndex < 0)
            return SLANG_E_INVALID_ARG;
        ComPtr<IShaderObject> entryPointObject;
        SLANG_RETURN_ON_FAIL(
            rootObject->getEntryPoint((uint32_t)location.entryPointIndex, entryPointObject.writeRef())
        );
        return entryPointObject->setBinding(location.offset, binding);
    }

    return rootObject->setBinding(location.offset, binding);
}

} // namespace rhi
