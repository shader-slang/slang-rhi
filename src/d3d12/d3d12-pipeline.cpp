#include "d3d12-pipeline.h"
#include "d3d12-device.h"
#include "d3d12-pipeline-state-stream.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-input-layout.h"

#include "core/stable_vector.h"
#include "core/string.h"

#include <climits>

#include <string>

namespace rhi::d3d12 {

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12PipelineState;
    outHandle->value = (uint64_t)(m_pipelineState.get());
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);

    ComPtr<ID3D12PipelineState> pipelineState;

    // A helper to fill common fields between graphics and mesh pipeline descs
    const auto fillCommonGraphicsState = [&](auto& psoDesc)
    {
        psoDesc.pRootSignature = program->m_rootObjectLayout->m_rootSignature;

        psoDesc.PrimitiveTopologyType = D3DUtil::getPrimitiveTopologyType(desc.primitiveTopology);

        uint32_t numRenderTargets = desc.targetCount;

        {
            if (desc.depthStencil.format != Format::Unknown)
            {
                psoDesc.DSVFormat = D3DUtil::getMapFormat(desc.depthStencil.format);
            }
            else
            {
                psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            }
            psoDesc.NumRenderTargets = numRenderTargets;
            for (uint32_t i = 0; i < numRenderTargets; i++)
            {
                psoDesc.RTVFormats[i] = D3DUtil::getMapFormat(desc.targets[i].format);
            }

            psoDesc.SampleDesc.Count = desc.multisample.sampleCount;
            psoDesc.SampleDesc.Quality = 0;
            psoDesc.SampleMask = desc.multisample.sampleMask;
        }

        {
            auto& rs = psoDesc.RasterizerState;
            rs.FillMode = D3DUtil::getFillMode(desc.rasterizer.fillMode);
            rs.CullMode = D3DUtil::getCullMode(desc.rasterizer.cullMode);
            rs.FrontCounterClockwise = desc.rasterizer.frontFace == FrontFaceMode::CounterClockwise ? TRUE : FALSE;
            rs.DepthBias = desc.rasterizer.depthBias;
            rs.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
            rs.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
            rs.DepthClipEnable = desc.rasterizer.depthClipEnable ? TRUE : FALSE;
            rs.MultisampleEnable = desc.rasterizer.multisampleEnable ? TRUE : FALSE;
            rs.AntialiasedLineEnable = desc.rasterizer.antialiasedLineEnable ? TRUE : FALSE;
            rs.ForcedSampleCount = desc.rasterizer.forcedSampleCount;
            rs.ConservativeRaster = desc.rasterizer.enableConservativeRasterization
                                        ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
                                        : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        }

        {
            D3D12_BLEND_DESC& blend = psoDesc.BlendState;
            blend.IndependentBlendEnable = FALSE;
            blend.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable ? TRUE : FALSE;
            blend.RenderTarget[0].RenderTargetWriteMask = (uint8_t)RenderTargetWriteMask::EnableAll;
            for (uint32_t i = 0; i < numRenderTargets; i++)
            {
                auto& d3dDesc = blend.RenderTarget[i];
                d3dDesc.BlendEnable = desc.targets[i].enableBlend ? TRUE : FALSE;
                d3dDesc.BlendOp = D3DUtil::getBlendOp(desc.targets[i].color.op);
                d3dDesc.BlendOpAlpha = D3DUtil::getBlendOp(desc.targets[i].alpha.op);
                d3dDesc.DestBlend = D3DUtil::getBlendFactor(desc.targets[i].color.dstFactor);
                d3dDesc.DestBlendAlpha = D3DUtil::getBlendFactor(desc.targets[i].alpha.dstFactor);
                d3dDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
                d3dDesc.LogicOpEnable = FALSE;
                d3dDesc.RenderTargetWriteMask = desc.targets[i].writeMask;
                d3dDesc.SrcBlend = D3DUtil::getBlendFactor(desc.targets[i].color.srcFactor);
                d3dDesc.SrcBlendAlpha = D3DUtil::getBlendFactor(desc.targets[i].alpha.srcFactor);
            }
            auto equalBlendState = [](const ColorTargetDesc& a, const ColorTargetDesc& b)
            {
                return a.enableBlend == b.enableBlend && a.color.op == b.color.op &&
                       a.color.srcFactor == b.color.srcFactor && a.color.dstFactor == b.color.dstFactor &&
                       a.alpha.op == b.alpha.op && a.alpha.srcFactor == b.alpha.srcFactor &&
                       a.alpha.dstFactor == b.alpha.dstFactor && a.writeMask == b.writeMask;
            };
            for (uint32_t i = 1; i < numRenderTargets; i++)
            {
                if (!equalBlendState(desc.targets[i], desc.targets[0]))
                {
                    blend.IndependentBlendEnable = TRUE;
                    break;
                }
            }
            for (uint32_t i = numRenderTargets; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
                blend.RenderTarget[i] = blend.RenderTarget[0];
            }
        }

        {
            auto& ds = psoDesc.DepthStencilState;

            ds.DepthEnable = desc.depthStencil.depthTestEnable;
            ds.DepthWriteMask =
                desc.depthStencil.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3DUtil::getComparisonFunc(desc.depthStencil.depthFunc);
            ds.StencilEnable = desc.depthStencil.stencilEnable;
            ds.StencilReadMask = (UINT8)desc.depthStencil.stencilReadMask;
            ds.StencilWriteMask = (UINT8)desc.depthStencil.stencilWriteMask;
            ds.FrontFace = D3DUtil::translateStencilOpDesc(desc.depthStencil.frontFace);
            ds.BackFace = D3DUtil::translateStencilOpDesc(desc.depthStencil.backFace);
        }

        psoDesc.PrimitiveTopologyType = D3DUtil::getPrimitiveTopologyType(desc.primitiveTopology);
    };

    if (program->isMeshShaderProgram())
    {
        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC meshDesc = {};
        for (auto& shaderBin : program->m_shaders)
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
                handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "Unsupported shader stage.");
                return SLANG_E_NOT_AVAILABLE;
            }
        }
        fillCommonGraphicsState(meshDesc);
        if (m_pipelineCreationAPIDispatcher)
        {
            SLANG_RETURN_ON_FAIL(m_pipelineCreationAPIDispatcher->createMeshPipeline(
                this,
                program->linkedProgram.get(),
                &meshDesc,
                (void**)pipelineState.writeRef()
            ));
        }
        else
        {
            CD3DX12_PIPELINE_STATE_STREAM2 meshStateStream{meshDesc};
            D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(meshStateStream), &meshStateStream};

            SLANG_RETURN_ON_FAIL(m_device5->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pipelineState.writeRef())));
        }
    }
    else
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc = {};
        for (auto& shaderBin : program->m_shaders)
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
                handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "Unsupported shader stage.");
                return SLANG_E_NOT_AVAILABLE;
            }
        }

        if (inputLayout)
        {
            graphicsDesc.InputLayout = {inputLayout->m_elements.data(), UINT(inputLayout->m_elements.size())};
        }

        fillCommonGraphicsState(graphicsDesc);

        if (m_pipelineCreationAPIDispatcher)
        {
            SLANG_RETURN_ON_FAIL(m_pipelineCreationAPIDispatcher->createRenderPipeline(
                this,
                program->linkedProgram.get(),
                &graphicsDesc,
                (void**)pipelineState.writeRef()
            ));
        }
        else
        {
#if SLANG_RHI_ENABLE_NVAPI
            if (m_nvapiShaderExtension)
            {
                NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC extensionDesc;
                extensionDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc.psoExtension = NV_PSO_SET_SHADER_EXTENSION_SLOT_AND_SPACE;
                extensionDesc.version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
                extensionDesc.uavSlot = m_nvapiShaderExtension.uavSlot;
                extensionDesc.registerSpace = m_nvapiShaderExtension.registerSpace;

                const NVAPI_D3D12_PSO_EXTENSION_DESC* extensions[] = {&extensionDesc};

                // Now create the PSO.
                SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_CreateGraphicsPipelineState(
                    m_device,
                    &graphicsDesc,
                    SLANG_COUNT_OF(extensions),
                    extensions,
                    pipelineState.writeRef()
                ));
            }
            else
#endif // SLANG_RHI_ENABLE_NVAPI
            {
                SLANG_RETURN_ON_FAIL(
                    m_device->CreateGraphicsPipelineState(&graphicsDesc, IID_PPV_ARGS(pipelineState.writeRef()))
                );
            }
        }
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl();
    pipeline->m_program = program;
    pipeline->m_inputLayout = inputLayout;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_primitiveTopology = D3DUtil::getPrimitiveTopology(desc.primitiveTopology);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12PipelineState;
    outHandle->value = (uint64_t)(m_pipelineState.get());
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());

    ComPtr<ID3D12PipelineState> pipelineState;

    // Describe and create the compute pipeline state object
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = desc.d3d12RootSignatureOverride
                                     ? static_cast<ID3D12RootSignature*>(desc.d3d12RootSignatureOverride)
                                     : program->m_rootObjectLayout->m_rootSignature;
    computeDesc.CS = {program->m_shaders[0].code.data(), SIZE_T(program->m_shaders[0].code.size())};

    if (m_pipelineCreationAPIDispatcher)
    {
        SLANG_RETURN_ON_FAIL(m_pipelineCreationAPIDispatcher->createComputePipeline(
            this,
            program->linkedProgram.get(),
            &computeDesc,
            (void**)pipelineState.writeRef()
        ));
    }
    else
    {
#if SLANG_RHI_ENABLE_NVAPI
        if (m_nvapiShaderExtension)
        {
            NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC extensionDesc;
            extensionDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
            extensionDesc.psoExtension = NV_PSO_SET_SHADER_EXTENSION_SLOT_AND_SPACE;
            extensionDesc.version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
            extensionDesc.uavSlot = m_nvapiShaderExtension.uavSlot;
            extensionDesc.registerSpace = m_nvapiShaderExtension.registerSpace;

            const NVAPI_D3D12_PSO_EXTENSION_DESC* extensions[] = {&extensionDesc};

            // Now create the PSO.
            SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_CreateComputePipelineState(
                m_device,
                &computeDesc,
                SLANG_COUNT_OF(extensions),
                extensions,
                pipelineState.writeRef()
            ));
        }
        else
#endif // SLANG_RHI_ENABLE_NVAPI
        {
            SLANG_RETURN_ON_FAIL(
                m_device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.writeRef()))
            );
        }
    }

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12StateObject;
    outHandle->value = (uint64_t)(m_stateObject.get());
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    if (!m_device5)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

#if SLANG_RHI_DXR
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());

    ComPtr<ID3D12StateObject> stateObject;

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
    pipelineConfig.MaxTraceRecursionDepth = desc.maxRecursion;
    if (is_set(desc.flags, RayTracingPipelineFlags::SkipTriangles))
        pipelineConfig.Flags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES;
    if (is_set(desc.flags, RayTracingPipelineFlags::SkipProcedurals))
        pipelineConfig.Flags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

    D3D12_STATE_SUBOBJECT pipelineConfigSubobject = {};
    pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1;
    pipelineConfigSubobject.pDesc = &pipelineConfig;
    subObjects.push_back(pipelineConfigSubobject);

    auto compileShader =
        [&](slang::EntryPointLayout* entryPointInfo, slang::IComponentType* component, SlangInt entryPointIndex)
    {
        ComPtr<ISlangBlob> codeBlob;
        auto compileResult = getEntryPointCodeFromShaderCache(
            component,
            entryPointIndex,
            0,
            codeBlob.writeRef(),
            diagnostics.writeRef()
        );
        if (diagnostics.get())
        {
            handleMessage(
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

    for (uint32_t i = 0; i < desc.hitGroupCount; i++)
    {
        auto& hitGroup = desc.hitGroups[i];
        D3D12_HIT_GROUP_DESC hitGroupDesc = {};
        hitGroupDesc.Type = hitGroup.intersectionEntryPoint ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                                                            : D3D12_HIT_GROUP_TYPE_TRIANGLES;

        if (hitGroup.anyHitEntryPoint)
        {
            hitGroupDesc.AnyHitShaderImport = getWStr(hitGroup.anyHitEntryPoint);
        }
        if (hitGroup.closestHitEntryPoint)
        {
            hitGroupDesc.ClosestHitShaderImport = getWStr(hitGroup.closestHitEntryPoint);
        }
        if (hitGroup.intersectionEntryPoint)
        {
            hitGroupDesc.IntersectionShaderImport = getWStr(hitGroup.intersectionEntryPoint);
        }
        hitGroupDesc.HitGroupExport = getWStr(hitGroup.hitGroupName);

        D3D12_STATE_SUBOBJECT hitGroupSubObject = {};
        hitGroupSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitGroups.push_back(hitGroupDesc);
        hitGroupSubObject.pDesc = &hitGroups.back();
        subObjects.push_back(hitGroupSubObject);
    }

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    // According to DXR spec, fixed function triangle intersections must use float2 as ray
    // attributes that defines the barycentric coordinates at intersection.
    shaderConfig.MaxAttributeSizeInBytes = (UINT)desc.maxAttributeSizeInBytes;
    shaderConfig.MaxPayloadSizeInBytes = (UINT)desc.maxRayPayloadSize;
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

    if (m_pipelineCreationAPIDispatcher)
    {
        SLANG_RETURN_ON_FAIL(m_pipelineCreationAPIDispatcher->beforeCreateRayTracingState(this, slangGlobalScope));
    }
    else
    {
#if SLANG_RHI_ENABLE_NVAPI
        if (m_nvapiShaderExtension)
        {
            SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetNvShaderExtnSlotSpace(
                m_device,
                m_nvapiShaderExtension.uavSlot,
                m_nvapiShaderExtension.registerSpace
            ));
        }
#endif // SLANG_RHI_ENABLE_NVAPI
    }

    D3D12_STATE_OBJECT_DESC rtpsoDesc = {};
    rtpsoDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpsoDesc.NumSubobjects = (UINT)subObjects.size();
    rtpsoDesc.pSubobjects = subObjects.data();
    SLANG_RETURN_ON_FAIL(m_device5->CreateStateObject(&rtpsoDesc, IID_PPV_ARGS(stateObject.writeRef())));

    if (m_pipelineCreationAPIDispatcher)
    {
        SLANG_RETURN_ON_FAIL(m_pipelineCreationAPIDispatcher->afterCreateRayTracingState(this, slangGlobalScope));
    }
    else
    {
#if SLANG_RHI_ENABLE_NVAPI
        if (m_nvapiShaderExtension)
        {
            SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetNvShaderExtnSlotSpace(m_device, 0xffffffff, 0));
        }
#endif // SLANG_RHI_ENABLE_NVAPI
    }

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl();
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_stateObject = stateObject;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

} // namespace rhi::d3d12
