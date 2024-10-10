#include "wgpu-pipeline.h"
#include "wgpu-device.h"
#include "wgpu-input-layout.h"
#include "wgpu-shader-program.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-util.h"

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
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(m_program.get());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.graphics.inputLayout);
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

    pipelineDesc.primitive.topology = translatePrimitiveTopology(desc.graphics.primitiveTopology);
    // TODO support strip topologies
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = translateFrontFace(desc.graphics.rasterizer.frontFace);
    pipelineDesc.primitive.cullMode = translateCullMode(desc.graphics.rasterizer.cullMode);
    pipelineDesc.primitive.unclippedDepth = !desc.graphics.rasterizer.depthClipEnable;

    WGPUDepthStencilState depthStencil = {};
    if (desc.graphics.depthStencil.format != Format::Unknown)
    {
        const DepthStencilState& depthStencilIn = desc.graphics.depthStencil;
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
        depthStencil.depthBias = desc.graphics.rasterizer.depthBias;
        depthStencil.depthBiasSlopeScale = desc.graphics.rasterizer.slopeScaledDepthBias;
        depthStencil.depthBiasClamp = desc.graphics.rasterizer.depthBiasClamp;
        pipelineDesc.depthStencil = &depthStencil;
    }

    pipelineDesc.multisample.count = desc.graphics.multisample.sampleCount;
    pipelineDesc.multisample.mask = desc.graphics.multisample.sampleMask;
    pipelineDesc.multisample.alphaToCoverageEnabled = desc.graphics.multisample.alphaToCoverageEnable;
    // desc.graphics.multisample.alphaToOneEnable not supported

    short_vector<WGPUColorTargetState, 8> targets(desc.graphics.targetCount, {});
    short_vector<WGPUBlendState, 8> blendStates(desc.graphics.targetCount, {});
    for (GfxIndex i = 0; i < desc.graphics.targetCount; ++i)
    {
        const ColorTargetState& targetIn = desc.graphics.targets[i];
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
    fragment.targetCount = (uint32_t)targets.size();
    fragment.targets = targets.data();
    pipelineDesc.fragment = &fragment;

    m_renderPipeline = m_device->m_ctx.api.wgpuDeviceCreateRenderPipeline(m_device->m_ctx.device, &pipelineDesc);

    return m_renderPipeline ? SLANG_OK : SLANG_FAIL;
}

Result PipelineImpl::createComputePipeline()
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(m_program.get());
    ShaderProgramImpl::Module* computeModule = program->findModule(SlangStage::SLANG_STAGE_COMPUTE);
    if (!computeModule)
    {
        return SLANG_FAIL;
    }

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = program->m_rootObjectLayout->m_pipelineLayout;
    pipelineDesc.compute.module = computeModule->module;
    pipelineDesc.compute.entryPoint = computeModule->entryPointName.c_str();

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
