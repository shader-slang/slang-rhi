#include "resource-desc-utils.h"

namespace rhi {

inline ResourceState determineDefaultResourceState(BufferUsage usage)
{
    struct BufferUsageMapping
    {
        BufferUsage usage;
        ResourceState state;
    };
    static const BufferUsageMapping kBufferUsageMappings[] = {
        {BufferUsage::ShaderTable, ResourceState::ShaderResource},
        {BufferUsage::VertexBuffer, ResourceState::VertexBuffer},
        {BufferUsage::IndexBuffer, ResourceState::IndexBuffer},
        {BufferUsage::AccelerationStructure, ResourceState::AccelerationStructureRead},
        {BufferUsage::AccelerationStructureBuildInput, ResourceState::AccelerationStructureBuildInput},
        {BufferUsage::ConstantBuffer, ResourceState::ConstantBuffer},
        {BufferUsage::ShaderResource, ResourceState::ShaderResource},
        {BufferUsage::UnorderedAccess, ResourceState::UnorderedAccess},
        {BufferUsage::IndirectArgument, ResourceState::IndirectArgument},
        {BufferUsage::CopySource, ResourceState::CopySource},
        {BufferUsage::CopyDestination, ResourceState::CopyDestination},
    };
    for (const auto& mapping : kBufferUsageMappings)
    {
        if (is_set(usage, mapping.usage))
            return mapping.state;
    }
    return ResourceState::General;
}

inline ResourceState determineDefaultResourceState(TextureUsage usage)
{
    struct TextureUsageMapping
    {
        TextureUsage usage;
        ResourceState state;
    };
    static const TextureUsageMapping kTextureUsageMappings[] = {
        {TextureUsage::ShaderResource, ResourceState::ShaderResource},
        {TextureUsage::UnorderedAccess, ResourceState::UnorderedAccess},
        {TextureUsage::RenderTarget, ResourceState::RenderTarget},
        {TextureUsage::DepthStencil, ResourceState::DepthWrite},
        {TextureUsage::CopySource, ResourceState::CopySource},
        {TextureUsage::CopyDestination, ResourceState::CopyDestination},
        {TextureUsage::ResolveSource, ResourceState::ResolveSource},
        {TextureUsage::ResolveDestination, ResourceState::ResolveDestination},
    };
    for (const auto& mapping : kTextureUsageMappings)
    {
        if (is_set(usage, mapping.usage))
            return mapping.state;
    }
    return ResourceState::General;
}

BufferDesc fixupBufferDesc(const BufferDesc& desc)
{
    BufferDesc result = desc;

    if (desc.defaultState == ResourceState::Undefined)
        result.defaultState = determineDefaultResourceState(desc.usage);

    return result;
}

TextureDesc fixupTextureDesc(const TextureDesc& desc)
{
    TextureDesc result = desc;

    if (desc.mipCount == kAllMips)
        result.mipCount = calcMipCount(desc.type, desc.size);
    if (desc.defaultState == ResourceState::Undefined)
        result.defaultState = determineDefaultResourceState(desc.usage);

    return result;
}

Format srgbToLinearFormat(Format format)
{
    switch (format)
    {
    case Format::BC1UnormSrgb:
        return Format::BC1Unorm;
    case Format::BC2UnormSrgb:
        return Format::BC2Unorm;
    case Format::BC3UnormSrgb:
        return Format::BC3Unorm;
    case Format::BC7UnormSrgb:
        return Format::BC7Unorm;
    case Format::BGRA8UnormSrgb:
        return Format::BGRA8Unorm;
    case Format::BGRX8UnormSrgb:
        return Format::BGRX8Unorm;
    case Format::RGBA8UnormSrgb:
        return Format::RGBA8Unorm;
    default:
        return format;
    }
}

} // namespace rhi
