#include "metal-pipeline.h"
#include "metal-device.h"
#include "metal-shader-object-layout.h"
#include "metal-shader-program.h"
#include "metal-utils.h"
#include "metal-input-layout.h"

namespace rhi::metal {

RenderPipelineImpl::RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device, desc)
{
}

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLRenderPipelineState;
    outHandle->value = (uint64_t)m_pipelineState.get();
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    AUTORELEASEPOOL

    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);
    if (!program)
        return SLANG_FAIL;
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    NS::SharedPtr<MTL::RenderPipelineDescriptor> pd = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());

    for (const ShaderProgramImpl::Module& module : program->m_modules)
    {
        auto functionName = createString(module.entryPointName.data());
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
    if (inputLayout)
    {
        NS::SharedPtr<MTL::VertexDescriptor> vertexDescriptor;
        vertexDescriptor = inputLayout->createVertexDescriptor(vertexBufferOffset);
        pd->setVertexDescriptor(vertexDescriptor.get());
    }
    pd->setInputPrimitiveTopology(translatePrimitiveTopologyClass(desc.primitiveTopology));

    pd->setAlphaToCoverageEnabled(desc.multisample.alphaToCoverageEnable);
    // pd->setAlphaToOneEnabled(); // Currently not supported by rhi
    // pd->setRasterizationEnabled(true); // Enabled by default

    for (uint32_t i = 0; i < desc.targetCount; ++i)
    {
        const ColorTargetDesc& targetState = desc.targets[i];
        MTL::RenderPipelineColorAttachmentDescriptor* colorAttachment = pd->colorAttachments()->object(i);
        colorAttachment->setPixelFormat(translatePixelFormat(targetState.format));

        colorAttachment->setBlendingEnabled(targetState.enableBlend);
        colorAttachment->setSourceRGBBlendFactor(translateBlendFactor(targetState.color.srcFactor));
        colorAttachment->setDestinationRGBBlendFactor(translateBlendFactor(targetState.color.dstFactor));
        colorAttachment->setRgbBlendOperation(translateBlendOperation(targetState.color.op));
        colorAttachment->setSourceAlphaBlendFactor(translateBlendFactor(targetState.alpha.srcFactor));
        colorAttachment->setDestinationAlphaBlendFactor(translateBlendFactor(targetState.alpha.dstFactor));
        colorAttachment->setAlphaBlendOperation(translateBlendOperation(targetState.alpha.op));
        colorAttachment->setWriteMask(translateColorWriteMask(targetState.writeMask));
    }
    if (desc.depthStencil.format != Format::Undefined)
    {
        const DepthStencilDesc& depthStencil = desc.depthStencil;
        MTL::PixelFormat pixelFormat = translatePixelFormat(depthStencil.format);
        if (isDepthFormat(pixelFormat))
        {
            pd->setDepthAttachmentPixelFormat(translatePixelFormat(depthStencil.format));
        }
        if (isStencilFormat(pixelFormat))
        {
            pd->setStencilAttachmentPixelFormat(translatePixelFormat(depthStencil.format));
        }
    }

    pd->setRasterSampleCount(desc.multisample.sampleCount);

    if (desc.label)
    {
        pd->setLabel(createString(desc.label).get());
    }

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
    auto createStencilDesc = [](const DepthStencilOpDesc& desc,
                                uint32_t readMask,
                                uint32_t writeMask) -> NS::SharedPtr<MTL::StencilDescriptor>
    {
        NS::SharedPtr<MTL::StencilDescriptor> stencilDesc = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
        stencilDesc->setStencilCompareFunction(translateCompareFunction(desc.stencilFunc));
        stencilDesc->setStencilFailureOperation(translateStencilOperation(desc.stencilFailOp));
        stencilDesc->setDepthFailureOperation(translateStencilOperation(desc.stencilDepthFailOp));
        stencilDesc->setDepthStencilPassOperation(translateStencilOperation(desc.stencilPassOp));
        stencilDesc->setReadMask(readMask);
        stencilDesc->setWriteMask(writeMask);
        return stencilDesc;
    };

    const auto& depthStencil = desc.depthStencil;
    NS::SharedPtr<MTL::DepthStencilDescriptor> depthStencilDesc =
        NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    if (depthStencil.depthTestEnable)
    {
        depthStencilDesc->setDepthCompareFunction(translateCompareFunction(depthStencil.depthFunc));
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
    NS::SharedPtr<MTL::DepthStencilState> depthStencilState =
        NS::TransferPtr(m_device->newDepthStencilState(depthStencilDesc.get()));
    if (!depthStencilState)
    {
        return SLANG_FAIL;
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
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_depthStencilState = depthStencilState;
    pipeline->m_primitiveType = translatePrimitiveType(desc.primitiveTopology);
    pipeline->m_rasterizerDesc = desc.rasterizer;
    pipeline->m_vertexBufferOffset = vertexBufferOffset;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
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

    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    const ShaderProgramImpl::Module& module = program->m_modules[0];
    auto functionName = createString(module.entryPointName.data());
    NS::SharedPtr<MTL::Function> function = NS::TransferPtr(module.library->newFunction(functionName.get()));
    if (!function)
        return SLANG_FAIL;

    NS::SharedPtr<MTL::ComputePipelineDescriptor> pd = NS::TransferPtr(MTL::ComputePipelineDescriptor::alloc()->init());

    pd->setComputeFunction(function.get());

    if (desc.label)
    {
        pd->setLabel(createString(desc.label).get());
    }

    NS::Error* error;
    NS::SharedPtr<MTL::ComputePipelineState> pipelineState =
        NS::TransferPtr(m_device->newComputePipelineState(pd.get(), MTL::PipelineOptionNone, nullptr, &error));
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
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_threadGroupSize = MTL::Size(threadGroupSize[0], threadGroupSize[1], threadGroupSize[2]);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

RayTracingPipelineImpl::RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device, desc)
{
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
