#include "cuda-shader-table.h"
#include "cuda-device.h"
#include "cuda-pipeline.h"
#include "cuda-utils.h"

#include <vector>

namespace rhi::cuda {

ShaderTableImpl::ShaderTableImpl(Device* device, const ShaderTableDesc& desc)
    : ShaderTable(device, desc)
{
}

ShaderTableImpl::~ShaderTableImpl() {}

optix::ShaderBindingTable* ShaderTableImpl::getShaderBindingTable(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto instanceIt = m_shaderBindingTables.find(pipeline);
    if (instanceIt != m_shaderBindingTables.end())
        return instanceIt->second.get();

    RefPtr<optix::ShaderBindingTable>& shaderBindingTable = m_shaderBindingTables[pipeline];

    Result result = getDevice<DeviceImpl>()->m_ctx.optixContext->createShaderBindingTable(
        this,
        pipeline->m_optixPipeline,
        shaderBindingTable.writeRef()
    );
    SLANG_RHI_ASSERT(SLANG_SUCCEEDED(result));

    return shaderBindingTable.get();
}

} // namespace rhi::cuda
