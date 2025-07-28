#pragma once

#include "d3d12-base.h"

#include "core/common.h"
#include "core/short_vector.h"

#include <vector>

namespace rhi::d3d12 {

bool isSupportedNVAPIOp(ID3D12Device* dev, uint32_t op);

D3D12_RESOURCE_FLAGS calcResourceFlags(BufferUsage usage);
D3D12_RESOURCE_FLAGS calcResourceFlags(TextureUsage usage);
D3D12_RESOURCE_DIMENSION calcResourceDimension(TextureType type);

bool isTypelessDepthFormat(DXGI_FORMAT format);

D3D12_FILTER_TYPE translateFilterMode(TextureFilteringMode mode);
D3D12_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op);
D3D12_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode);
D3D12_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func);

Result initTextureDesc(D3D12_RESOURCE_DESC& resourceDesc, const TextureDesc& textureDesc, bool isTypeless);
void initBufferDesc(Size bufferSize, D3D12_RESOURCE_DESC& out);

Result createNullDescriptor(
    ID3D12Device* d3dDevice,
    D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor,
    slang::BindingType bindingType,
    SlangResourceShape resourceShape
);

void translatePostBuildInfoDescs(
    uint32_t propertyQueryCount,
    const AccelerationStructureQueryDesc* queryDescs,
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
