// debug-shader-object.h
#pragma once
#include "debug-base.h"

#include <vector>
#include <string>
#include <map>
#include <unordered_map>

namespace gfx
{
using namespace Slang;

namespace debug
{

struct ShaderOffsetKey
{
    ShaderOffset offset;
    bool operator==(ShaderOffsetKey other) const
    {
        return offset.bindingArrayIndex == other.offset.bindingArrayIndex &&
            offset.bindingRangeIndex == other.offset.bindingRangeIndex &&
            offset.uniformOffset == other.offset.uniformOffset;
    }
    Slang::HashCode getHashCode() const
    {
        return Slang::combineHash(
            (Slang::HashCode)offset.uniformOffset,
            Slang::combineHash(
                (Slang::HashCode)offset.bindingArrayIndex,
                (Slang::HashCode)offset.bindingRangeIndex));
    }
};

class DebugShaderObject : public DebugObject<IShaderObject>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    void checkCompleteness();

public:
    IShaderObject* getInterface(const Slang::Guid& guid);
    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() override;
    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() override;
    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        getEntryPoint(GfxIndex index, IShaderObject** entryPoint) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        setData(ShaderOffset const& offset, void const* data, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        getObject(ShaderOffset const& offset, IShaderObject** object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        setObject(ShaderOffset const& offset, IShaderObject* object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        setResource(ShaderOffset const& offset, IResourceView* resourceView) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        setSampler(ShaderOffset const& offset, ISamplerState* sampler) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setCombinedTextureSampler(
        ShaderOffset const& offset,
        IResourceView* textureView,
        ISamplerState* sampler) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setSpecializationArgs(
        ShaderOffset const& offset,
        const slang::SpecializationArg* args,
        GfxCount count) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentVersion(
        ITransientResourceHeap* transientHeap, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;
    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
        setConstantBufferOverride(IBufferResource* constantBuffer) override;

public:
    // Type name of an ordinary shader object.
    std::string m_typeName;

    // The slang Type of an ordinary shader object. This is null for root objects.
    slang::TypeReflection* m_slangType = nullptr;

    // The slang program from which a root shader object is created, this is null for ordinary
    // objects.
    Slang::ComPtr<slang::IComponentType> m_rootComponentType;

    DebugDevice* m_device;

    std::vector<Slang::RefPtr<DebugShaderObject>> m_entryPoints;
    struct ShaderOffsetKeyHasher
    {
        Slang::HashCode operator()(const ShaderOffsetKey& key) const { return key.getHashCode(); }
    };
    std::unordered_map<ShaderOffsetKey, Slang::RefPtr<DebugShaderObject>, ShaderOffsetKeyHasher> m_objects;
    std::unordered_map<ShaderOffsetKey, Slang::RefPtr<DebugResourceView>, ShaderOffsetKeyHasher> m_resources;
    std::unordered_map<ShaderOffsetKey, Slang::RefPtr<DebugSamplerState>, ShaderOffsetKeyHasher> m_samplers;
    Slang::HashSet<SlangInt> m_initializedBindingRanges;
};

class DebugRootShaderObject : public DebugShaderObject
{
public:
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }
    virtual SLANG_NO_THROW Result SLANG_MCALL setSpecializationArgs(
        ShaderOffset const& offset,
        const slang::SpecializationArg* args,
        GfxCount count) override;
    void reset();
};

} // namespace debug
} // namespace gfx
