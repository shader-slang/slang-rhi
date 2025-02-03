#pragma once

#include "d3d12-base.h"

#include "core/common.h"
#include "core/short_vector.h"

#include <vector>

#ifndef __ID3D12GraphicsCommandList1_FWD_DEFINED__
// If can't find a definition of CommandList1, just use an empty definition
struct ID3D12GraphicsCommandList1
{};
#endif

namespace rhi::d3d12 {

bool isSupportedNVAPIOp(ID3D12Device* dev, uint32_t op);

D3D12_RESOURCE_FLAGS calcResourceFlags(BufferUsage usage);
D3D12_RESOURCE_FLAGS calcResourceFlags(TextureUsage usage);
D3D12_RESOURCE_DIMENSION calcResourceDimension(TextureType type);

DXGI_FORMAT getTypelessFormatFromDepthFormat(Format format);
bool isTypelessDepthFormat(DXGI_FORMAT format);

D3D12_FILTER_TYPE translateFilterMode(TextureFilteringMode mode);
D3D12_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op);
D3D12_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode);
D3D12_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func);

Result initTextureDesc(D3D12_RESOURCE_DESC& resourceDesc, const TextureDesc& srcDesc);
void initBufferDesc(Size bufferSize, D3D12_RESOURCE_DESC& out);

Result createNullDescriptor(
    ID3D12Device* d3dDevice,
    D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor,
    slang::BindingType bindingType,
    SlangResourceShape resourceShape
);

void translatePostBuildInfoDescs(
    uint32_t propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs,
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC>& postBuildInfoDescs
);

#if SLANG_RHI_ENABLE_NVAPI
NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE translateCooperativeVectorComponentType(CooperativeVectorComponentType type);
CooperativeVectorComponentType translateCooperativeVectorComponentType(NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE type);
NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT translateCooperativeVectorMatrixLayout(CooperativeVectorMatrixLayout layout);
CooperativeVectorMatrixLayout translateCooperativeVectorMatrixLayout(NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT layout);
NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC translateConvertCooperativeVectorMatrixDesc(
    const ConvertCooperativeVectorMatrixDesc& desc,
    bool isDevice
);
#endif // SLANG_RHI_ENABLE_NVAPI

} // namespace rhi::d3d12

namespace rhi {

Result SLANG_MCALL getD3D12Adapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL createD3D12Device(const DeviceDesc* desc, IDevice** outDevice);

void SLANG_MCALL enableD3D12DebugLayerIfAvailable();

} // namespace rhi
