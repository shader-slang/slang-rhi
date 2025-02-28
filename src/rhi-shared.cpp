#include "rhi-shared.h"
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
// Fence
// ----------------------------------------------------------------------------

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
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

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = min(resolved.offset, m_desc.size);
    resolved.size = min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

BufferDesc& Buffer::getDesc()
{
    return m_desc;
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

// ----------------------------------------------------------------------------
// Texture
// ----------------------------------------------------------------------------

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITexture::getTypeGuid())
        return static_cast<ITexture*>(this);
    return nullptr;
}

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.mipLevel = min(resolved.mipLevel, m_desc.mipLevelCount);
    resolved.mipLevelCount = min(resolved.mipLevelCount, m_desc.mipLevelCount - resolved.mipLevel);
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    resolved.baseArrayLayer = min(resolved.baseArrayLayer, arrayLayerCount);
    resolved.layerCount = min(resolved.layerCount, arrayLayerCount - resolved.baseArrayLayer);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.mipLevel > 0 || range.mipLevelCount < m_desc.mipLevelCount)
    {
        return false;
    }
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    if (range.baseArrayLayer > 0 || range.layerCount < arrayLayerCount)
    {
        return false;
    }
    return true;
}

TextureDesc& Texture::getDesc()
{
    return m_desc;
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

// ----------------------------------------------------------------------------
// TextureView
// ----------------------------------------------------------------------------

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITextureView::getTypeGuid())
        return static_cast<ITextureView*>(this);
    return nullptr;
}

Result TextureView::getNativeHandle(NativeHandle* outHandle)
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

const SamplerDesc& Sampler::getDesc()
{
    return m_desc;
}

Result Sampler::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_IMPLEMENTED;
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

AccelerationStructureHandle AccelerationStructure::getHandle()
{
    return {};
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
    case Format::D16_UNORM:
    case Format::D32_FLOAT:
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(Format format)
{
    switch (format)
    {
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

} // namespace rhi
