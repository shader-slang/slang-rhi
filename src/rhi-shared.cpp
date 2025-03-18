#include "rhi-shared.h"
#include "device-child.h"
#include "command-list.h"

#include "core/common.h"

#include <atomic>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

// ----------------------------------------------------------------------------
// Extents
// ----------------------------------------------------------------------------

Extents Extents::kWholeTexture = {kRemainingTextureSize, kRemainingTextureSize, kRemainingTextureSize};

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
// Texture helpers
// ----------------------------------------------------------------------------

Result calcSubresourceRegionLayout(
    const TextureDesc& desc,
    uint32_t mipLevel,
    Offset3D offset,
    Extents extents,
    Size rowAlignment,
    SubresourceLayout* outLayout
)
{
    Extents textureSize = desc.size;
    const FormatInfo& formatInfo = getFormatInfo(desc.format);

    if (extents.width == kRemainingTextureSize)
    {
        extents.width = max(1, (textureSize.width >> mipLevel));
        if (offset.x >= extents.width)
            return SLANG_E_INVALID_ARG;
        extents.width -= offset.x;
    }
    if (extents.height == kRemainingTextureSize)
    {
        extents.height = max(1, (textureSize.height >> mipLevel));
        if (offset.y >= extents.height)
            return SLANG_E_INVALID_ARG;
        extents.height -= offset.y;
    }
    if (extents.depth == kRemainingTextureSize)
    {
        extents.depth = max(1, (textureSize.depth >> mipLevel));
        if (offset.z >= extents.depth)
            return SLANG_E_INVALID_ARG;
        extents.depth -= offset.z;
    }

    size_t rowSize = math::divideRoundedUp(extents.width, formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
    size_t rowCount = math::divideRoundedUp(extents.height, formatInfo.blockHeight);
    size_t rowPitch = math::calcAligned2(rowSize, rowAlignment);
    size_t layerPitch = rowPitch * rowCount;

    outLayout->size = extents;
    outLayout->strideX = formatInfo.blockSizeInBytes;
    outLayout->strideY = rowPitch;
    outLayout->strideZ = layerPitch;
    outLayout->sizeInBytes = layerPitch * extents.depth;
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

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.mipLevel = min(resolved.mipLevel, m_desc.mipLevelCount);
    resolved.mipLevelCount = min(resolved.mipLevelCount, m_desc.mipLevelCount - resolved.mipLevel);
    resolved.baseArrayLayer = min(resolved.baseArrayLayer, m_desc.getLayerCount());
    resolved.layerCount = min(resolved.layerCount, m_desc.getLayerCount() - resolved.baseArrayLayer);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.mipLevel > 0 || range.mipLevelCount < m_desc.mipLevelCount)
    {
        return false;
    }
    if (range.baseArrayLayer > 0 || range.layerCount < m_desc.getLayerCount())
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

Result Texture::getSubresourceRegionLayout(
    uint32_t mipLevel,
    Offset3D offset,
    Extents extents,
    SubresourceLayout* outLayout
)
{
    size_t rowAlignment;
    SLANG_RETURN_ON_FAIL(m_device->getTextureRowAlignment(m_desc.format, &rowAlignment));
    return calcSubresourceRegionLayout(m_desc, mipLevel, offset, extents, rowAlignment, outLayout);
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
// ShaderTable
// ----------------------------------------------------------------------------

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
