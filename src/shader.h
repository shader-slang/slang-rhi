#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "core/short_vector.h"

#include "rhi-shared-fwd.h"
#include "device-child.h"

#include <unordered_map>

namespace rhi {

struct SpecializationKey
{
    short_vector<ShaderComponentID> componentIDs;

    SpecializationKey(const ExtendedShaderObjectTypeList& args);

    bool operator==(const SpecializationKey& rhs) const { return componentIDs == rhs.componentIDs; }

    struct Hasher
    {
        std::size_t operator()(const SpecializationKey& key) const
        {
            size_t hash = 0;
            for (auto& arg : key.componentIDs)
                hash_combine(hash, arg);
            return hash;
        }
    };
};

class ShaderProgram : public IShaderProgram, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderProgram* getInterface(const Guid& guid);

    ShaderProgramDesc m_desc;
    StructHolder m_descHolder;

    ComPtr<slang::IComponentType> slangGlobalScope;
    std::vector<ComPtr<slang::IComponentType>> slangEntryPoints;

    // Linked program when linkingStyle is GraphicsCompute, or the original global scope
    // when linking style is RayTracing.
    ComPtr<slang::IComponentType> linkedProgram;

    // Linked program for each entry point when linkingStyle is RayTracing.
    std::vector<ComPtr<slang::IComponentType>> linkedEntryPoints;

    bool m_isSpecializable = false;

    bool m_compiledShaders = false;

    std::unordered_map<SpecializationKey, RefPtr<ShaderProgram>, SpecializationKey::Hasher> m_specializedPrograms;

    ShaderProgram(Device* device, const ShaderProgramDesc& desc)
        : DeviceChild(device)
        , m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
        m_descHolder.holdList(m_desc.slangEntryPoints, m_desc.slangEntryPointCount);
    }

    void init();

    bool isSpecializable() const { return m_isSpecializable; }
    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode);

    virtual SLANG_NO_THROW const ShaderProgramDesc& SLANG_MCALL getDesc() { return m_desc; }

    virtual SLANG_NO_THROW slang::TypeReflection* SLANG_MCALL findTypeByName(const char* name) override
    {
        return linkedProgram->getLayout()->findTypeByName(name);
    }

    virtual ShaderObjectLayout* getRootShaderObjectLayout() = 0;

private:
    bool _isSpecializable()
    {
        if (slangGlobalScope->getSpecializationParamCount() != 0)
        {
            return true;
        }
        for (auto& entryPoint : slangEntryPoints)
        {
            if (entryPoint->getSpecializationParamCount() != 0)
            {
                return true;
            }
        }
        return false;
    }
};

} // namespace rhi
