#include "enum-utils.h"

namespace rhi {

static const char* kInvalid = "invalid";

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

const char* enumToString(PrimitiveType value)
{
    switch (value)
    {
    case PrimitiveType::Point:
        return "Point";
    case PrimitiveType::Line:
        return "Line";
    case PrimitiveType::Triangle:
        return "Triangle";
    case PrimitiveType::Patch:
        return "Patch";
    }
    return kInvalid;
}

} // namespace rhi
