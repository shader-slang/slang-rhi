#include "rhi-shared.h"
#include "device-child.h"
#include "command-list.h"

#include "core/common.h"

#include <slang.h>

#include <atomic>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

// ----------------------------------------------------------------------------
// Extent3D
// ----------------------------------------------------------------------------

Extent3D Extent3D::kWholeTexture = {kRemainingTextureSize, kRemainingTextureSize, kRemainingTextureSize};

// ----------------------------------------------------------------------------
// Fence
// ----------------------------------------------------------------------------

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
}

Fence::Fence(Device* device, const FenceDesc& desc)
    : DeviceChild(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}


// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------

IResource* Buffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == IBuffer::getTypeGuid())
        return static_cast<IBuffer*>(this);
    return nullptr;
}

Buffer::Buffer(Device* device, const BufferDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = min(resolved.offset, m_desc.size);
    resolved.size = min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

Result Buffer::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getDescriptorHandle(
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// Texture helpers
// ----------------------------------------------------------------------------

Result calcSubresourceRegionLayout(
    const TextureDesc& desc,
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    Size rowAlignment,
    SubresourceLayout* outLayout
)
{
    Extent3D textureSize = desc.size;
    const FormatInfo& formatInfo = getFormatInfo(desc.format);

    if (extent.width == kRemainingTextureSize)
    {
        extent.width = max(1u, (textureSize.width >> mip));
        if (offset.x >= extent.width)
            return SLANG_E_INVALID_ARG;
        extent.width -= offset.x;
    }
    if (extent.height == kRemainingTextureSize)
    {
        extent.height = max(1u, (textureSize.height >> mip));
        if (offset.y >= extent.height)
            return SLANG_E_INVALID_ARG;
        extent.height -= offset.y;
    }
    if (extent.depth == kRemainingTextureSize)
    {
        extent.depth = max(1u, (textureSize.depth >> mip));
        if (offset.z >= extent.depth)
            return SLANG_E_INVALID_ARG;
        extent.depth -= offset.z;
    }

    size_t rowSize = math::divideRoundedUp(extent.width, formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
    size_t rowCount = math::divideRoundedUp(extent.height, formatInfo.blockHeight);
    size_t rowPitch = math::calcAligned2(rowSize, rowAlignment);
    size_t layerPitch = rowPitch * rowCount;

    outLayout->size = extent;
    outLayout->colPitch = formatInfo.blockSizeInBytes;
    outLayout->rowPitch = rowPitch;
    outLayout->slicePitch = layerPitch;
    outLayout->sizeInBytes = layerPitch * extent.depth;
    outLayout->rowCount = rowCount;
    outLayout->blockWidth = formatInfo.blockWidth;
    outLayout->blockHeight = formatInfo.blockHeight;

    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// Texture
// ----------------------------------------------------------------------------

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITexture::getTypeGuid())
        return static_cast<ITexture*>(this);
    return nullptr;
}

Texture::Texture(Device* device, const TextureDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.layer = min(resolved.layer, (m_desc.getLayerCount() - 1));
    resolved.layerCount = min(resolved.layerCount, m_desc.getLayerCount() - resolved.layer);
    resolved.mip = min(resolved.mip, m_desc.mipCount - 1);
    resolved.mipCount = min(resolved.mipCount, m_desc.mipCount - resolved.mip);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.layer > 0 || range.layerCount < m_desc.getLayerCount())
    {
        return false;
    }
    if (range.mip > 0 || range.mipCount < m_desc.mipCount)
    {
        return false;
    }
    return true;
}

Result Texture::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Texture::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Texture::getSubresourceRegionLayout(
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    size_t rowAlignment,
    SubresourceLayout* outLayout
)
{
    if (rowAlignment == kDefaultAlignment)
    {
        SLANG_RETURN_ON_FAIL(m_device->getTextureRowAlignment(m_desc.format, &rowAlignment));
    }
    return calcSubresourceRegionLayout(m_desc, mip, offset, extent, rowAlignment, outLayout);
}

Result Texture::createView(const TextureViewDesc& desc, ITextureView** outTextureView)
{
    return m_device->createTextureView(this, desc, outTextureView);
}

// ----------------------------------------------------------------------------
// TextureView
// ----------------------------------------------------------------------------

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITextureView::getTypeGuid())
        return static_cast<ITextureView*>(this);
    return nullptr;
}

TextureView::TextureView(Device* device, const TextureViewDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

Result TextureView::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result TextureView::getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// Sampler
// ----------------------------------------------------------------------------

ISampler* Sampler::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ISampler::getTypeGuid())
        return static_cast<ISampler*>(this);
    return nullptr;
}

Sampler::Sampler(Device* device, const SamplerDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

const SamplerDesc& Sampler::getDesc()
{
    return m_desc;
}

Result Sampler::getDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Sampler::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// AccelerationStructure
// ----------------------------------------------------------------------------

IAccelerationStructure* AccelerationStructure::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() ||
        guid == IAccelerationStructure::getTypeGuid())
        return static_cast<IAccelerationStructure*>(this);
    return nullptr;
}

AccelerationStructure::AccelerationStructure(Device* device, const AccelerationStructureDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

AccelerationStructureHandle AccelerationStructure::getHandle()
{
    return {};
}

Result AccelerationStructure::getDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// InputLayout
// ----------------------------------------------------------------------------

IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IInputLayout::getTypeGuid())
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

// ----------------------------------------------------------------------------
// QueryPool
// ----------------------------------------------------------------------------

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

QueryPool::QueryPool(Device* device, const QueryPoolDesc& desc)
    : DeviceChild(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

// ----------------------------------------------------------------------------
// ShaderTable
// ----------------------------------------------------------------------------

IShaderTable* ShaderTable::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderTable::getTypeGuid())
        return static_cast<IShaderTable*>(this);
    return nullptr;
}

ShaderTable::ShaderTable(Device* device, const ShaderTableDesc& desc)
    : DeviceChild(device)
{
    m_rayGenShaderCount = desc.rayGenShaderCount;
    m_missShaderCount = desc.missShaderCount;
    m_hitGroupCount = desc.hitGroupCount;
    m_callableShaderCount = desc.callableShaderCount;
    m_shaderGroupNames.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    m_recordOverwrites.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    for (uint32_t i = 0; i < desc.rayGenShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.rayGenShaderEntryPointNames[i]);
        if (desc.rayGenShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.rayGenShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.missShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.missShaderEntryPointNames[i]);
        if (desc.missShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.missShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.hitGroupCount; i++)
    {
        m_shaderGroupNames.push_back(desc.hitGroupNames[i]);
        if (desc.hitGroupRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.hitGroupRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.callableShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.callableShaderEntryPointNames[i]);
        if (desc.callableShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.callableShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
}

// ----------------------------------------------------------------------------
// Surface
// ----------------------------------------------------------------------------

ISurface* Surface::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ISurface::getTypeGuid())
        return static_cast<ISurface*>(this);
    return nullptr;
}

void Surface::setInfo(const SurfaceInfo& info)
{
    m_info = info;
    m_infoHolder.reset();
    m_infoHolder.holdList(m_info.formats, m_info.formatCount);
}

void Surface::setConfig(const SurfaceConfig& config)
{
    m_config = config;
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

bool isDepthFormat(Format format)
{
    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32Float:
    case Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(Format format)
{
    switch (format)
    {
    case Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

} // namespace rhi
