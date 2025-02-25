#include "wgpu-pipeline.h"
#include "wgpu-device.h"
#include "wgpu-input-layout.h"
#include "wgpu-shader-program.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-util.h"

namespace rhi::wgpu {

RenderPipelineImpl::~RenderPipelineImpl()
{
    if (m_renderPipeline)
    {
        m_device->m_ctx.api.wgpuRenderPipelineRelease(m_renderPipeline);
    }
}

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPURenderPipeline;
    outHandle->value = (uint64_t)m_renderPipeline;
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);
    ShaderProgramImpl::Module* vertexModule = program->findModule(SlangStage::SLANG_STAGE_VERTEX);
    ShaderProgramImpl::Module* fragmentModule = program->findModule(SlangStage::SLANG_STAGE_FRAGMENT);
    if (!vertexModule || !fragmentModule)
    {
        return SLANG_FAIL;
    }

    WGPURenderPipelineDescriptor pipelineDesc = {};

    pipelineDesc.layout = program->m_rootObjectLayout->m_pipelineLayout;

    pipelineDesc.vertex.module = vertexModule->module;
    pipelineDesc.vertex.entryPoint = vertexModule->entryPointName.c_str();
    pipelineDesc.vertex.buffers = inputLayout->m_vertexBufferLayouts.data();
    pipelineDesc.vertex.bufferCount = (uint32_t)inputLayout->m_vertexBufferLayouts.size();

    pipelineDesc.primitive.topology = translatePrimitiveTopology(desc.primitiveTopology);
    // TODO support strip topologies
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = translateFrontFace(desc.rasterizer.frontFace);
    pipelineDesc.primitive.cullMode = translateCullMode(desc.rasterizer.cullMode);
    pipelineDesc.primitive.unclippedDepth = !desc.rasterizer.depthClipEnable;

    WGPUDepthStencilState depthStencil = {};
    if (desc.depthStencil.format != Format::Unknown)
    {
        const DepthStencilDesc& depthStencilIn = desc.depthStencil;
        depthStencil.format = translateTextureFormat(depthStencilIn.format);
        depthStencil.depthWriteEnabled =
            depthStencilIn.depthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = translateCompareFunction(depthStencilIn.depthFunc);
        depthStencil.stencilFront.compare = translateCompareFunction(depthStencilIn.frontFace.stencilFunc);
        depthStencil.stencilFront.failOp = translateStencilOp(depthStencilIn.frontFace.stencilFailOp);
        depthStencil.stencilFront.depthFailOp = translateStencilOp(depthStencilIn.frontFace.stencilDepthFailOp);
        depthStencil.stencilFront.passOp = translateStencilOp(depthStencilIn.frontFace.stencilPassOp);
        depthStencil.stencilBack.compare = translateCompareFunction(depthStencilIn.backFace.stencilFunc);
        depthStencil.stencilBack.failOp = translateStencilOp(depthStencilIn.backFace.stencilFailOp);
        depthStencil.stencilBack.depthFailOp = translateStencilOp(depthStencilIn.backFace.stencilDepthFailOp);
        depthStencil.stencilBack.passOp = translateStencilOp(depthStencilIn.backFace.stencilPassOp);
        depthStencil.stencilReadMask = depthStencilIn.stencilReadMask;
        depthStencil.stencilWriteMask = depthStencilIn.stencilWriteMask;
        depthStencil.depthBias = desc.rasterizer.depthBias;
        depthStencil.depthBiasSlopeScale = desc.rasterizer.slopeScaledDepthBias;
        depthStencil.depthBiasClamp = desc.rasterizer.depthBiasClamp;
        pipelineDesc.depthStencil = &depthStencil;
    }

    pipelineDesc.multisample.count = desc.multisample.sampleCount;
    pipelineDesc.multisample.mask = desc.multisample.sampleMask;
    pipelineDesc.multisample.alphaToCoverageEnabled = desc.multisample.alphaToCoverageEnable;
    // desc.multisample.alphaToOneEnable not supported

    short_vector<WGPUColorTargetState, 8> targets(desc.targetCount, {});
    short_vector<WGPUBlendState, 8> blendStates(desc.targetCount, {});
    for (uint32_t i = 0; i < desc.targetCount; ++i)
    {
        const ColorTargetDesc& targetIn = desc.targets[i];
        WGPUColorTargetState& target = targets[i];
        WGPUBlendState& blend = blendStates[i];
        target.format = translateTextureFormat(targetIn.format);
        if (targetIn.enableBlend)
        {
            blend.color.operation = translateBlendOperation(targetIn.color.op);
            blend.color.srcFactor = translateBlendFactor(targetIn.color.srcFactor);
            blend.color.dstFactor = translateBlendFactor(targetIn.color.dstFactor);
            blend.alpha.operation = translateBlendOperation(targetIn.alpha.op);
            blend.alpha.srcFactor = translateBlendFactor(targetIn.alpha.srcFactor);
            blend.alpha.dstFactor = translateBlendFactor(targetIn.alpha.dstFactor);
            target.blend = &blend;
        }
        target.writeMask = targetIn.writeMask;
    }

    WGPUFragmentState fragment = {};
    fragment.module = fragmentModule->module;
    fragment.entryPoint = fragmentModule->entryPointName.c_str();
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();
    pipelineDesc.fragment = &fragment;

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl();
    pipeline->m_device = this;
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_renderPipeline = m_ctx.api.wgpuDeviceCreateRenderPipeline(m_ctx.device, &pipelineDesc);
    if (!pipeline->m_renderPipeline)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::~ComputePipelineImpl()
{
    if (m_computePipeline)
    {
        m_device->m_ctx.api.wgpuComputePipelineRelease(m_computePipeline);
    }
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUComputePipeline;
    outHandle->value = (uint64_t)m_computePipeline;
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
    ShaderProgramImpl::Module* computeModule = program->findModule(SlangStage::SLANG_STAGE_COMPUTE);
    if (!computeModule)
    {
        return SLANG_FAIL;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = program->m_rootObjectLayout->m_pipelineLayout;
    pipelineDesc.compute.module = computeModule->module;
    pipelineDesc.compute.entryPoint = computeModule->entryPointName.c_str();

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_device = this;
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_computePipeline = m_ctx.api.wgpuDeviceCreateComputePipeline(m_ctx.device, &pipelineDesc);
    if (!pipeline->m_computePipeline)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::wgpu
