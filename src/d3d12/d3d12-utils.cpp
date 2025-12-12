#include "d3d12-utils.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-query.h"

#include "core/string.h"

namespace rhi::d3d12 {

bool isSupportedNVAPIOp(ID3D12Device* dev, uint32_t op)
{
#if SLANG_RHI_ENABLE_NVAPI
    {
        bool isSupported;
        NvAPI_Status status = NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(dev, NvU32(op), &isSupported);
        return status == NVAPI_OK && isSupported;
    }
#else
    return false;
#endif
}

D3D12_RESOURCE_FLAGS calcResourceFlags(BufferUsage usage)
{
    int flags = D3D12_RESOURCE_FLAG_NONE;
    if (is_set(usage, BufferUsage::UnorderedAccess))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (is_set(usage, BufferUsage::AccelerationStructure))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return (D3D12_RESOURCE_FLAGS)flags;
}

D3D12_RESOURCE_FLAGS calcResourceFlags(TextureUsage usage)
{
    int flags = D3D12_RESOURCE_FLAG_NONE;
    if (is_set(usage, TextureUsage::RenderTarget))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (is_set(usage, TextureUsage::DepthStencil))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return (D3D12_RESOURCE_FLAGS)flags;
}

D3D12_RESOURCE_DIMENSION calcResourceDimension(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case TextureType::Texture3D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    }
    return D3D12_RESOURCE_DIMENSION_UNKNOWN;
}

bool isTypelessDepthFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R24G8_TYPELESS:
        return true;
    default:
        return false;
    }
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE translatePrimitiveTopologyType(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveTopology::PatchList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid PrimitiveTopology value");
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE(0);
}

D3D12_FILTER_TYPE translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return D3D12_FILTER_TYPE_POINT;
    case TextureFilteringMode::Linear:
        return D3D12_FILTER_TYPE_LINEAR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return D3D12_FILTER_TYPE(0);
}

D3D12_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op)
{
    switch (op)
    {
    case TextureReductionOp::Average:
        return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    case TextureReductionOp::Comparison:
        return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
    case TextureReductionOp::Minimum:
        return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
    case TextureReductionOp::Maximum:
        return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureReductionOp value");
    return D3D12_FILTER_REDUCTION_TYPE(0);
}

D3D12_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case TextureAddressingMode::ClampToEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case TextureAddressingMode::ClampToBorder:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case TextureAddressingMode::MirrorRepeat:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case TextureAddressingMode::MirrorOnce:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAddressingMode value");
    return D3D12_TEXTURE_ADDRESS_MODE(0);
}

D3D12_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return D3D12_COMPARISON_FUNC_NEVER;
    case ComparisonFunc::Less:
        return D3D12_COMPARISON_FUNC_LESS;
    case ComparisonFunc::Equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case ComparisonFunc::LessEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case ComparisonFunc::Greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case ComparisonFunc::NotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case ComparisonFunc::Always:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid ComparisonFunc value");
    return D3D12_COMPARISON_FUNC(0);
}

D3D12_STENCIL_OP translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return D3D12_STENCIL_OP_KEEP;
    case StencilOp::Zero:
        return D3D12_STENCIL_OP_ZERO;
    case StencilOp::Replace:
        return D3D12_STENCIL_OP_REPLACE;
    case StencilOp::IncrementSaturate:
        return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementSaturate:
        return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:
        return D3D12_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:
        return D3D12_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap:
        return D3D12_STENCIL_OP_DECR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StencilOp value");
    return D3D12_STENCIL_OP(0);
}

D3D12_DEPTH_STENCILOP_DESC translateStencilOpDesc(DepthStencilOpDesc desc)
{
    D3D12_DEPTH_STENCILOP_DESC rs;
    rs.StencilDepthFailOp = translateStencilOp(desc.stencilDepthFailOp);
    rs.StencilFailOp = translateStencilOp(desc.stencilFailOp);
    rs.StencilFunc = translateComparisonFunc(desc.stencilFunc);
    rs.StencilPassOp = translateStencilOp(desc.stencilPassOp);
    return rs;
}

D3D12_INPUT_CLASSIFICATION translateInputSlotClass(InputSlotClass slotClass)
{
    switch (slotClass)
    {
    case InputSlotClass::PerVertex:
        return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    case InputSlotClass::PerInstance:
        return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid InputSlotClass value");
    return D3D12_INPUT_CLASSIFICATION(0);
}

D3D12_FILL_MODE translateFillMode(FillMode mode)
{
    switch (mode)
    {
    case FillMode::Solid:
        return D3D12_FILL_MODE_SOLID;
    case FillMode::Wireframe:
        return D3D12_FILL_MODE_WIREFRAME;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FillMode value");
    return D3D12_FILL_MODE(0);
}

D3D12_CULL_MODE translateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return D3D12_CULL_MODE_NONE;
    case CullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case CullMode::Back:
        return D3D12_CULL_MODE_BACK;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CullMode value");
    return D3D12_CULL_MODE(0);
}

D3D12_BLEND_OP translateBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return D3D12_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:
        return D3D12_BLEND_OP_MIN;
    case BlendOp::Max:
        return D3D12_BLEND_OP_MAX;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendOp value");
    return D3D12_BLEND_OP(0);
}

D3D12_BLEND translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::One:
        return D3D12_BLEND_ONE;
    case BlendFactor::SrcColor:
        return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:
        return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:
        return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:
        return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor:
        return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlphaSaturate:
        return D3D12_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::BlendColor:
        return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::InvBlendColor:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::SecondarySrcColor:
        return D3D12_BLEND_SRC1_COLOR;
    case BlendFactor::InvSecondarySrcColor:
        return D3D12_BLEND_INV_SRC1_COLOR;
    case BlendFactor::SecondarySrcAlpha:
        return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::InvSecondarySrcAlpha:
        return D3D12_BLEND_INV_SRC1_ALPHA;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendFactor value");
    return D3D12_BLEND(0);
}

D3D12_RESOURCE_STATES translateResourceState(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::General:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::VertexBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ConstantBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::StreamOutput:
        return D3D12_RESOURCE_STATE_STREAM_OUT;
    case ResourceState::ShaderResource:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::UnorderedAccess:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::RenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::DepthWrite:;
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::IndirectArgument:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case ResourceState::CopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDestination:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::ResolveSource:
        return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case ResourceState::ResolveDestination:
        return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case ResourceState::AccelerationStructureRead:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    case ResourceState::AccelerationStructureWrite:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    case ResourceState::AccelerationStructureBuildInput:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

Result initTextureDesc(D3D12_RESOURCE_DESC& resourceDesc, const TextureDesc& textureDesc, bool isTypeless)
{
    const DXGI_FORMAT pixelFormat = isTypeless ? getFormatMapping(textureDesc.format).typelessFormat
                                               : getFormatMapping(textureDesc.format).rtvFormat;
    if (pixelFormat == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    const D3D12_RESOURCE_DIMENSION dimension = calcResourceDimension(textureDesc.type);
    if (dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    resourceDesc.Dimension = dimension;
    resourceDesc.Format = pixelFormat;
    resourceDesc.Width = textureDesc.size.width;
    resourceDesc.Height = textureDesc.size.height;
    resourceDesc.DepthOrArraySize =
        textureDesc.type == TextureType::Texture3D ? textureDesc.size.depth : textureDesc.getLayerCount();
    resourceDesc.MipLevels = textureDesc.mipCount;
    resourceDesc.SampleDesc.Count = textureDesc.sampleCount;
    resourceDesc.SampleDesc.Quality = textureDesc.sampleQuality;

    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    resourceDesc.Flags |= calcResourceFlags(textureDesc.usage);

    resourceDesc.Alignment = 0;

    return SLANG_OK;
}

void initBufferDesc(Size bufferSize, D3D12_RESOURCE_DESC& out)
{
    out = {};

    out.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    out.Alignment = 0;
    out.Width = bufferSize;
    out.Height = 1;
    out.DepthOrArraySize = 1;
    out.MipLevels = 1;
    out.Format = DXGI_FORMAT_UNKNOWN;
    out.SampleDesc.Count = 1;
    out.SampleDesc.Quality = 0;
    out.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    out.Flags = D3D12_RESOURCE_FLAG_NONE;
}

Result createNullDescriptor(
    ID3D12Device* d3dDevice,
    D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor,
    slang::BindingType bindingType,
    SlangResourceShape resourceShape
)
{
    switch (bindingType)
    {
    case slang::BindingType::ConstantBuffer:
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = 0;
        cbvDesc.SizeInBytes = 0;
        d3dDevice->CreateConstantBufferView(&cbvDesc, destDescriptor);
        break;
    }
    case slang::BindingType::MutableRawBuffer:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        d3dDevice->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, destDescriptor);
        break;
    }
    case slang::BindingType::MutableTypedBuffer:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d3dDevice->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, destDescriptor);
        break;
    }
    case slang::BindingType::RawBuffer:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
        break;
    }
    case slang::BindingType::TypedBuffer:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
        break;
    }
    case slang::BindingType::Texture:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (resourceShape)
        {
        case SLANG_TEXTURE_1D:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            break;
        case SLANG_TEXTURE_1D_ARRAY:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            break;
        case SLANG_TEXTURE_2D:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            break;
        case SLANG_TEXTURE_2D_ARRAY:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            break;
        case SLANG_TEXTURE_3D:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            break;
        case SLANG_TEXTURE_CUBE:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            break;
        case SLANG_TEXTURE_CUBE_ARRAY:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            break;
        case SLANG_TEXTURE_2D_MULTISAMPLE:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            break;
        case SLANG_TEXTURE_2D_MULTISAMPLE_ARRAY:
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            break;
        default:
            return SLANG_OK;
        }
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
        break;
    }
    case slang::BindingType::MutableTexture:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        switch (resourceShape)
        {
        case SLANG_TEXTURE_1D:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            break;
        case SLANG_TEXTURE_1D_ARRAY:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            break;
        case SLANG_TEXTURE_2D:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            break;
        case SLANG_TEXTURE_2D_ARRAY:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            break;
        case SLANG_TEXTURE_3D:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            break;
        case SLANG_TEXTURE_CUBE:
        case SLANG_TEXTURE_CUBE_ARRAY:
        case SLANG_TEXTURE_2D_MULTISAMPLE:
        case SLANG_TEXTURE_2D_MULTISAMPLE_ARRAY:
        default:
            return SLANG_OK;
        }
        d3dDevice->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, destDescriptor);
        break;
    }
    case slang::BindingType::RayTracingAccelerationStructure:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = 0;
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
        break;
    }
    default:
        break;
    }
    return SLANG_OK;
}

void translatePostBuildInfoDescs(
    uint32_t propertyQueryCount,
    const AccelerationStructureQueryDesc* queryDescs,
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC>& postBuildInfoDescs
)
{
    postBuildInfoDescs.resize(propertyQueryCount);
    for (uint32_t i = 0; i < propertyQueryCount; i++)
    {
        switch (queryDescs[i].queryType)
        {
        case QueryType::AccelerationStructureCompactedSize:
            postBuildInfoDescs[i].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
            postBuildInfoDescs[i].DestBuffer =
                checked_cast<PlainBufferProxyQueryPoolImpl*>(queryDescs[i].queryPool)->m_buffer->getDeviceAddress() +
                sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC) *
                    queryDescs[i].firstQueryIndex;
            break;
        case QueryType::AccelerationStructureCurrentSize:
            postBuildInfoDescs[i].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
            postBuildInfoDescs[i].DestBuffer =
                checked_cast<PlainBufferProxyQueryPoolImpl*>(queryDescs[i].queryPool)->m_buffer->getDeviceAddress() +
                sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC) *
                    queryDescs[i].firstQueryIndex;
            break;
        case QueryType::AccelerationStructureSerializedSize:
            postBuildInfoDescs[i].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION;
            postBuildInfoDescs[i].DestBuffer =
                checked_cast<PlainBufferProxyQueryPoolImpl*>(queryDescs[i].queryPool)->m_buffer->getDeviceAddress() +
                sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION_DESC) *
                    queryDescs[i].firstQueryIndex;
            break;
        default:
            break;
        }
    }
}

#if SLANG_RHI_ENABLE_NVAPI

NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE translateCooperativeVectorComponentType(CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Float16:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT16;
    case CooperativeVectorComponentType::Float32:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT32;
    case CooperativeVectorComponentType::Float64:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT64;
    case CooperativeVectorComponentType::Sint8:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8;
    case CooperativeVectorComponentType::Sint16:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT16;
    case CooperativeVectorComponentType::Sint32:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT32;
    case CooperativeVectorComponentType::Sint64:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT64;
    case CooperativeVectorComponentType::Uint8:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8;
    case CooperativeVectorComponentType::Uint16:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT16;
    case CooperativeVectorComponentType::Uint32:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT32;
    case CooperativeVectorComponentType::Uint64:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT64;
    case CooperativeVectorComponentType::Sint8Packed:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8_PACKED;
    case CooperativeVectorComponentType::Uint8Packed:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8_PACKED;
    case CooperativeVectorComponentType::FloatE4M3:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E4M3;
    case CooperativeVectorComponentType::FloatE5M2:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E5M2;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CooperativeVectorComponentType value");
    return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE(0);
}

CooperativeVectorComponentType translateCooperativeVectorComponentType(NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE type)
{
    switch (type)
    {
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT16:
        return CooperativeVectorComponentType::Float16;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT32:
        return CooperativeVectorComponentType::Float32;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT64:
        return CooperativeVectorComponentType::Float64;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8:
        return CooperativeVectorComponentType::Sint8;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT16:
        return CooperativeVectorComponentType::Sint16;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT32:
        return CooperativeVectorComponentType::Sint32;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT64:
        return CooperativeVectorComponentType::Sint64;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8:
        return CooperativeVectorComponentType::Uint8;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT16:
        return CooperativeVectorComponentType::Uint16;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT32:
        return CooperativeVectorComponentType::Uint32;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT64:
        return CooperativeVectorComponentType::Uint64;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8_PACKED:
        return CooperativeVectorComponentType::Sint8Packed;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8_PACKED:
        return CooperativeVectorComponentType::Uint8Packed;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E4M3:
        return CooperativeVectorComponentType::FloatE4M3;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E5M2:
        return CooperativeVectorComponentType::FloatE5M2;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE value");
        return CooperativeVectorComponentType(0);
    }
}

NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT translateCooperativeVectorMatrixLayout(CooperativeVectorMatrixLayout layout)
{
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
        return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL;
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CooperativeVectorMatrixLayout value");
    return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT(0);
}

CooperativeVectorMatrixLayout translateCooperativeVectorMatrixLayout(NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT layout)
{
    switch (layout)
    {
    case NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR:
        return CooperativeVectorMatrixLayout::RowMajor;
    case NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR:
        return CooperativeVectorMatrixLayout::ColumnMajor;
    case NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL:
        return CooperativeVectorMatrixLayout::InferencingOptimal;
    case NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL:
        return CooperativeVectorMatrixLayout::TrainingOptimal;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT value");
        return CooperativeVectorMatrixLayout(0);
    }
}

#endif // SLANG_RHI_ENABLE_NVAPI

} // namespace rhi::d3d12
