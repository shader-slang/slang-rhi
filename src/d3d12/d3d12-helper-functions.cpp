#include "d3d12-helper-functions.h"
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
    if (is_set(usage, TextureUsage::DepthRead))
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (is_set(usage, TextureUsage::DepthWrite))
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
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case TextureType::TextureCube:
    case TextureType::Texture2D:
    {
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    }
    case TextureType::Texture3D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    default:
        return D3D12_RESOURCE_DIMENSION_UNKNOWN;
    }
}

DXGI_FORMAT getTypelessFormatFromDepthFormat(Format format)
{
    switch (format)
    {
    case Format::D16_UNORM:
        return DXGI_FORMAT_R16_TYPELESS;
    case Format::D32_FLOAT:
        return DXGI_FORMAT_R32_TYPELESS;
    case Format::D32_FLOAT_S8_UINT:
        return DXGI_FORMAT_R32G8X24_TYPELESS;
    // case Format::D24_UNORM_S8_UINT:
    //     return DXGI_FORMAT_R24G8_TYPELESS;
    default:
        return D3DUtil::getMapFormat(format);
    }
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

D3D12_FILTER_TYPE translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    default:
        return D3D12_FILTER_TYPE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureFilteringMode::SRC:                                                                                    \
        return D3D12_FILTER_TYPE_##DST

        CASE(Point, POINT);
        CASE(Linear, LINEAR);

#undef CASE
    }
}

D3D12_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op)
{
    switch (op)
    {
    default:
        return D3D12_FILTER_REDUCTION_TYPE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureReductionOp::SRC:                                                                                      \
        return D3D12_FILTER_REDUCTION_TYPE_##DST

        CASE(Average, STANDARD);
        CASE(Comparison, COMPARISON);
        CASE(Minimum, MINIMUM);
        CASE(Maximum, MAXIMUM);

#undef CASE
    }
}

D3D12_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    default:
        return D3D12_TEXTURE_ADDRESS_MODE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureAddressingMode::SRC:                                                                                   \
        return D3D12_TEXTURE_ADDRESS_MODE_##DST

        CASE(Wrap, WRAP);
        CASE(ClampToEdge, CLAMP);
        CASE(ClampToBorder, BORDER);
        CASE(MirrorRepeat, MIRROR);
        CASE(MirrorOnce, MIRROR_ONCE);

#undef CASE
    }
}

D3D12_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    default:
        // TODO: need to report failures
        return D3D12_COMPARISON_FUNC_ALWAYS;

#define CASE(FROM, TO)                                                                                                 \
    case ComparisonFunc::FROM:                                                                                         \
        return D3D12_COMPARISON_FUNC_##TO

        CASE(Never, NEVER);
        CASE(Less, LESS);
        CASE(Equal, EQUAL);
        CASE(LessEqual, LESS_EQUAL);
        CASE(Greater, GREATER);
        CASE(NotEqual, NOT_EQUAL);
        CASE(GreaterEqual, GREATER_EQUAL);
        CASE(Always, ALWAYS);
#undef CASE
    }
}

Result initTextureDesc(D3D12_RESOURCE_DESC& resourceDesc, const TextureDesc& srcDesc)
{
    const DXGI_FORMAT pixelFormat = D3DUtil::getMapFormat(srcDesc.format);
    if (pixelFormat == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    const D3D12_RESOURCE_DIMENSION dimension = calcResourceDimension(srcDesc.type);
    if (dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    resourceDesc.Dimension = dimension;
    resourceDesc.Format = pixelFormat;
    resourceDesc.Width = srcDesc.size.width;
    resourceDesc.Height = srcDesc.size.height;
    if (srcDesc.type == TextureType::Texture3D)
    {
        resourceDesc.DepthOrArraySize = srcDesc.size.depth;
    }
    else
    {
        resourceDesc.DepthOrArraySize = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);
    }

    resourceDesc.MipLevels = srcDesc.mipLevelCount;
    resourceDesc.SampleDesc.Count = srcDesc.sampleCount;
    resourceDesc.SampleDesc.Quality = srcDesc.sampleQuality;

    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    resourceDesc.Flags |= calcResourceFlags(srcDesc.usage);

    resourceDesc.Alignment = 0;

    if (isDepthFormat(srcDesc.format) &&
        (is_set(srcDesc.usage, TextureUsage::ShaderResource) || is_set(srcDesc.usage, TextureUsage::UnorderedAccess)))
    {
        resourceDesc.Format = getTypelessFormatFromDepthFormat(srcDesc.format);
    }

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
    }
    break;
    case slang::BindingType::MutableRawBuffer:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        d3dDevice->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, destDescriptor);
    }
    break;
    case slang::BindingType::MutableTypedBuffer:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d3dDevice->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, destDescriptor);
    }
    break;
    case slang::BindingType::RawBuffer:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
    }
    break;
    case slang::BindingType::TypedBuffer:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, destDescriptor);
    }
    break;
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
    }
    break;
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
    }
    break;
    default:
        break;
    }
    return SLANG_OK;
}

void translatePostBuildInfoDescs(
    uint32_t propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs,
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
    case CooperativeVectorComponentType::SInt8:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8;
    case CooperativeVectorComponentType::SInt16:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT16;
    case CooperativeVectorComponentType::SInt32:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT32;
    case CooperativeVectorComponentType::SInt64:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT64;
    case CooperativeVectorComponentType::UInt8:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8;
    case CooperativeVectorComponentType::UInt16:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT16;
    case CooperativeVectorComponentType::UInt32:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT32;
    case CooperativeVectorComponentType::UInt64:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT64;
    case CooperativeVectorComponentType::SInt8Packed:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8_PACKED;
    case CooperativeVectorComponentType::UInt8Packed:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8_PACKED;
    case CooperativeVectorComponentType::FloatE4M3:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E4M3;
    case CooperativeVectorComponentType::FloatE5M2:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E5M2;
    default:
        return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE(0);
    }
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
        return CooperativeVectorComponentType::SInt8;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT16:
        return CooperativeVectorComponentType::SInt16;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT32:
        return CooperativeVectorComponentType::SInt32;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT64:
        return CooperativeVectorComponentType::SInt64;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8:
        return CooperativeVectorComponentType::UInt8;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT16:
        return CooperativeVectorComponentType::UInt16;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT32:
        return CooperativeVectorComponentType::UInt32;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT64:
        return CooperativeVectorComponentType::UInt64;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8_PACKED:
        return CooperativeVectorComponentType::SInt8Packed;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8_PACKED:
        return CooperativeVectorComponentType::UInt8Packed;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E4M3:
        return CooperativeVectorComponentType::FloatE4M3;
    case NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E5M2:
        return CooperativeVectorComponentType::FloatE5M2;
    default:
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
    default:
        return NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT(0);
    }
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
        return CooperativeVectorMatrixLayout(0);
    }
}

NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC translateConvertCooperativeVectorMatrixDesc(
    const ConvertCooperativeVectorMatrixDesc& desc,
    bool isDevice
)
{
    NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC nvDesc = {};
    nvDesc.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER1;
    nvDesc.srcSize = desc.srcSize;
    nvDesc.srcData.deviceAddress = desc.srcData.deviceAddress;
    nvDesc.srcData.bIsDeviceAlloc = isDevice;
    nvDesc.pDstSize = desc.dstSize;
    nvDesc.dstData.deviceAddress = desc.dstData.deviceAddress;
    nvDesc.dstData.bIsDeviceAlloc = isDevice;
    nvDesc.srcComponentType = translateCooperativeVectorComponentType(desc.srcComponentType);
    nvDesc.dstComponentType = translateCooperativeVectorComponentType(desc.dstComponentType);
    nvDesc.numRows = desc.rowCount;
    nvDesc.numColumns = desc.colCount;
    nvDesc.srcLayout = translateCooperativeVectorMatrixLayout(desc.srcLayout);
    nvDesc.srcStride = desc.srcStride;
    nvDesc.dstLayout = translateCooperativeVectorMatrixLayout(desc.dstLayout);
    nvDesc.dstStride = desc.dstStride;
    return nvDesc;
}

#endif // SLANG_RHI_ENABLE_NVAPI

} // namespace rhi::d3d12

namespace rhi {

Result SLANG_MCALL getD3D12Adapters(std::vector<AdapterInfo>& outAdapters)
{
    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(D3DUtil::findAdapters(DeviceCheckFlag::UseHardwareDevice, nullptr, dxgiAdapters));

    outAdapters.clear();
    for (const auto& dxgiAdapter : dxgiAdapters)
    {
        DXGI_ADAPTER_DESC desc;
        dxgiAdapter->GetDesc(&desc);
        AdapterInfo info = {};
        auto name = string::from_wstring(desc.Description);
        memcpy(info.name, name.data(), min(name.length(), sizeof(AdapterInfo::name) - 1));
        info.vendorID = desc.VendorId;
        info.deviceID = desc.DeviceId;
        info.luid = D3DUtil::getAdapterLUID(dxgiAdapter);
        outAdapters.push_back(info);
    }
    return SLANG_OK;
}

Result SLANG_MCALL createD3D12Device(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<d3d12::DeviceImpl> result = new d3d12::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

void SLANG_MCALL enableD3D12DebugLayerIfAvailable()
{
    SharedLibraryHandle d3dModule;
#if SLANG_WINDOWS_FAMILY
    const char* libName = "d3d12";
#else
    const char* libName = "libvkd3d-proton-d3d12.so";
#endif
    if (SLANG_FAILED(loadSharedLibrary(libName, d3dModule)))
        return;
    PFN_D3D12_GET_DEBUG_INTERFACE d3d12GetDebugInterface =
        (PFN_D3D12_GET_DEBUG_INTERFACE)findSymbolAddressByName(d3dModule, "D3D12GetDebugInterface");
    ComPtr<ID3D12Debug> dxDebug;
    if (d3d12GetDebugInterface && SLANG_SUCCEEDED(d3d12GetDebugInterface(IID_PPV_ARGS(dxDebug.writeRef()))))
        dxDebug->EnableDebugLayer();
}

} // namespace rhi
