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

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
}

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

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialTypeLayout
)
{
    // Our task here is to figure out if a value of `concreteTypeLayout`
    // can fit into an existential value using `existentialTypelayout`.

    // We can start by asking how many bytes the concrete type of the object consumes.
    //
    auto concreteValueSize = concreteTypeLayout->getSize();

    // We can also compute how many bytes the existential-type value provides,
    // but we need to remember that the *payload* part of that value comes after
    // the header with RTTI and witness-table IDs, so the payload is 16 bytes
    // smaller than the entire value.
    //
    auto existentialValueSize = existentialTypeLayout->getSize();
    auto existentialPayloadSize = existentialValueSize - 16;

    // If the concrete type consumes more ordinary bytes than we have in the payload,
    // it cannot possibly fit.
    //
    if (concreteValueSize > existentialPayloadSize)
        return false;

    // It is possible that the ordinary bytes of `concreteTypeLayout` can fit
    // in the payload, but that type might also use storage other than ordinary
    // bytes. In that case, the value would *not* fit, because all the non-ordinary
    // data can't fit in the payload at all.
    //
    auto categoryCount = concreteTypeLayout->getCategoryCount();
    for (unsigned int i = 0; i < categoryCount; ++i)
    {
        auto category = concreteTypeLayout->getCategoryByIndex(i);
        switch (category)
        {
        // We want to ignore any ordinary/uniform data usage, since that
        // was already checked above.
        //
        case slang::ParameterCategory::Uniform:
            break;

        // Any other kind of data consumed means the value cannot possibly fit.
        default:
            return false;

            // TODO: Are there any cases of resource usage that need to be ignored here?
            // E.g., if the sub-object contains its own existential-type fields (which
            // get reflected as consuming "existential value" storage) should that be
            // ignored?
        }
    }

    // If we didn't reject the concrete type above for either its ordinary
    // data or some use of non-ordinary data, then it seems like it must fit.
    //
    return true;
}


IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IInputLayout::getTypeGuid())
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

IPipeline* RenderPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRenderPipeline::getTypeGuid())
        return static_cast<IRenderPipeline*>(this);
    return nullptr;
}

Result VirtualRenderPipeline::init(Device* device, const RenderPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.targets, m_desc.targetCount);
    m_program = checked_cast<ShaderProgram*>(desc.program);
    m_inputLayout = checked_cast<InputLayout*>(desc.inputLayout);
    return SLANG_OK;
}

Result VirtualRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IPipeline* ComputePipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IComputePipeline::getTypeGuid())
        return static_cast<IComputePipeline*>(this);
    return nullptr;
}

Result VirtualComputePipeline::init(Device* device, const ComputePipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IPipeline* RayTracingPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRayTracingPipeline::getTypeGuid())
        return static_cast<IRayTracingPipeline*>(this);
    return nullptr;
}

Result VirtualRayTracingPipeline::init(Device* device, const RayTracingPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.hitGroups, m_desc.hitGroupCount);
    for (uint32_t i = 0; i < m_desc.hitGroupCount; i++)
    {
        m_descHolder.holdString(m_desc.hitGroups[i].hitGroupName);
        m_descHolder.holdString(m_desc.hitGroups[i].closestHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].anyHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].intersectionEntryPoint);
    }
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

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
