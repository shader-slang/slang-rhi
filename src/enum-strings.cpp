#include "enum-strings.h"

namespace rhi {

static const char* kInvalid = "invalid";

template<typename Enum>
std::string flagsToString(Enum value)
{
    std::string result;
    for (uint32_t i = 0; i < 32; i++)
    {
        if (uint32_t(value) & (1 << i))
        {
            if (!result.empty())
                result += "|";
            result += enumToString(Enum(1 << i));
        }
    }
    return result;
}

const char* enumToString(DeviceType value)
{
    switch (value)
    {
    case DeviceType::Default:
        return "Default";
    case DeviceType::D3D11:
        return "D3D11";
    case DeviceType::D3D12:
        return "D3D12";
    case DeviceType::Vulkan:
        return "Vulkan";
    case DeviceType::Metal:
        return "Metal";
    case DeviceType::CPU:
        return "CPU";
    case DeviceType::CUDA:
        return "CUDA";
    }
    return kInvalid;
}

const char* enumToString(Format value)
{
    FormatInfo info;
    if (rhiGetFormatInfo(value, &info) == SLANG_OK)
    {
        return info.name;
    }
    else
    {
        return kInvalid;
    }
}

const char* enumToString(FormatSupport value)
{
    switch (value)
    {
    case FormatSupport::None:
        return "None";
    case FormatSupport::Buffer:
        return "Buffer";
    case FormatSupport::IndexBuffer:
        return "IndexBuffer";
    case FormatSupport::VertexBuffer:
        return "VertexBuffer";
    case FormatSupport::Texture:
        return "Texture";
    case FormatSupport::DepthStencil:
        return "DepthStencil";
    case FormatSupport::RenderTarget:
        return "RenderTarget";
    case FormatSupport::Blendable:
        return "Blendable";
    case FormatSupport::ShaderLoad:
        return "ShaderLoad";
    case FormatSupport::ShaderSample:
        return "ShaderSample";
    case FormatSupport::ShaderUavLoad:
        return "ShaderUavLoad";
    case FormatSupport::ShaderUavStore:
        return "ShaderUavStore";
    case FormatSupport::ShaderAtomic:
        return "ShaderAtomic";
    }
    return kInvalid;
}

const char* enumToString(MemoryType value)
{
    switch (value)
    {
    case MemoryType::DeviceLocal:
        return "DeviceLocal";
    case MemoryType::Upload:
        return "Upload";
    case MemoryType::ReadBack:
        return "ReadBack";
    }
    return kInvalid;
}

const char* enumToString(BufferUsage value)
{
    switch (value)
    {
    case BufferUsage::None:
        return "None";
    case BufferUsage::VertexBuffer:
        return "VertexBuffer";
    case BufferUsage::IndexBuffer:
        return "IndexBuffer";
    case BufferUsage::ConstantBuffer:
        return "ConstantBuffer";
    case BufferUsage::ShaderResource:
        return "ShaderResource";
    case BufferUsage::UnorderedAccess:
        return "UnorderedAccess";
    case BufferUsage::IndirectArgument:
        return "IndirectArgument";
    case BufferUsage::CopySource:
        return "CopySource";
    case BufferUsage::CopyDestination:
        return "CopyDestination";
    case BufferUsage::AccelerationStructure:
        return "AccelerationStructure";
    case BufferUsage::AccelerationStructureBuildInput:
        return "AccelerationStructureBuildInput";
    case BufferUsage::ShaderTable:
        return "ShaderTable";
    }
    return kInvalid;
}

std::string flagsToString(BufferUsage value)
{
    return flagsToString<BufferUsage>(value);
}

const char* enumToString(TextureType value)
{
    switch (value)
    {
    case TextureType::Texture1D:
        return "Texture1D";
    case TextureType::Texture2D:
        return "Texture2D";
    case TextureType::Texture3D:
        return "Texture3D";
    case TextureType::TextureCube:
        return "TextureCube";
    }
    return kInvalid;
}

const char* enumToString(TextureUsage value)
{
    switch (value)
    {
    case TextureUsage::None:
        return "None";
    case TextureUsage::ShaderResource:
        return "ShaderResource";
    case TextureUsage::UnorderedAccess:
        return "UnorderedAccess";
    case TextureUsage::RenderTarget:
        return "RenderTarget";
    case TextureUsage::DepthRead:
        return "DepthRead";
    case TextureUsage::DepthWrite:
        return "DepthWrite";
    case TextureUsage::Present:
        return "Present";
    case TextureUsage::CopySource:
        return "CopySource";
    case TextureUsage::CopyDestination:
        return "CopyDestination";
    case TextureUsage::ResolveSource:
        return "ResolveSource";
    case TextureUsage::ResolveDestination:
        return "ResolveDestination";
    }
    return kInvalid;
}

std::string flagsToString(TextureUsage value)
{
    return flagsToString<TextureUsage>(value);
}

const char* enumToString(ResourceState value)
{
    switch (value)
    {
    case ResourceState::Undefined:
        return "Undefined";
    case ResourceState::General:
        return "General";
    case ResourceState::VertexBuffer:
        return "VertexBuffer";
    case ResourceState::IndexBuffer:
        return "IndexBuffer";
    case ResourceState::ConstantBuffer:
        return "ConstantBuffer";
    case ResourceState::StreamOutput:
        return "StreamOutput";
    case ResourceState::ShaderResource:
        return "ShaderResource";
    case ResourceState::UnorderedAccess:
        return "UnorderedAccess";
    case ResourceState::RenderTarget:
        return "RenderTarget";
    case ResourceState::DepthRead:
        return "DepthRead";
    case ResourceState::DepthWrite:
        return "DepthWrite";
    case ResourceState::Present:
        return "Present";
    case ResourceState::IndirectArgument:
        return "IndirectArgument";
    case ResourceState::CopySource:
        return "CopySource";
    case ResourceState::CopyDestination:
        return "CopyDestination";
    case ResourceState::ResolveSource:
        return "ResolveSource";
    case ResourceState::ResolveDestination:
        return "ResolveDestination";
    case ResourceState::AccelerationStructure:
        return "AccelerationStructure";
    case ResourceState::AccelerationStructureBuildInput:
        return "AccelerationStructureBuildInput";
    }
    return kInvalid;
}

const char* enumToString(TextureFilteringMode value)
{
    switch (value)
    {
    case TextureFilteringMode::Point:
        return "Point";
    case TextureFilteringMode::Linear:
        return "Linear";
    }
    return kInvalid;
}

const char* enumToString(TextureAddressingMode value)
{
    switch (value)
    {
    case TextureAddressingMode::Wrap:
        return "Wrap";
    case TextureAddressingMode::ClampToEdge:
        return "ClampToEdge";
    case TextureAddressingMode::ClampToBorder:
        return "ClampToBorder";
    case TextureAddressingMode::MirrorRepeat:
        return "MirrorRepeat";
    case TextureAddressingMode::MirrorOnce:
        return "MirrorOnce";
    }
    return kInvalid;
}

const char* enumToString(ComparisonFunc value)
{
    switch (value)
    {
    case ComparisonFunc::Never:
        return "Never";
    case ComparisonFunc::Less:
        return "Less";
    case ComparisonFunc::Equal:
        return "Equal";
    case ComparisonFunc::LessEqual:
        return "LessEqual";
    case ComparisonFunc::Greater:
        return "Greater";
    case ComparisonFunc::NotEqual:
        return "NotEqual";
    case ComparisonFunc::GreaterEqual:
        return "GreaterEqual";
    case ComparisonFunc::Always:
        return "Always";
    }
    return kInvalid;
}

const char* enumToString(TextureReductionOp value)
{
    switch (value)
    {
    case TextureReductionOp::Average:
        return "Average";
    case TextureReductionOp::Comparison:
        return "Comparison";
    case TextureReductionOp::Minimum:
        return "Minimum";
    case TextureReductionOp::Maximum:
        return "Maximum";
    }
    return kInvalid;
}

const char* enumToString(InputSlotClass value)
{
    switch (value)
    {
    case InputSlotClass::PerVertex:
        return "PerVertex";
    case InputSlotClass::PerInstance:
        return "PerInstance";
    }
    return kInvalid;
}

const char* enumToString(PrimitiveTopology value)
{
    switch (value)
    {
    case PrimitiveTopology::PointList:
        return "PointList";
    case PrimitiveTopology::LineList:
        return "LineList";
    case PrimitiveTopology::LineStrip:
        return "LineStrip";
    case PrimitiveTopology::TriangleList:
        return "TriangleList";
    case PrimitiveTopology::TriangleStrip:
        return "TriangleStrip";
    case PrimitiveTopology::PatchList:
        return "PatchList";
    }
    return kInvalid;
}

} // namespace rhi
