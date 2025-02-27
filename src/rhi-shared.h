#pragma once

#include <slang-rhi.h>

#include "slang-context.h"
#include "resource-desc-utils.h"
#include "command-list.h"

#include "core/common.h"
#include "core/short_vector.h"
#include "reference.h"

#include "rhi-shared-fwd.h"

#include "device.h"
#include "command-buffer.h"

#include "rhi-shared-fwd.h"

#include "device.h"

#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rhi {

class Device;
class CommandEncoder;
class CommandList;

/// Common header for Desc struct types.
struct DescStructHeader
{
    StructType type;
    DescStructHeader* next;
};


class Fence : public IFence, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IFence* getInterface(const Guid& guid);

protected:
    NativeHandle sharedHandle = {};
};

class Resource : public ComObject
{};

class Buffer : public IBuffer, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Buffer(const BufferDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    BufferRange resolveBufferRange(const BufferRange& range);

    // IBuffer interface
    virtual SLANG_NO_THROW BufferDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    BufferDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle m_sharedHandle;
};

class Texture : public ITexture, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Texture(const TextureDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    SubresourceRange resolveSubresourceRange(const SubresourceRange& range);
    bool isEntireTexture(const SubresourceRange& range);

    // ITexture interface
    virtual SLANG_NO_THROW TextureDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    TextureDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle m_sharedHandle;
};

class TextureView : public ITextureView, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ITextureView* getInterface(const Guid& guid);

public:
    TextureView(const TextureViewDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // ITextureView interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    TextureViewDesc m_desc;
    StructHolder m_descHolder;
};

class Sampler : public ISampler, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISampler* getInterface(const Guid& guid);

public:
    Sampler(const SamplerDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // ISampler interface
    virtual SLANG_NO_THROW const SamplerDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    SamplerDesc m_desc;
    StructHolder m_descHolder;
};

class AccelerationStructure : public IAccelerationStructure, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IAccelerationStructure* getInterface(const Guid& guid);

public:
    AccelerationStructure(const AccelerationStructureDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // IAccelerationStructure interface
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;

public:
    AccelerationStructureDesc m_desc;
    StructHolder m_descHolder;
};

struct SpecializationKey
{
    short_vector<ShaderComponentID> componentIDs;

    SpecializationKey(const ExtendedShaderObjectTypeList& args)
        : componentIDs(args.componentIDs)
    {
    }

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

class ShaderProgram : public IShaderProgram, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderProgram* getInterface(const Guid& guid);

    ShaderProgramDesc m_desc;

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

    void init(const ShaderProgramDesc& desc);

    bool isSpecializable() const { return m_isSpecializable; }
    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode);

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

class InputLayout : public IInputLayout, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IInputLayout* getInterface(const Guid& guid);
};

class QueryPool : public IQueryPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IQueryPool* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override { return SLANG_OK; }

public:
    QueryPoolDesc m_desc;
};

enum class PipelineType
{
    Render,
    Compute,
    RayTracing,
};

class Pipeline : public ComObject
{
public:
    RefPtr<ShaderProgram> m_program;

    virtual PipelineType getType() const = 0;
    virtual bool isVirtual() const { return false; }
};

class RenderPipeline : public IRenderPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::Render; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRenderPipeline : public RenderPipeline
{
public:
    Device* m_device;
    RenderPipelineDesc m_desc;
    StructHolder m_descHolder;
    RefPtr<InputLayout> m_inputLayout;

    Result init(Device* device, const RenderPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRenderPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipeline : public IComputePipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::Compute; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualComputePipeline : public ComputePipeline
{
public:
    Device* m_device;
    ComputePipelineDesc m_desc;

    Result init(Device* device, const ComputePipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IComputePipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipeline : public IRayTracingPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::RayTracing; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRayTracingPipeline : public RayTracingPipeline
{
public:
    Device* m_device;
    RayTracingPipelineDesc m_desc;
    StructHolder m_descHolder;

    Result init(Device* device, const RayTracingPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRayTracingPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};



static const int kRayGenRecordSize = 64; // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

class ShaderTable : public IShaderTable, public ComObject
{
public:
    std::vector<std::string> m_shaderGroupNames;
    std::vector<ShaderRecordOverwrite> m_recordOverwrites;

    uint32_t m_rayGenShaderCount;
    uint32_t m_missShaderCount;
    uint32_t m_hitGroupCount;
    uint32_t m_callableShaderCount;

    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderTable* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderTable::getTypeGuid())
            return static_cast<IShaderTable*>(this);
        return nullptr;
    }

    Result init(const ShaderTableDesc& desc);
};

class Surface : public ISurface, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISurface* getInterface(const Guid& guid);

public:
    const SurfaceInfo& getInfo() override { return m_info; }
    const SurfaceConfig& getConfig() override { return m_config; }

public:
    void setInfo(const SurfaceInfo& info);
    void setConfig(const SurfaceConfig& config);

    SurfaceInfo m_info;
    StructHolder m_infoHolder;
    SurfaceConfig m_config;
    StructHolder m_configHolder;
};



bool isDepthFormat(Format format);
bool isStencilFormat(Format format);

bool isDebugLayersEnabled();

} // namespace rhi
