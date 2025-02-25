#include "metal-pipeline.h"
#include "metal-device.h"
#include "metal-shader-object-layout.h"
#include "metal-shader-program.h"
#include "metal-util.h"
#include "metal-input-layout.h"

namespace rhi::metal {

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLRenderPipelineState;
    outHandle->value = (uint64_t)m_pipelineState.get();
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    AUTORELEASEPOOL

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);
    if (!program | !inputLayout)
        return SLANG_FAIL;

    NS::SharedPtr<MTL::RenderPipelineDescriptor> pd = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());

    for (const ShaderProgramImpl::Module& module : program->m_modules)
    {
        auto functionName = MetalUtil::createString(module.entryPointName.data());
        NS::SharedPtr<MTL::Function> function = NS::TransferPtr(module.library->newFunction(functionName.get()));
        if (!function)
            return SLANG_FAIL;

        switch (module.stage)
        {
        case SLANG_STAGE_VERTEX:
            pd->setVertexFunction(function.get());
            break;
        case SLANG_STAGE_FRAGMENT:
            pd->setFragmentFunction(function.get());
            break;
        default:
            return SLANG_FAIL;
        }
    }

    // Create a vertex descriptor with the vertex buffer binding indices being offset.
    // They need to be in a range not used by any buffers in the root object layout.
    // The +1 is to account for a potential constant buffer at index 0.
    NS::UInteger vertexBufferOffset = program->m_rootObjectLayout->getTotalBufferCount() + 1;
    NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor = inputLayout->createVertexDescriptor(vertexBufferOffset);
    pd->setVertexDescriptor(vertexDescriptor.get());
    pd->setInputPrimitiveTopology(MetalUtil::translatePrimitiveTopologyClass(desc.primitiveTopology));

    pd->setAlphaToCoverageEnabled(desc.multisample.alphaToCoverageEnable);
    // pd->setAlphaToOneEnabled(); // Currently not supported by rhi
    // pd->setRasterizationEnabled(true); // Enabled by default

    for (uint32_t i = 0; i < desc.targetCount; ++i)
    {
        const ColorTargetDesc& targetState = desc.targets[i];
        MTL::RenderPipelineColorAttachmentDescriptor* colorAttachment = pd->colorAttachments()->object(i);
        colorAttachment->setPixelFormat(MetalUtil::translatePixelFormat(targetState.format));

        colorAttachment->setBlendingEnabled(targetState.enableBlend);
        colorAttachment->setSourceRGBBlendFactor(MetalUtil::translateBlendFactor(targetState.color.srcFactor));
        colorAttachment->setDestinationRGBBlendFactor(MetalUtil::translateBlendFactor(targetState.color.dstFactor));
        colorAttachment->setRgbBlendOperation(MetalUtil::translateBlendOperation(targetState.color.op));
        colorAttachment->setSourceAlphaBlendFactor(MetalUtil::translateBlendFactor(targetState.alpha.srcFactor));
        colorAttachment->setDestinationAlphaBlendFactor(MetalUtil::translateBlendFactor(targetState.alpha.dstFactor));
        colorAttachment->setAlphaBlendOperation(MetalUtil::translateBlendOperation(targetState.alpha.op));
        colorAttachment->setWriteMask(MetalUtil::translateColorWriteMask(targetState.writeMask));
    }
    if (desc.depthStencil.format != Format::Unknown)
    {
        const DepthStencilDesc& depthStencil = desc.depthStencil;
        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(depthStencil.format);
        if (MetalUtil::isDepthFormat(pixelFormat))
        {
            pd->setDepthAttachmentPixelFormat(MetalUtil::translatePixelFormat(depthStencil.format));
        }
        if (MetalUtil::isStencilFormat(pixelFormat))
        {
            pd->setStencilAttachmentPixelFormat(MetalUtil::translatePixelFormat(depthStencil.format));
        }
    }

    pd->setRasterSampleCount(desc.multisample.sampleCount);

    NS::Error* error;
    NS::SharedPtr<MTL::RenderPipelineState> pipelineState =
        NS::TransferPtr(m_device->newRenderPipelineState(pd.get(), &error));
    if (!pipelineState)
    {
        if (error)
        {
            handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                error->localizedDescription()->utf8String()
            );
        }
        return SLANG_FAIL;
    }

    // Create depth stencil state
    auto createStencilDesc = [](const DepthStencilOpDesc& desc, uint32_t readMask, uint32_t writeMask
                             ) -> NS::SharedPtr<MTL::StencilDescriptor>
    {
        NS::SharedPtr<MTL::StencilDescriptor> stencilDesc = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
        stencilDesc->setStencilCompareFunction(MetalUtil::translateCompareFunction(desc.stencilFunc));
        stencilDesc->setStencilFailureOperation(MetalUtil::translateStencilOperation(desc.stencilFailOp));
        stencilDesc->setDepthFailureOperation(MetalUtil::translateStencilOperation(desc.stencilDepthFailOp));
        stencilDesc->setDepthStencilPassOperation(MetalUtil::translateStencilOperation(desc.stencilPassOp));
        stencilDesc->setReadMask(readMask);
        stencilDesc->setWriteMask(writeMask);
        return stencilDesc;
    };

    const auto& depthStencil = desc.depthStencil;
    NS::SharedPtr<MTL::DepthStencilDescriptor> depthStencilDesc =
        NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    NS::SharedPtr<MTL::DepthStencilState> depthStencilState =
        NS::TransferPtr(m_device->newDepthStencilState(depthStencilDesc.get()));
    if (!depthStencilState)
    {
        return SLANG_FAIL;
    }
    if (depthStencil.depthTestEnable)
    {
        depthStencilDesc->setDepthCompareFunction(MetalUtil::translateCompareFunction(depthStencil.depthFunc));
    }
    depthStencilDesc->setDepthWriteEnabled(depthStencil.depthWriteEnable);
    if (depthStencil.stencilEnable)
    {
        depthStencilDesc->setFrontFaceStencil(
            createStencilDesc(depthStencil.frontFace, depthStencil.stencilReadMask, depthStencil.stencilWriteMask).get()
        );
        depthStencilDesc->setBackFaceStencil(
            createStencilDesc(depthStencil.backFace, depthStencil.stencilReadMask, depthStencil.stencilWriteMask).get()
        );
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl();
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_depthStencilState = depthStencilState;
    pipeline->m_primitiveType = MetalUtil::translatePrimitiveType(desc.primitiveTopology);
    pipeline->m_rasterizerDesc = desc.rasterizer;
    pipeline->m_vertexBufferOffset = vertexBufferOffset;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLComputePipelineState;
    outHandle->value = (uint64_t)m_pipelineState.get();
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    AUTORELEASEPOOL

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    const ShaderProgramImpl::Module& module = program->m_modules[0];
    auto functionName = MetalUtil::createString(module.entryPointName.data());
    NS::SharedPtr<MTL::Function> function = NS::TransferPtr(module.library->newFunction(functionName.get()));
    if (!function)
        return SLANG_FAIL;

    NS::Error* error;
    NS::SharedPtr<MTL::ComputePipelineState> pipelineState =
        NS::TransferPtr(m_device->newComputePipelineState(function.get(), &error));
    if (!pipelineState)
    {
        if (error)
        {
            handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                error->localizedDescription()->utf8String()
            );
        }
        return SLANG_FAIL;
    }

    // Query thread group size for use during dispatch.
    SlangUInt threadGroupSize[3];
    program->linkedProgram->getLayout()->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, threadGroupSize);

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_threadGroupSize = MTL::Size(threadGroupSize[0], threadGroupSize[1], threadGroupSize[2]);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

} // namespace rhi::metal
