#include "enum-strings.h"
#include "strings.h"

namespace rhi {

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
        return S_DeviceType_Default;
    case DeviceType::D3D11:
        return S_DeviceType_D3D11;
    case DeviceType::D3D12:
        return S_DeviceType_D3D12;
    case DeviceType::Vulkan:
        return S_DeviceType_Vulkan;
    case DeviceType::Metal:
        return S_DeviceType_Metal;
    case DeviceType::CPU:
        return S_DeviceType_CPU;
    case DeviceType::CUDA:
        return S_DeviceType_CUDA;
    case DeviceType::WGPU:
        return S_DeviceType_WGPU;
    }
    return S_INVALID;
}

const char* enumToString(Format value)
{
    if (int(value) >= 0 && int(value) < int(Format::_Count))
    {
        return getFormatInfo(value).name;
    }
    else
    {
        return S_INVALID;
    }
}

const char* enumToString(FormatSupport value)
{
    switch (value)
    {
    case FormatSupport::None:
        return S_FormatSupport_None;
    case FormatSupport::Buffer:
        return S_FormatSupport_Buffer;
    case FormatSupport::IndexBuffer:
        return S_FormatSupport_IndexBuffer;
    case FormatSupport::VertexBuffer:
        return S_FormatSupport_VertexBuffer;
    case FormatSupport::Texture:
        return S_FormatSupport_Texture;
    case FormatSupport::DepthStencil:
        return S_FormatSupport_DepthStencil;
    case FormatSupport::RenderTarget:
        return S_FormatSupport_RenderTarget;
    case FormatSupport::Blendable:
        return S_FormatSupport_Blendable;
    case FormatSupport::ShaderLoad:
        return S_FormatSupport_ShaderLoad;
    case FormatSupport::ShaderSample:
        return S_FormatSupport_ShaderSample;
    case FormatSupport::ShaderUavLoad:
        return S_FormatSupport_ShaderUavLoad;
    case FormatSupport::ShaderUavStore:
        return S_FormatSupport_ShaderUavStore;
    case FormatSupport::ShaderAtomic:
        return S_FormatSupport_ShaderAtomic;
    }
    return S_INVALID;
}

const char* enumToString(MemoryType value)
{
    switch (value)
    {
    case MemoryType::DeviceLocal:
        return S_MemoryType_DeviceLocal;
    case MemoryType::Upload:
        return S_MemoryType_Upload;
    case MemoryType::ReadBack:
        return S_MemoryType_ReadBack;
    }
    return S_INVALID;
}

const char* enumToString(BufferUsage value)
{
    switch (value)
    {
    case BufferUsage::None:
        return S_BufferUsage_None;
    case BufferUsage::VertexBuffer:
        return S_BufferUsage_VertexBuffer;
    case BufferUsage::IndexBuffer:
        return S_BufferUsage_IndexBuffer;
    case BufferUsage::ConstantBuffer:
        return S_BufferUsage_ConstantBuffer;
    case BufferUsage::ShaderResource:
        return S_BufferUsage_ShaderResource;
    case BufferUsage::UnorderedAccess:
        return S_BufferUsage_UnorderedAccess;
    case BufferUsage::IndirectArgument:
        return S_BufferUsage_IndirectArgument;
    case BufferUsage::CopySource:
        return S_BufferUsage_CopySource;
    case BufferUsage::CopyDestination:
        return S_BufferUsage_CopyDestination;
    case BufferUsage::AccelerationStructure:
        return S_BufferUsage_AccelerationStructure;
    case BufferUsage::AccelerationStructureBuildInput:
        return S_BufferUsage_AccelerationStructureBuildInput;
    case BufferUsage::ShaderTable:
        return S_BufferUsage_ShaderTable;
    case BufferUsage::Shared:
        return S_BufferUsage_Shared;
    }
    return S_INVALID;
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
        return S_TextureType_Texture1D;
    case TextureType::Texture2D:
        return S_TextureType_Texture2D;
    case TextureType::Texture3D:
        return S_TextureType_Texture3D;
    case TextureType::TextureCube:
        return S_TextureType_TextureCube;
    }
    return S_INVALID;
}

const char* enumToString(TextureAspect value)
{
    switch (value)
    {
    case TextureAspect::All:
        return S_TextureAspect_All;
    case TextureAspect::DepthOnly:
        return S_TextureAspect_DepthOnly;
    case TextureAspect::StencilOnly:
        return S_TextureAspect_StencilOnly;
    }
    return S_INVALID;
}

const char* enumToString(TextureUsage value)
{
    switch (value)
    {
    case TextureUsage::None:
        return S_TextureUsage_None;
    case TextureUsage::ShaderResource:
        return S_TextureUsage_ShaderResource;
    case TextureUsage::UnorderedAccess:
        return S_TextureUsage_UnorderedAccess;
    case TextureUsage::RenderTarget:
        return S_TextureUsage_RenderTarget;
    case TextureUsage::DepthRead:
        return S_TextureUsage_DepthRead;
    case TextureUsage::DepthWrite:
        return S_TextureUsage_DepthWrite;
    case TextureUsage::Present:
        return S_TextureUsage_Present;
    case TextureUsage::CopySource:
        return S_TextureUsage_CopySource;
    case TextureUsage::CopyDestination:
        return S_TextureUsage_CopyDestination;
    case TextureUsage::ResolveSource:
        return S_TextureUsage_ResolveSource;
    case TextureUsage::ResolveDestination:
        return S_TextureUsage_ResolveDestination;
    case TextureUsage::Shared:
        return S_TextureUsage_Shared;
    }
    return S_INVALID;
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
        return S_ResourceState_Undefined;
    case ResourceState::General:
        return S_ResourceState_General;
    case ResourceState::VertexBuffer:
        return S_ResourceState_VertexBuffer;
    case ResourceState::IndexBuffer:
        return S_ResourceState_IndexBuffer;
    case ResourceState::ConstantBuffer:
        return S_ResourceState_ConstantBuffer;
    case ResourceState::StreamOutput:
        return S_ResourceState_StreamOutput;
    case ResourceState::ShaderResource:
        return S_ResourceState_ShaderResource;
    case ResourceState::UnorderedAccess:
        return S_ResourceState_UnorderedAccess;
    case ResourceState::RenderTarget:
        return S_ResourceState_RenderTarget;
    case ResourceState::DepthRead:
        return S_ResourceState_DepthRead;
    case ResourceState::DepthWrite:
        return S_ResourceState_DepthWrite;
    case ResourceState::Present:
        return S_ResourceState_Present;
    case ResourceState::IndirectArgument:
        return S_ResourceState_IndirectArgument;
    case ResourceState::CopySource:
        return S_ResourceState_CopySource;
    case ResourceState::CopyDestination:
        return S_ResourceState_CopyDestination;
    case ResourceState::ResolveSource:
        return S_ResourceState_ResolveSource;
    case ResourceState::ResolveDestination:
        return S_ResourceState_ResolveDestination;
    case ResourceState::AccelerationStructure:
        return S_ResourceState_AccelerationStructure;
    case ResourceState::AccelerationStructureBuildInput:
        return S_ResourceState_AccelerationStructureBuildInput;
    }
    return S_INVALID;
}

const char* enumToString(TextureFilteringMode value)
{
    switch (value)
    {
    case TextureFilteringMode::Point:
        return S_TextureFilteringMode_Point;
    case TextureFilteringMode::Linear:
        return S_TextureFilteringMode_Linear;
    }
    return S_INVALID;
}

const char* enumToString(TextureAddressingMode value)
{
    switch (value)
    {
    case TextureAddressingMode::Wrap:
        return S_TextureAddressingMode_Wrap;
    case TextureAddressingMode::ClampToEdge:
        return S_TextureAddressingMode_ClampToEdge;
    case TextureAddressingMode::ClampToBorder:
        return S_TextureAddressingMode_ClampToBorder;
    case TextureAddressingMode::MirrorRepeat:
        return S_TextureAddressingMode_MirrorRepeat;
    case TextureAddressingMode::MirrorOnce:
        return S_TextureAddressingMode_MirrorOnce;
    }
    return S_INVALID;
}

const char* enumToString(ComparisonFunc value)
{
    switch (value)
    {
    case ComparisonFunc::Never:
        return S_ComparisonFunc_Never;
    case ComparisonFunc::Less:
        return S_ComparisonFunc_Less;
    case ComparisonFunc::Equal:
        return S_ComparisonFunc_Equal;
    case ComparisonFunc::LessEqual:
        return S_ComparisonFunc_LessEqual;
    case ComparisonFunc::Greater:
        return S_ComparisonFunc_Greater;
    case ComparisonFunc::NotEqual:
        return S_ComparisonFunc_NotEqual;
    case ComparisonFunc::GreaterEqual:
        return S_ComparisonFunc_GreaterEqual;
    case ComparisonFunc::Always:
        return S_ComparisonFunc_Always;
    }
    return S_INVALID;
}

const char* enumToString(TextureReductionOp value)
{
    switch (value)
    {
    case TextureReductionOp::Average:
        return S_TextureReductionOp_Average;
    case TextureReductionOp::Comparison:
        return S_TextureReductionOp_Comparison;
    case TextureReductionOp::Minimum:
        return S_TextureReductionOp_Minimum;
    case TextureReductionOp::Maximum:
        return S_TextureReductionOp_Maximum;
    }
    return S_INVALID;
}

const char* enumToString(InputSlotClass value)
{
    switch (value)
    {
    case InputSlotClass::PerVertex:
        return S_InputSlotClass_PerVertex;
    case InputSlotClass::PerInstance:
        return S_InputSlotClass_PerInstance;
    }
    return S_INVALID;
}

const char* enumToString(PrimitiveTopology value)
{
    switch (value)
    {
    case PrimitiveTopology::PointList:
        return S_PrimitiveTopology_PointList;
    case PrimitiveTopology::LineList:
        return S_PrimitiveTopology_LineList;
    case PrimitiveTopology::LineStrip:
        return S_PrimitiveTopology_LineStrip;
    case PrimitiveTopology::TriangleList:
        return S_PrimitiveTopology_TriangleList;
    case PrimitiveTopology::TriangleStrip:
        return S_PrimitiveTopology_TriangleStrip;
    case PrimitiveTopology::PatchList:
        return S_PrimitiveTopology_PatchList;
    }
    return S_INVALID;
}

} // namespace rhi
