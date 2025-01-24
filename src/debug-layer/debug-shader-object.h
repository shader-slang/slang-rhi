#pragma once

#include "debug-base.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhi::debug {

struct ShaderOffsetKey
{
    ShaderOffset offset;
    bool operator==(ShaderOffsetKey other) const
    {
        return offset.bindingArrayIndex == other.offset.bindingArrayIndex &&
               offset.bindingRangeIndex == other.offset.bindingRangeIndex &&
               offset.uniformOffset == other.offset.uniformOffset;
    }

    size_t getHashCode() const
    {
        size_t hash = 0;
        hash_combine(hash, offset.uniformOffset);
        hash_combine(hash, offset.bindingArrayIndex);
        hash_combine(hash, offset.bindingRangeIndex);
        return hash;
    }
};

class DebugShaderObject : public DebugObject<IShaderObject>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugShaderObject);

    void checkCompleteness();
    void checkFinalized();
    void checkNotFinalized();

public:
    IShaderObject* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() override;
    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() override;
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(uint32_t index, IShaderObject** entryPoint) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(const ShaderOffset& offset, const void* data, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getObject(const ShaderOffset& offset, IShaderObject** object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(const ShaderOffset& offset, IShaderObject* object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset& offset, Binding binding) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSpecializationArgs(const ShaderOffset& offset, const slang::SpecializationArg* args, uint32_t count) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;
    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setConstantBufferOverride(IBuffer* constantBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finalize() override;
    virtual SLANG_NO_THROW bool SLANG_MCALL isFinalized() override;

public:
    // Type name of an ordinary shader object.
    std::string m_typeName;

    // The slang Type of an ordinary shader object. This is null for root objects.
    slang::TypeReflection* m_slangType = nullptr;

    // The slang program from which a root shader object is created, this is null for ordinary
    // objects.
    ComPtr<slang::IComponentType> m_rootComponentType;

    DebugDevice* m_device;

    std::vector<RefPtr<DebugShaderObject>> m_entryPoints;
    struct ShaderOffsetKeyHasher
    {
        size_t operator()(const ShaderOffsetKey& key) const { return key.getHashCode(); }
    };
    std::unordered_map<ShaderOffsetKey, RefPtr<DebugShaderObject>, ShaderOffsetKeyHasher> m_objects;
    std::unordered_map<ShaderOffsetKey, Binding, ShaderOffsetKeyHasher> m_bindings;
    std::set<SlangInt> m_initializedBindingRanges;
};

class DebugRootShaderObject : public DebugShaderObject
{
public:
    DebugRootShaderObject(DebugContext* ctx)
        : DebugShaderObject(ctx)
    {
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSpecializationArgs(const ShaderOffset& offset, const slang::SpecializationArg* args, uint32_t count) override;
    void reset();
};

} // namespace rhi::debug
