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
#include "shader-object.h"
#include "shader.h"
#include "pipeline.h"

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

class Fence : public IFence, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IFence* getInterface(const Guid& guid);

public:
    Fence(Device* device, const FenceDesc& desc)
        : DeviceChild(device)
        , m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

protected:
    FenceDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle sharedHandle = {};
};

class Resource : public DeviceChild
{
public:
    Resource(Device* device)
        : DeviceChild(device)
    {
    }
};

class Buffer : public IBuffer, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Buffer(Device* device, const BufferDesc& desc)
        : Resource(device)
        , m_desc(desc)
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

struct SubResourceLayout
{
    Extents size;
    Size strideY;
    Size strideZ;
};

class Texture : public ITexture, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Texture(Device* device, const TextureDesc& desc)
        : Resource(device)
        , m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    SubresourceRange resolveSubresourceRange(const SubresourceRange& range);
    bool isEntireTexture(const SubresourceRange& range);

    // Get layout the target requires for a given region within a given sub resource
    // of this texture. Supply offset==0 and extents==kRemainingTextureSize to indicate whole sub resource.
    virtual Result getSubresourceRegionLayout(
        uint32_t mipLevel,
        uint32_t layerIndex,
        Offset3D offset,
        Extents extents,
        SubresourceLayout* outLayout
    )
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }

    // ITexture interface
    virtual SLANG_NO_THROW TextureDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getSubresourceLayout(uint32_t mipLevel, uint32_t layerIndex, SubresourceLayout* outLayout) override
    {
        return getSubresourceRegionLayout(mipLevel, layerIndex, Offset3D(), Extents(), outLayout);
    }

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
    TextureView(Device* device, const TextureViewDesc& desc)
        : Resource(device)
        , m_desc(desc)
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
    Sampler(Device* device, const SamplerDesc& desc)
        : Resource(device)
        , m_desc(desc)
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
    AccelerationStructure(Device* device, const AccelerationStructureDesc& desc)
        : Resource(device)
        , m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // IAccelerationStructure interface
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;

public:
    AccelerationStructureDesc m_desc;
    StructHolder m_descHolder;
};

class InputLayout : public IInputLayout, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IInputLayout* getInterface(const Guid& guid);
};

class QueryPool : public IQueryPool, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IQueryPool* getInterface(const Guid& guid);

public:
    QueryPool(Device* device, const QueryPoolDesc& desc)
        : DeviceChild(device)
        , m_desc(desc)
    {
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override { return SLANG_OK; }

public:
    QueryPoolDesc m_desc;
};

static const int kRayGenRecordSize = 64; // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

class ShaderTable : public IShaderTable, public DeviceChild
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

    ShaderTable(Device* device, const ShaderTableDesc& desc);
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
