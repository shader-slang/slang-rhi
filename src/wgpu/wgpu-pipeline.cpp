#include "wgpu-pipeline.h"
#include "wgpu-device.h"
#include "wgpu-shader-program.h"
#include "wgpu-shader-object-layout.h"

namespace rhi::wgpu {

PipelineImpl::~PipelineImpl()
{
    if (m_renderPipeline)
    {
        m_device->m_ctx.api.wgpuRenderPipelineRelease(m_renderPipeline);
    }
    if (m_computePipeline)
    {
        m_device->m_ctx.api.wgpuComputePipelineRelease(m_computePipeline);
    }
}

void PipelineImpl::init(const RenderPipelineDesc& desc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Graphics;
    pipelineDesc.graphics = desc;
    initializeBase(pipelineDesc);
}

void PipelineImpl::init(const ComputePipelineDesc& desc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Compute;
    pipelineDesc.compute = desc;
    initializeBase(pipelineDesc);
}

void PipelineImpl::init(const RayTracingPipelineDesc& desc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::RayTracing;
    pipelineDesc.rayTracing = desc;
    initializeBase(pipelineDesc);
}

Result PipelineImpl::createRenderPipeline()
{
    WGPURenderPipelineDescriptor pipelineDesc = {};
    m_renderPipeline = m_device->m_ctx.api.wgpuDeviceCreateRenderPipeline(m_device->m_ctx.device, &pipelineDesc);
    return m_renderPipeline ? SLANG_OK : SLANG_FAIL;
}

Result PipelineImpl::createComputePipeline()
{
    ShaderProgramImpl* program = static_cast<ShaderProgramImpl*>(m_program.get());
    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.compute.module = program->m_modules[0].module;
    pipelineDesc.compute.entryPoint = program->m_modules[0].entryPointName.c_str();
    pipelineDesc.layout = program->m_rootObjectLayout->m_pipelineLayout;
    m_computePipeline = m_device->m_ctx.api.wgpuDeviceCreateComputePipeline(m_device->m_ctx.device, &pipelineDesc);
    return m_computePipeline ? SLANG_OK : SLANG_FAIL;
}

Result PipelineImpl::ensureAPIPipelineCreated()
{
    switch (desc.type)
    {
    case PipelineType::Compute:
        return m_computePipeline ? SLANG_OK : createComputePipeline();
    case PipelineType::Graphics:
        return m_renderPipeline ? SLANG_OK : createRenderPipeline();
    default:
        SLANG_RHI_UNREACHABLE("Unknown pipeline type.");
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

Result PipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    switch (desc.type)
    {
    case PipelineType::Compute:
        outHandle->type = NativeHandleType::WGPUComputePipeline;
        outHandle->value = (uint64_t)m_computePipeline;
        return SLANG_OK;
    case PipelineType::Graphics:
        outHandle->type = NativeHandleType::WGPURenderPipeline;
        outHandle->value = (uint64_t)m_renderPipeline;
        return SLANG_OK;
    }
    return SLANG_E_NOT_AVAILABLE;
}


Result DeviceImpl::createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline)
{
    RefPtr<PipelineImpl> pipeline = new PipelineImpl();
    pipeline->m_device = this;
    pipeline->init(desc);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline)
{
    RefPtr<PipelineImpl> pipeline = new PipelineImpl();
    pipeline->m_device = this;
    pipeline->init(desc);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

} // namespace rhi::wgpu
