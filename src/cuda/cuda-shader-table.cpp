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

ShaderTableImpl::Instance* ShaderTableImpl::getInstance(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto instanceIt = m_instances.find(pipeline);
    if (instanceIt != m_instances.end())
        return &instanceIt->second;

    Instance& instance = m_instances[pipeline];

    Result result = getDevice<DeviceImpl>()->m_ctx.optixContext->createShaderBindingTable(
        this,
        pipeline->m_optixPipeline,
        instance.optixShaderBindingTable.writeRef()
    );
    SLANG_RHI_ASSERT(SLANG_SUCCEEDED(result));

    return &instance;
}

} // namespace rhi::cuda
