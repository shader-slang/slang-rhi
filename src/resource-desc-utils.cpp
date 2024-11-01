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
        {BufferUsage::AccelerationStructure, ResourceState::AccelerationStructure},
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
        {TextureUsage::DepthRead, ResourceState::DepthRead},
        {TextureUsage::DepthWrite, ResourceState::DepthWrite},
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

    if (desc.arrayLength == 0)
        result.arrayLength = 1;
    if (desc.mipLevelCount == 0)
        result.mipLevelCount = calcNumMipLevels(desc.type, desc.size);
    if (desc.defaultState == ResourceState::Undefined)
        result.defaultState = determineDefaultResourceState(desc.usage);

    return result;
}

Format srgbToLinearFormat(Format format)
{
    switch (format)
    {
    case Format::BC1_UNORM_SRGB:
        return Format::BC1_UNORM;
    case Format::BC2_UNORM_SRGB:
        return Format::BC2_UNORM;
    case Format::BC3_UNORM_SRGB:
        return Format::BC3_UNORM;
    case Format::BC7_UNORM_SRGB:
        return Format::BC7_UNORM;
    case Format::B8G8R8A8_UNORM_SRGB:
        return Format::B8G8R8A8_UNORM;
    case Format::B8G8R8X8_UNORM_SRGB:
        return Format::B8G8R8X8_UNORM;
    case Format::R8G8B8A8_UNORM_SRGB:
        return Format::R8G8B8A8_UNORM;
    default:
        return format;
    }
}

} // namespace rhi
