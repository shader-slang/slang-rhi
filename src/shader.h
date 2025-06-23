#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "core/short_vector.h"
#include "core/timer.h"

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

using ShaderProgramID = uint64_t;

class ShaderProgram : public IShaderProgram, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderProgram* getInterface(const Guid& guid);

    ShaderProgramDesc m_desc;
    StructHolder m_descHolder;

    ShaderProgramID m_id;

    ComPtr<slang::IComponentType> slangGlobalScope;
    std::vector<ComPtr<slang::IComponentType>> slangEntryPoints;

    // Linked program when linkingStyle is SingleProgram, or the original global scope
    // when linking style is SeparateEntryPointCompilation.
    ComPtr<slang::IComponentType> linkedProgram;

    // Linked program for each entry point when linkingStyle is SeparateEntryPointCompilation.
    std::vector<ComPtr<slang::IComponentType>> linkedEntryPoints;

    bool m_isSpecializable = false;

    bool m_compiledShaders = false;

    std::unordered_map<SpecializationKey, RefPtr<ShaderProgram>, SpecializationKey::Hasher> m_specializedPrograms;

    ShaderProgram(Device* device, const ShaderProgramDesc& desc);

    Result init();

    bool isSpecializable() const { return m_isSpecializable; }
    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode);

    virtual SLANG_NO_THROW const ShaderProgramDesc& SLANG_MCALL getDesc() override { return m_desc; }

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

class ShaderCompilationReporter : public RefObject
{
public:
    enum class PipelineType
    {
        Render,
        Compute,
        RayTracing
    };

    ShaderCompilationReporter(Device* device);

    void registerProgram(ShaderProgram* program);

    void reportCompileEntryPoint(
        ShaderProgram* program,
        const char* entryPointName,
        TimePoint startTime,
        TimePoint endTime,
        double totalTime,
        double downstreamTime,
        bool isCached,
        size_t cacheSize
    );

    void reportCreatePipeline(
        ShaderProgram* program,
        PipelineType pipelineType,
        TimePoint startTime,
        TimePoint endTime,
        bool isCached,
        size_t cacheSize
    );

private:
    Device* m_device;
};

} // namespace rhi
