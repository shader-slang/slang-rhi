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
    Fence(Device* device, const FenceDesc& desc);

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
    Buffer(Device* device, const BufferDesc& desc);

    BufferRange resolveBufferRange(const BufferRange& range);

    // IBuffer interface
    virtual SLANG_NO_THROW BufferDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    ) override;

public:
    BufferDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle m_sharedHandle;
};

struct SubResourceLayout
{
    Extent3D size;
    Size strideY;
    Size strideZ;
};

// Shared helper to calculate the layout of a subresource region from
// a texture description given a minimum row alignment.
Result calcSubresourceRegionLayout(
    const TextureDesc& desc,
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    Size rowAlignment,
    SubresourceLayout* outLayout
);

class Texture : public ITexture, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Texture(Device* device, const TextureDesc& desc);

    SubresourceRange resolveSubresourceRange(const SubresourceRange& range);
    bool isEntireTexture(const SubresourceRange& range);

    // Get layout the target requires for a given region within a given sub resource
    // of this texture. Supply offset==0 and extent==kRemainingTextureSize to indicate whole sub resource.
    // If rowAlignment is kDefaultAlignment, implementation uses Device::getTextureRowAlignment for alignment.
    virtual Result getSubresourceRegionLayout(
        uint32_t mip,
        Offset3D offset,
        Extent3D extent,
        size_t rowAlignment,
        SubresourceLayout* outLayout
    );

    // ITexture interface
    virtual SLANG_NO_THROW TextureDesc& SLANG_MCALL getDesc() override { return m_desc; };
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createView(
        const TextureViewDesc& desc,
        ITextureView** outTextureView
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSubresourceLayout(
        uint32_t mip,
        size_t rowAlignment,
        SubresourceLayout* outLayout
    ) override
    {
        return getSubresourceRegionLayout(mip, {0, 0, 0}, Extent3D::kWholeTexture, rowAlignment, outLayout);
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
    TextureView(Device* device, const TextureViewDesc& desc);

    // ITextureView interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW const TextureViewDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result getDescriptorHandle(
        DescriptorHandleAccess access,
        DescriptorHandle* outHandle
    ) override;

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
    Sampler(Device* device, const SamplerDesc& desc);

    // ISampler interface
    virtual SLANG_NO_THROW const SamplerDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

    // IResource interface
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
    AccelerationStructure(Device* device, const AccelerationStructureDesc& desc);

    // IAccelerationStructure interface
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

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
    QueryPool(Device* device, const QueryPoolDesc& desc);

    virtual SLANG_NO_THROW const QueryPoolDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override { return SLANG_OK; }

public:
    QueryPoolDesc m_desc;
    StructHolder m_descHolder;
};

static const int kRayGenRecordSize = 64; // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

class ShaderTable : public IShaderTable, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderTable* getInterface(const Guid& guid);

public:
    ShaderTable(Device* device, const ShaderTableDesc& desc);

public:
    std::vector<std::string> m_shaderGroupNames;
    std::vector<ShaderRecordOverwrite> m_recordOverwrites;

    uint32_t m_rayGenShaderCount;
    uint32_t m_missShaderCount;
    uint32_t m_hitGroupCount;
    uint32_t m_callableShaderCount;
};

class Surface : public ISurface, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISurface* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW const SurfaceInfo& SLANG_MCALL getInfo() override { return m_info; }
    virtual SLANG_NO_THROW const SurfaceConfig* SLANG_MCALL getConfig() override
    {
        return m_configured ? &m_config : nullptr;
    }

public:
    void setInfo(const SurfaceInfo& info);
    void setConfig(const SurfaceConfig& config);

    SurfaceInfo m_info;
    StructHolder m_infoHolder;
    SurfaceConfig m_config;
    StructHolder m_configHolder;
    bool m_configured = false;
};

struct DeviceAdapter
{
    Device* device;
    DeviceAdapter(Device* device)
        : device(device)
    {
    }
    DeviceAdapter(DeviceChild* deviceChild)
        : device(deviceChild && deviceChild->getDevice() ? deviceChild->getDevice() : nullptr)
    {
    }
    explicit operator bool() const { return device != nullptr; }
    Device* operator->() const { return device; }
};

bool isDepthFormat(Format format);
bool isStencilFormat(Format format);

inline uint32_t widthInBlocks(const FormatInfo& formatInfo, uint32_t size)
{
    return formatInfo.isCompressed ? (size + formatInfo.blockWidth - 1) / formatInfo.blockWidth : size;
}

inline uint32_t heightInBlocks(const FormatInfo& formatInfo, uint32_t size)
{
    return formatInfo.isCompressed ? (size + formatInfo.blockHeight - 1) / formatInfo.blockHeight : size;
}

bool isDebugLayersEnabled();

} // namespace rhi
