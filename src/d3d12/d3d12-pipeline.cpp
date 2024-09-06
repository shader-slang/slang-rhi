#include "d3d12-pipeline.h"
#include "d3d12-device.h"
#include "d3d12-pipeline-state-stream.h"
#include "d3d12-shader-program.h"
#include "d3d12-vertex-layout.h"

#ifdef SLANG_RHI_NVAPI
#include "../nvapi/nvapi-include.h"
#endif

#include "../nvapi/nvapi-util.h"

#include "core/stable_vector.h"
#include "core/string.h"

#include <climits>

#include <string>

namespace rhi::d3d12 {

void PipelineImpl::init(const RenderPipelineDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Graphics;
    pipelineDesc.graphics = inDesc;
    initializeBase(pipelineDesc);
}

void PipelineImpl::init(const ComputePipelineDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Compute;
    pipelineDesc.compute = inDesc;
    initializeBase(pipelineDesc);
}

Result PipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RETURN_ON_FAIL(ensureAPIPipelineCreated());
    outHandle->type = NativeHandleType::D3D12PipelineState;
    outHandle->value = (uint64_t)(m_pipelineState.get());
    return SLANG_OK;
}

Result PipelineImpl::ensureAPIPipelineCreated()
{
    if (m_pipelineState)
        return SLANG_OK;

    auto programImpl = static_cast<ShaderProgramImpl*>(m_program.Ptr());
    if (programImpl->m_shaders.size() == 0)
    {
        SLANG_RETURN_ON_FAIL(programImpl->compileShaders(m_device));
    }
    if (desc.type == PipelineType::Graphics)
    {
        // Only actually create a D3D12 pipeline state if the pipeline is fully specialized.
        auto inputLayoutImpl = (InputLayoutImpl*)desc.graphics.inputLayout;

        // A helper to fill common fields between graphics and mesh pipeline descs
        const auto fillCommonGraphicsState = [&](auto& psoDesc)
        {
            psoDesc.pRootSignature = programImpl->m_rootObjectLayout->m_rootSignature;

            psoDesc.PrimitiveTopologyType = D3DUtil::getPrimitiveType(desc.graphics.primitiveType);

            const auto& framebufferLayout = desc.graphics.framebufferLayout;
            const int numRenderTargets = int(framebufferLayout.renderTargetCount);

            {
                if (framebufferLayout.depthStencil.format != Format::Unknown)
                {
                    psoDesc.DSVFormat = D3DUtil::getMapFormat(framebufferLayout.depthStencil.format);
                }
                else
                {
                    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
                }
                psoDesc.NumRenderTargets = numRenderTargets;
                for (Int i = 0; i < numRenderTargets; i++)
                {
                    psoDesc.RTVFormats[i] = D3DUtil::getMapFormat(framebufferLayout.renderTargets[0].format);
                }

                psoDesc.SampleDesc.Count = framebufferLayout.sampleCount;
                psoDesc.SampleDesc.Quality = 0;
                psoDesc.SampleMask = UINT_MAX;
            }

            {
                auto& rs = psoDesc.RasterizerState;
                rs.FillMode = D3DUtil::getFillMode(desc.graphics.rasterizer.fillMode);
                rs.CullMode = D3DUtil::getCullMode(desc.graphics.rasterizer.cullMode);
                rs.FrontCounterClockwise =
                    desc.graphics.rasterizer.frontFace == FrontFaceMode::CounterClockwise ? TRUE : FALSE;
                rs.DepthBias = desc.graphics.rasterizer.depthBias;
                rs.DepthBiasClamp = desc.graphics.rasterizer.depthBiasClamp;
                rs.SlopeScaledDepthBias = desc.graphics.rasterizer.slopeScaledDepthBias;
                rs.DepthClipEnable = desc.graphics.rasterizer.depthClipEnable ? TRUE : FALSE;
                rs.MultisampleEnable = desc.graphics.rasterizer.multisampleEnable ? TRUE : FALSE;
                rs.AntialiasedLineEnable = desc.graphics.rasterizer.antialiasedLineEnable ? TRUE : FALSE;
                rs.ForcedSampleCount = desc.graphics.rasterizer.forcedSampleCount;
                rs.ConservativeRaster = desc.graphics.rasterizer.enableConservativeRasterization
                                            ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
                                            : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            }

            {
                D3D12_BLEND_DESC& blend = psoDesc.BlendState;
                blend.IndependentBlendEnable = FALSE;
                blend.AlphaToCoverageEnable = desc.graphics.blend.alphaToCoverageEnable ? TRUE : FALSE;
                blend.RenderTarget[0].RenderTargetWriteMask = (uint8_t)RenderTargetWriteMask::EnableAll;
                for (GfxIndex i = 0; i < numRenderTargets; i++)
                {
                    auto& d3dDesc = blend.RenderTarget[i];
                    d3dDesc.BlendEnable = desc.graphics.blend.targets[i].enableBlend ? TRUE : FALSE;
                    d3dDesc.BlendOp = D3DUtil::getBlendOp(desc.graphics.blend.targets[i].color.op);
                    d3dDesc.BlendOpAlpha = D3DUtil::getBlendOp(desc.graphics.blend.targets[i].alpha.op);
                    d3dDesc.DestBlend = D3DUtil::getBlendFactor(desc.graphics.blend.targets[i].color.dstFactor);
                    d3dDesc.DestBlendAlpha = D3DUtil::getBlendFactor(desc.graphics.blend.targets[i].alpha.dstFactor);
                    d3dDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
                    d3dDesc.LogicOpEnable = FALSE;
                    d3dDesc.RenderTargetWriteMask = desc.graphics.blend.targets[i].writeMask;
                    d3dDesc.SrcBlend = D3DUtil::getBlendFactor(desc.graphics.blend.targets[i].color.srcFactor);
                    d3dDesc.SrcBlendAlpha = D3DUtil::getBlendFactor(desc.graphics.blend.targets[i].alpha.srcFactor);
                }
                for (GfxIndex i = 1; i < numRenderTargets; i++)
                {
                    if (memcmp(
                            &desc.graphics.blend.targets[i],
                            &desc.graphics.blend.targets[0],
                            sizeof(desc.graphics.blend.targets[0])
                        ) != 0)
                    {
                        blend.IndependentBlendEnable = TRUE;
                        break;
                    }
                }
                for (uint32_t i = (uint32_t)numRenderTargets; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
                {
                    blend.RenderTarget[i] = blend.RenderTarget[0];
                }
            }

            {
                auto& ds = psoDesc.DepthStencilState;

                ds.DepthEnable = desc.graphics.depthStencil.depthTestEnable;
                ds.DepthWriteMask = desc.graphics.depthStencil.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL
                                                                                : D3D12_DEPTH_WRITE_MASK_ZERO;
                ds.DepthFunc = D3DUtil::getComparisonFunc(desc.graphics.depthStencil.depthFunc);
                ds.StencilEnable = desc.graphics.depthStencil.stencilEnable;
                ds.StencilReadMask = (UINT8)desc.graphics.depthStencil.stencilReadMask;
                ds.StencilWriteMask = (UINT8)desc.graphics.depthStencil.stencilWriteMask;
                ds.FrontFace = D3DUtil::translateStencilOpDesc(desc.graphics.depthStencil.frontFace);
                ds.BackFace = D3DUtil::translateStencilOpDesc(desc.graphics.depthStencil.backFace);
            }

            psoDesc.PrimitiveTopologyType = D3DUtil::getPrimitiveType(desc.graphics.primitiveType);
        };

        if (m_program->isMeshShaderProgram())
        {
            D3DX12_MESH_SHADER_PIPELINE_STATE_DESC meshDesc = {};
            for (auto& shaderBin : programImpl->m_shaders)
            {
                switch (shaderBin.stage)
                {
                case SLANG_STAGE_FRAGMENT:
                    meshDesc.PS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_AMPLIFICATION:
                    meshDesc.AS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_MESH:
                    meshDesc.MS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                default:
                    getDebugCallback()->handleMessage(
                        DebugMessageType::Error,
                        DebugMessageSource::Layer,
                        "Unsupported shader stage."
                    );
                    return SLANG_E_NOT_AVAILABLE;
                }
            }
            fillCommonGraphicsState(meshDesc);
            if (m_device->m_pipelineCreationAPIDispatcher)
            {
                SLANG_RETURN_ON_FAIL(m_device->m_pipelineCreationAPIDispatcher->createMeshPipeline(
                    m_device,
                    programImpl->linkedProgram.get(),
                    &meshDesc,
                    (void**)m_pipelineState.writeRef()
                ));
            }
            else
            {
                CD3DX12_PIPELINE_STATE_STREAM2 meshStateStream{meshDesc};
                D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(meshStateStream), &meshStateStream};

                SLANG_RETURN_ON_FAIL(
                    m_device->m_device5->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pipelineState.writeRef()))
                );
            }
        }
        else
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc = {};
            for (auto& shaderBin : programImpl->m_shaders)
            {
                switch (shaderBin.stage)
                {
                case SLANG_STAGE_VERTEX:
                    graphicsDesc.VS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_FRAGMENT:
                    graphicsDesc.PS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_DOMAIN:
                    graphicsDesc.DS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_HULL:
                    graphicsDesc.HS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                case SLANG_STAGE_GEOMETRY:
                    graphicsDesc.GS = {shaderBin.code.data(), SIZE_T(shaderBin.code.size())};
                    break;
                default:
                    getDebugCallback()->handleMessage(
                        DebugMessageType::Error,
                        DebugMessageSource::Layer,
                        "Unsupported shader stage."
                    );
                    return SLANG_E_NOT_AVAILABLE;
                }
            }

            if (inputLayoutImpl)
            {
                graphicsDesc.InputLayout = {
                    inputLayoutImpl->m_elements.data(),
                    UINT(inputLayoutImpl->m_elements.size())
                };
            }

            fillCommonGraphicsState(graphicsDesc);

            if (m_device->m_pipelineCreationAPIDispatcher)
            {
                SLANG_RETURN_ON_FAIL(m_device->m_pipelineCreationAPIDispatcher->createRenderPipeline(
                    m_device,
                    programImpl->linkedProgram.get(),
                    &graphicsDesc,
                    (void**)m_pipelineState.writeRef()
                ));
            }
            else
            {
                SLANG_RETURN_ON_FAIL(m_device->m_device->CreateGraphicsPipelineState(
                    &graphicsDesc,
                    IID_PPV_ARGS(m_pipelineState.writeRef())
                ));
            }
        }
    }
    else
    {

        // Only actually create a D3D12 pipeline state if the pipeline is fully specialized.
        ComPtr<ID3D12PipelineState> pipelineState;
        if (!programImpl->isSpecializable())
        {
            // Describe and create the compute pipeline state object
            D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
            computeDesc.pRootSignature =
                desc.compute.d3d12RootSignatureOverride
                    ? static_cast<ID3D12RootSignature*>(desc.compute.d3d12RootSignatureOverride)
                    : programImpl->m_rootObjectLayout->m_rootSignature;
            computeDesc.CS = {programImpl->m_shaders[0].code.data(), SIZE_T(programImpl->m_shaders[0].code.size())};

#ifdef SLANG_RHI_NVAPI
            if (m_device->m_nvapi)
            {
                // Also fill the extension structure.
                // Use the same UAV slot index and register space that are declared in the shader.

                // For simplicities sake we just use u0
                NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC extensionDesc;
                extensionDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc.version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
                extensionDesc.uavSlot = 0;
                extensionDesc.registerSpace = 0;

                // Put the pointer to the extension into an array - there can be multiple extensions
                // enabled at once.
                const NVAPI_D3D12_PSO_EXTENSION_DESC* extensions[] = {&extensionDesc};

                // Now create the PSO.
                const NvAPI_Status nvapiStatus = NvAPI_D3D12_CreateComputePipelineState(
                    m_device->m_device,
                    &computeDesc,
                    SLANG_COUNT_OF(extensions),
                    extensions,
                    m_pipelineState.writeRef()
                );

                if (nvapiStatus != NVAPI_OK)
                {
                    return SLANG_FAIL;
                }
            }
            else
#endif
            {
                if (m_device->m_pipelineCreationAPIDispatcher)
                {
                    SLANG_RETURN_ON_FAIL(m_device->m_pipelineCreationAPIDispatcher->createComputePipeline(
                        m_device,
                        programImpl->linkedProgram.get(),
                        &computeDesc,
                        (void**)m_pipelineState.writeRef()
                    ));
                }
                else
                {
                    SLANG_RETURN_ON_FAIL(m_device->m_device->CreateComputePipelineState(
                        &computeDesc,
                        IID_PPV_ARGS(m_pipelineState.writeRef())
                    ));
                }
            }
        }
    }

    return SLANG_OK;
}

#if SLANG_RHI_DXR

RayTracingPipelineImpl::RayTracingPipelineImpl(DeviceImpl* device)
    : m_device(device)
{
}

void RayTracingPipelineImpl::init(const RayTracingPipelineDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::RayTracing;
    pipelineDesc.rayTracing.set(inDesc);
    initializeBase(pipelineDesc);
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RETURN_ON_FAIL(ensureAPIPipelineCreated());
    outHandle->type = NativeHandleType::D3D12StateObject;
    outHandle->value = (uint64_t)(m_stateObject.get());
    return SLANG_OK;
}

Result RayTracingPipelineImpl::ensureAPIPipelineCreated()
{
    if (m_stateObject)
        return SLANG_OK;

    auto program = static_cast<ShaderProgramImpl*>(m_program.Ptr());
    auto slangGlobalScope = program->linkedProgram;
    auto programLayout = slangGlobalScope->getLayout();

    std::vector<D3D12_STATE_SUBOBJECT> subObjects;
    stable_vector<D3D12_DXIL_LIBRARY_DESC> dxilLibraries;
    stable_vector<D3D12_HIT_GROUP_DESC> hitGroups;
    stable_vector<ComPtr<ISlangBlob>> codeBlobs;
    stable_vector<D3D12_EXPORT_DESC> exports;
    stable_vector<const wchar_t*> strPtrs;
    ComPtr<ISlangBlob> diagnostics;
    stable_vector<std::wstring> stringPool;
    auto getWStr = [&](const char* name)
    {
        stringPool.push_back(string::to_wstring(name));
        return stringPool.back().data();
    };

    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = desc.rayTracing.maxRecursion;
    if (desc.rayTracing.flags & RayTracingPipelineFlags::SkipTriangles)
        pipelineConfig.Flags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES;
    if (desc.rayTracing.flags & RayTracingPipelineFlags::SkipProcedurals)
        pipelineConfig.Flags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

    D3D12_STATE_SUBOBJECT pipelineConfigSubobject = {};
    pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1;
    pipelineConfigSubobject.pDesc = &pipelineConfig;
    subObjects.push_back(pipelineConfigSubobject);

    auto compileShader =
        [&](slang::EntryPointLayout* entryPointInfo, slang::IComponentType* component, SlangInt entryPointIndex)
    {
        ComPtr<ISlangBlob> codeBlob;
        auto compileResult = m_device->getEntryPointCodeFromShaderCache(
            component,
            entryPointIndex,
            0,
            codeBlob.writeRef(),
            diagnostics.writeRef()
        );
        if (diagnostics.get())
        {
            getDebugCallback()->handleMessage(
                compileResult == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
                DebugMessageSource::Slang,
                (char*)diagnostics->getBufferPointer()
            );
        }
        SLANG_RETURN_ON_FAIL(compileResult);
        codeBlobs.push_back(codeBlob);
        D3D12_DXIL_LIBRARY_DESC library = {};
        library.DXILLibrary.BytecodeLength = codeBlob->getBufferSize();
        library.DXILLibrary.pShaderBytecode = codeBlob->getBufferPointer();
        library.NumExports = 1;
        D3D12_EXPORT_DESC exportDesc = {};
        exportDesc.Name = getWStr(entryPointInfo->getNameOverride());
        exportDesc.ExportToRename = nullptr;
        exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        exports.push_back(exportDesc);
        library.pExports = &exports.back();

        D3D12_STATE_SUBOBJECT dxilSubObject = {};
        dxilSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        dxilLibraries.push_back(library);
        dxilSubObject.pDesc = &dxilLibraries.back();
        subObjects.push_back(dxilSubObject);
        return SLANG_OK;
    };
    if (program->linkedEntryPoints.empty())
    {
        for (SlangUInt i = 0; i < programLayout->getEntryPointCount(); i++)
        {
            SLANG_RETURN_ON_FAIL(
                compileShader(programLayout->getEntryPointByIndex(i), program->linkedProgram, (SlangInt)i)
            );
        }
    }
    else
    {
        for (auto& entryPoint : program->linkedEntryPoints)
        {
            SLANG_RETURN_ON_FAIL(compileShader(entryPoint->getLayout()->getEntryPointByIndex(0), entryPoint, 0));
        }
    }

    for (Index i = 0; i < desc.rayTracing.hitGroupDescs.size(); i++)
    {
        auto& hitGroup = desc.rayTracing.hitGroups[i];
        D3D12_HIT_GROUP_DESC hitGroupDesc = {};
        hitGroupDesc.Type = hitGroup.intersectionEntryPoint.empty() ? D3D12_HIT_GROUP_TYPE_TRIANGLES
                                                                    : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

        if (!hitGroup.anyHitEntryPoint.empty())
        {
            hitGroupDesc.AnyHitShaderImport = getWStr(hitGroup.anyHitEntryPoint.data());
        }
        if (!hitGroup.closestHitEntryPoint.empty())
        {
            hitGroupDesc.ClosestHitShaderImport = getWStr(hitGroup.closestHitEntryPoint.data());
        }
        if (!hitGroup.intersectionEntryPoint.empty())
        {
            hitGroupDesc.IntersectionShaderImport = getWStr(hitGroup.intersectionEntryPoint.data());
        }
        hitGroupDesc.HitGroupExport = getWStr(hitGroup.hitGroupName.data());

        D3D12_STATE_SUBOBJECT hitGroupSubObject = {};
        hitGroupSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitGroups.push_back(hitGroupDesc);
        hitGroupSubObject.pDesc = &hitGroups.back();
        subObjects.push_back(hitGroupSubObject);
    }

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    // According to DXR spec, fixed function triangle intersections must use float2 as ray
    // attributes that defines the barycentric coordinates at intersection.
    shaderConfig.MaxAttributeSizeInBytes = (UINT)desc.rayTracing.maxAttributeSizeInBytes;
    shaderConfig.MaxPayloadSizeInBytes = (UINT)desc.rayTracing.maxRayPayloadSize;
    D3D12_STATE_SUBOBJECT shaderConfigSubObject = {};
    shaderConfigSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigSubObject.pDesc = &shaderConfig;
    subObjects.push_back(shaderConfigSubObject);

    D3D12_GLOBAL_ROOT_SIGNATURE globalSignatureDesc = {};
    globalSignatureDesc.pGlobalRootSignature = program->m_rootObjectLayout->m_rootSignature.get();
    D3D12_STATE_SUBOBJECT globalSignatureSubobject = {};
    globalSignatureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    globalSignatureSubobject.pDesc = &globalSignatureDesc;
    subObjects.push_back(globalSignatureSubobject);

    if (m_device->m_pipelineCreationAPIDispatcher)
    {
        m_device->m_pipelineCreationAPIDispatcher->beforeCreateRayTracingState(m_device, slangGlobalScope);
    }

    D3D12_STATE_OBJECT_DESC rtpsoDesc = {};
    rtpsoDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpsoDesc.NumSubobjects = (UINT)subObjects.size();
    rtpsoDesc.pSubobjects = subObjects.data();
    SLANG_RETURN_ON_FAIL(m_device->m_device5->CreateStateObject(&rtpsoDesc, IID_PPV_ARGS(m_stateObject.writeRef())));

    if (m_device->m_pipelineCreationAPIDispatcher)
    {
        m_device->m_pipelineCreationAPIDispatcher->afterCreateRayTracingState(m_device, slangGlobalScope);
    }
    return SLANG_OK;
}

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
