#include "d3d11-pipeline.h"
#include "d3d11-device.h"
#include "d3d11-shader-program.h"
#include "d3d11-input-layout.h"
#include "d3d11-utils.h"

#include "core/deferred.h"

namespace rhi::d3d11 {

RenderPipelineImpl::RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device, desc)
{
}

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

#if SLANG_RHI_ENABLE_NVAPI
    if (m_nvapiShaderExtension)
    {
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D11_SetNvShaderExtnSlot(m_device, m_nvapiShaderExtension.uavSlot));
        SLANG_RHI_DEFERRED({ SLANG_RHI_NVAPI_CHECK(NvAPI_D3D11_SetNvShaderExtnSlot(m_device, ~0)); });
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    ComPtr<ID3D11VertexShader> vertexShader;
    {
        auto module = program->findModule(SLANG_STAGE_VERTEX);
        if (!module)
            return SLANG_FAIL;
        SLANG_RETURN_ON_FAIL(m_device->CreateVertexShader(
            module->code->getBufferPointer(),
            module->code->getBufferSize(),
            nullptr,
            vertexShader.writeRef()
        ));
    }

    ComPtr<ID3D11PixelShader> pixelShader;
    {
        auto module = program->findModule(SLANG_STAGE_FRAGMENT);
        if (!module)
            return SLANG_FAIL;
        SLANG_RETURN_ON_FAIL(m_device->CreatePixelShader(
            module->code->getBufferPointer(),
            module->code->getBufferSize(),
            nullptr,
            pixelShader.writeRef()
        ));
    }

    ComPtr<ID3D11DepthStencilState> depthStencilState;
    {
        D3D11_DEPTH_STENCIL_DESC dsDesc;
        dsDesc.DepthEnable = desc.depthStencil.depthTestEnable;
        dsDesc.DepthWriteMask =
            desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc = translateComparisonFunc(desc.depthStencil.depthFunc);
        dsDesc.StencilEnable = desc.depthStencil.stencilEnable;
        dsDesc.StencilReadMask = desc.depthStencil.stencilReadMask;
        dsDesc.StencilWriteMask = desc.depthStencil.stencilWriteMask;

#define FACE(DST, SRC)                                                                                                 \
    dsDesc.DST.StencilFailOp = translateStencilOp(desc.depthStencil.SRC.stencilFailOp);                                \
    dsDesc.DST.StencilDepthFailOp = translateStencilOp(desc.depthStencil.SRC.stencilDepthFailOp);                      \
    dsDesc.DST.StencilPassOp = translateStencilOp(desc.depthStencil.SRC.stencilPassOp);                                \
    dsDesc.DST.StencilFunc = translateComparisonFunc(desc.depthStencil.SRC.stencilFunc);                               \
    /* end */

        FACE(FrontFace, frontFace);
        FACE(BackFace, backFace);

        SLANG_RETURN_ON_FAIL(m_device->CreateDepthStencilState(&dsDesc, depthStencilState.writeRef()));
    }

    ComPtr<ID3D11RasterizerState> rasterizerState;
    {
        D3D11_RASTERIZER_DESC rsDesc;
        rsDesc.FillMode = translateFillMode(desc.rasterizer.fillMode);
        rsDesc.CullMode = translateCullMode(desc.rasterizer.cullMode);
        rsDesc.FrontCounterClockwise = desc.rasterizer.frontFace == FrontFaceMode::Clockwise;
        rsDesc.DepthBias = desc.rasterizer.depthBias;
        rsDesc.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
        rsDesc.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
        rsDesc.DepthClipEnable = desc.rasterizer.depthClipEnable;
        rsDesc.ScissorEnable = desc.rasterizer.scissorEnable;
        rsDesc.MultisampleEnable = desc.rasterizer.multisampleEnable;
        rsDesc.AntialiasedLineEnable = desc.rasterizer.antialiasedLineEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateRasterizerState(&rsDesc, rasterizerState.writeRef()));
    }

    ComPtr<ID3D11BlendState> blendState;
    {
        D3D11_BLEND_DESC dstDesc = {};

        ColorTargetDesc defaultTargetState;

        static const uint32_t kMaxTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        int targetCount = desc.targetCount;
        if (targetCount > kMaxTargets)
            return SLANG_FAIL;

        for (uint32_t ii = 0; ii < kMaxTargets; ++ii)
        {
            const ColorTargetDesc* targetState = nullptr;
            if (ii < targetCount)
            {
                targetState = &desc.targets[ii];
            }
            else if (targetCount == 0)
            {
                targetState = &defaultTargetState;
            }
            else
            {
                targetState = &desc.targets[targetCount - 1];
            }

            auto& srcTarget = *targetState;
            auto& dstTarget = dstDesc.RenderTarget[ii];

            if (isBlendDisabled(srcTarget))
            {
                dstTarget.BlendEnable = false;
                dstTarget.BlendOp = D3D11_BLEND_OP_ADD;
                dstTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                dstTarget.SrcBlend = D3D11_BLEND_ONE;
                dstTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
                dstTarget.DestBlend = D3D11_BLEND_ZERO;
                dstTarget.DestBlendAlpha = D3D11_BLEND_ZERO;
            }
            else
            {
                dstTarget.BlendEnable = true;
                dstTarget.BlendOp = translateBlendOp(srcTarget.color.op);
                dstTarget.BlendOpAlpha = translateBlendOp(srcTarget.alpha.op);
                dstTarget.SrcBlend = translateBlendFactor(srcTarget.color.srcFactor);
                dstTarget.SrcBlendAlpha = translateBlendFactor(srcTarget.alpha.srcFactor);
                dstTarget.DestBlend = translateBlendFactor(srcTarget.color.dstFactor);
                dstTarget.DestBlendAlpha = translateBlendFactor(srcTarget.alpha.dstFactor);
            }

            dstTarget.RenderTargetWriteMask = (UINT8)srcTarget.writeMask;
        }

        dstDesc.IndependentBlendEnable = targetCount > 1;
        dstDesc.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateBlendState(&dstDesc, blendState.writeRef()));
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Render,
            startTime,
            Timer::now(),
            false,
            0
        );
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);
    pipeline->m_vertexShader = vertexShader;
    pipeline->m_pixelShader = pixelShader;
    pipeline->m_depthStencilState = depthStencilState;
    pipeline->m_rasterizerState = rasterizerState;
    pipeline->m_blendState = blendState;
    pipeline->m_rtvCount = desc.targetCount;
    pipeline->m_primitiveTopology = translatePrimitiveTopology(desc.primitiveTopology);
    pipeline->m_blendColor[0] = 0;
    pipeline->m_blendColor[1] = 0;
    pipeline->m_blendColor[2] = 0;
    pipeline->m_blendColor[3] = 0;
    pipeline->m_sampleMask = 0xFFFFFFFF;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

#if SLANG_RHI_ENABLE_NVAPI
    if (m_nvapiShaderExtension)
    {
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D11_SetNvShaderExtnSlot(m_device, m_nvapiShaderExtension.uavSlot));
        SLANG_RHI_DEFERRED({ SLANG_RHI_NVAPI_CHECK(NvAPI_D3D11_SetNvShaderExtnSlot(m_device, ~0)); });
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    ComPtr<ID3D11ComputeShader> computeShader;
    {
        auto module = program->findModule(SLANG_STAGE_COMPUTE);
        if (!module)
            return SLANG_FAIL;
        SLANG_RETURN_ON_FAIL(m_device->CreateComputeShader(
            module->code->getBufferPointer(),
            module->code->getBufferSize(),
            nullptr,
            computeShader.writeRef()
        ));
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Compute,
            startTime,
            Timer::now(),
            false,
            0
        );
    }

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_computeShader = computeShader;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::d3d11
