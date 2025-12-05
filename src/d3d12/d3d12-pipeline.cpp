#include "d3d12-pipeline.h"
#include "d3d12-device.h"
#include "d3d12-pipeline-state-stream.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-input-layout.h"
#include "d3d12-utils.h"

#include "core/stable_vector.h"
#include "core/string.h"
#include "core/sha1.h"

#include <climits>

#include <string>

namespace rhi::d3d12 {

void hashShader(SHA1& sha1, const D3D12_SHADER_BYTECODE& shaderBytecode)
{
    if (shaderBytecode.pShaderBytecode && shaderBytecode.BytecodeLength)
    {
        sha1.update(shaderBytecode.pShaderBytecode, shaderBytecode.BytecodeLength);
    }
}

template<typename T>
void hashValue(SHA1& sha1, const T& value)
{
    sha1.update(&value, sizeof(value));
}

void hashString(SHA1& sha1, const char* str)
{
    if (str)
    {
        sha1.update(str, strlen(str));
    }
}

inline void hashDevice(SHA1& sha1, DeviceImpl* device)
{
    const AdapterLUID& luid = device->getInfo().adapterLUID;
    sha1.update(luid.luid, sizeof(luid.luid));
}

inline void hashPipelineDesc(SHA1& sha1, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc)
{
    hashShader(sha1, desc->VS);
    hashShader(sha1, desc->PS);
    hashShader(sha1, desc->DS);
    hashShader(sha1, desc->HS);
    hashShader(sha1, desc->GS);
    for (uint32_t i = 0; i < desc->StreamOutput.NumEntries; ++i)
    {
        const D3D12_SO_DECLARATION_ENTRY& entry = desc->StreamOutput.pSODeclaration[i];
        hashValue(sha1, entry.Stream);
        hashString(sha1, entry.SemanticName);
        hashValue(sha1, entry.SemanticIndex);
        hashValue(sha1, entry.StartComponent);
        hashValue(sha1, entry.ComponentCount);
        hashValue(sha1, entry.OutputSlot);
    }
    for (uint32_t i = 0; i < desc->StreamOutput.NumStrides; ++i)
    {
        hashValue(sha1, desc->StreamOutput.pBufferStrides[i]);
    }
    hashValue(sha1, desc->StreamOutput.RasterizedStream);
    hashValue(sha1, desc->BlendState);
    hashValue(sha1, desc->SampleMask);
    hashValue(sha1, desc->RasterizerState);
    hashValue(sha1, desc->DepthStencilState);
    for (uint32_t i = 0; i < desc->InputLayout.NumElements; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC& element = desc->InputLayout.pInputElementDescs[i];
        hashString(sha1, element.SemanticName);
        hashValue(sha1, element.SemanticIndex);
        hashValue(sha1, element.Format);
        hashValue(sha1, element.InputSlot);
        hashValue(sha1, element.AlignedByteOffset);
        hashValue(sha1, element.InputSlotClass);
        hashValue(sha1, element.InstanceDataStepRate);
    }
    hashValue(sha1, desc->IBStripCutValue);
    hashValue(sha1, desc->PrimitiveTopologyType);
    hashValue(sha1, desc->NumRenderTargets);
    for (uint32_t i = 0; i < desc->NumRenderTargets; i++)
    {
        hashValue(sha1, desc->RTVFormats[i]);
    }
    hashValue(sha1, desc->DSVFormat);
    hashValue(sha1, desc->SampleDesc);
    hashValue(sha1, desc->NodeMask);
    hashValue(sha1, desc->Flags);
}

inline void hashPipelineDesc(SHA1& sha1, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc)
{
    hashShader(sha1, desc->CS);
    hashValue(sha1, desc->NodeMask);
    hashValue(sha1, desc->Flags);
}

inline void getPipelineCacheKey(
    DeviceImpl* device,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
    ISlangBlob** outBlob
)
{
    SHA1 sha1;
    hashDevice(sha1, device);
    hashPipelineDesc(sha1, desc);
    SHA1::Digest digest = sha1.getDigest();
    ComPtr<ISlangBlob> blob = OwnedBlob::create(digest.data(), digest.size());
    returnComPtr(outBlob, blob);
}

inline void getPipelineCacheKey(DeviceImpl* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ISlangBlob** outBlob)
{
    SHA1 sha1;
    hashDevice(sha1, device);
    hashPipelineDesc(sha1, desc);
    SHA1::Digest digest = sha1.getDigest();
    ComPtr<ISlangBlob> blob = OwnedBlob::create(digest.data(), digest.size());
    returnComPtr(outBlob, blob);
}

template<typename PipelineDesc, typename PipelineState>
Result createPipelineWithCache(
    DeviceImpl* device,
    PipelineDesc* desc,
    Result (*createPipelineFunc)(DeviceImpl* device, PipelineDesc* desc, PipelineState** outPipeline),
    PipelineState** outPipeline,
    bool& outCached,
    size_t& outCacheSize
)
{
    outCached = false;
    outCacheSize = 0;

    // Early out if cache is not enabled.
    if (!device->m_persistentPipelineCache)
    {
        return createPipelineFunc(device, desc, outPipeline);
    }

    bool writeCache = true;
    ComPtr<ISlangBlob> pipelineCacheKey;
    ComPtr<ISlangBlob> pipelineCacheData;
    PipelineState* pipeline = nullptr;

    // Create pipeline cache key.
    getPipelineCacheKey(device, desc, pipelineCacheKey.writeRef());

    // Query pipeline cache.
    if (SLANG_FAILED(device->m_persistentPipelineCache->queryCache(pipelineCacheKey, pipelineCacheData.writeRef())))
    {
        pipelineCacheData = nullptr;
    }

    // Try create pipeline from cache.
    if (pipelineCacheData)
    {
        desc->CachedPSO.pCachedBlob = pipelineCacheData->getBufferPointer();
        desc->CachedPSO.CachedBlobSizeInBytes = pipelineCacheData->getBufferSize();
        if (createPipelineFunc(device, desc, &pipeline) == SLANG_OK)
        {
            writeCache = false;
            outCached = true;
            outCacheSize = pipelineCacheData->getBufferSize();
        }
        else
        {
            desc->CachedPSO.pCachedBlob = nullptr;
            desc->CachedPSO.CachedBlobSizeInBytes = 0;
            pipeline = nullptr;
        }
    }

    // Create pipeline if not found in cache.
    if (!pipeline)
    {
        SLANG_RETURN_ON_FAIL(createPipelineFunc(device, desc, &pipeline));
    }

    // Write to the cache.
    if (writeCache)
    {
        ComPtr<ID3DBlob> cachedBlob;
        if (SLANG_SUCCEEDED(pipeline->GetCachedBlob(cachedBlob.writeRef())) && cachedBlob)
        {
            pipelineCacheData = UnownedBlob::create(cachedBlob->GetBufferPointer(), cachedBlob->GetBufferSize());
            device->m_persistentPipelineCache->writeCache(pipelineCacheKey, pipelineCacheData);
            outCacheSize = pipelineCacheData->getBufferSize();
        }
    }

    *outPipeline = pipeline;
    return SLANG_OK;
}


RenderPipelineImpl::RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device, desc)
{
}

Result RenderPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12PipelineState;
    outHandle->value = (uint64_t)(m_pipelineState.get());
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());
    InputLayoutImpl* inputLayout = checked_cast<InputLayoutImpl*>(desc.inputLayout);

    ComPtr<ID3D12PipelineState> pipelineState;
    bool cached = false;
    size_t cacheSize = 0;

    // A helper to fill common fields between graphics and mesh pipeline descs
    const auto fillCommonGraphicsState = [&](auto& psoDesc)
    {
        psoDesc.pRootSignature = program->m_rootObjectLayout->m_rootSignature;

        psoDesc.PrimitiveTopologyType = translatePrimitiveTopologyType(desc.primitiveTopology);

        uint32_t numRenderTargets = desc.targetCount;

        {
            if (desc.depthStencil.format != Format::Undefined)
            {
                psoDesc.DSVFormat = getFormatMapping(desc.depthStencil.format).rtvFormat;
            }
            else
            {
                psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            }
            psoDesc.NumRenderTargets = numRenderTargets;
            for (uint32_t i = 0; i < numRenderTargets; i++)
            {
                psoDesc.RTVFormats[i] = getFormatMapping(desc.targets[i].format).rtvFormat;
            }

            psoDesc.SampleDesc.Count = desc.multisample.sampleCount;
            psoDesc.SampleDesc.Quality = 0;
            psoDesc.SampleMask = desc.multisample.sampleMask;
        }

        {
            auto& rs = psoDesc.RasterizerState;
            rs.FillMode = translateFillMode(desc.rasterizer.fillMode);
            rs.CullMode = translateCullMode(desc.rasterizer.cullMode);
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
            blend.RenderTarget[0].RenderTargetWriteMask = (UINT8)RenderTargetWriteMask::All;
            for (uint32_t i = 0; i < numRenderTargets; i++)
            {
                auto& d3dDesc = blend.RenderTarget[i];
                d3dDesc.BlendEnable = desc.targets[i].enableBlend ? TRUE : FALSE;
                d3dDesc.BlendOp = translateBlendOp(desc.targets[i].color.op);
                d3dDesc.BlendOpAlpha = translateBlendOp(desc.targets[i].alpha.op);
                d3dDesc.DestBlend = translateBlendFactor(desc.targets[i].color.dstFactor);
                d3dDesc.DestBlendAlpha = translateBlendFactor(desc.targets[i].alpha.dstFactor);
                d3dDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
                d3dDesc.LogicOpEnable = FALSE;
                d3dDesc.RenderTargetWriteMask = (UINT8)desc.targets[i].writeMask;
                d3dDesc.SrcBlend = translateBlendFactor(desc.targets[i].color.srcFactor);
                d3dDesc.SrcBlendAlpha = translateBlendFactor(desc.targets[i].alpha.srcFactor);
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
            ds.DepthFunc = translateComparisonFunc(desc.depthStencil.depthFunc);
            ds.StencilEnable = desc.depthStencil.stencilEnable;
            ds.StencilReadMask = (UINT8)desc.depthStencil.stencilReadMask;
            ds.StencilWriteMask = (UINT8)desc.depthStencil.stencilWriteMask;
            ds.FrontFace = translateStencilOpDesc(desc.depthStencil.frontFace);
            ds.BackFace = translateStencilOpDesc(desc.depthStencil.backFace);
        }

        psoDesc.PrimitiveTopologyType = translatePrimitiveTopologyType(desc.primitiveTopology);
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
        CD3DX12_PIPELINE_STATE_STREAM2 meshStateStream{meshDesc};
        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(meshStateStream), &meshStateStream};
        SLANG_RETURN_ON_FAIL(m_device5->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pipelineState.writeRef())));
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

        Result result = createPipelineWithCache<D3D12_GRAPHICS_PIPELINE_STATE_DESC, ID3D12PipelineState>(
            this,
            &graphicsDesc,
            [](DeviceImpl* device,
               D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
               ID3D12PipelineState** outPipeline) -> Result
            {
#if SLANG_RHI_ENABLE_NVAPI
                if (device->m_nvapiShaderExtension)
                {
                    NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC extensionDesc;
                    extensionDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
                    extensionDesc.psoExtension = NV_PSO_SET_SHADER_EXTENSION_SLOT_AND_SPACE;
                    extensionDesc.version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
                    extensionDesc.uavSlot = device->m_nvapiShaderExtension.uavSlot;
                    extensionDesc.registerSpace = device->m_nvapiShaderExtension.registerSpace;

                    const NVAPI_D3D12_PSO_EXTENSION_DESC* extensions[] = {&extensionDesc};

                    NvAPI_Status status = NvAPI_D3D12_CreateGraphicsPipelineState(
                        device->m_device,
                        desc,
                        SLANG_COUNT_OF(extensions),
                        extensions,
                        outPipeline
                    );
                    return status == NVAPI_OK ? SLANG_OK : SLANG_FAIL;
                }
                else
#endif // SLANG_RHI_ENABLE_NVAPI
                {
                    HRESULT hr = device->m_device->CreateGraphicsPipelineState(desc, IID_PPV_ARGS(outPipeline));
                    return hr == S_OK ? SLANG_OK : SLANG_FAIL;
                }
            },
            pipelineState.writeRef(),
            cached,
            cacheSize
        );
        SLANG_RETURN_ON_FAIL(result);
    }

    if (desc.label)
    {
        pipelineState->SetName(string::to_wstring(desc.label).c_str());
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Render,
            startTime,
            Timer::now(),
            cached,
            cacheSize
        );
    }

    RefPtr<RenderPipelineImpl> pipeline = new RenderPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_inputLayout = inputLayout;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    pipeline->m_primitiveTopology = translatePrimitiveTopology(desc.primitiveTopology);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

ComputePipelineImpl::ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12PipelineState;
    outHandle->value = (uint64_t)(m_pipelineState.get());
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());

    // Describe and create the compute pipeline state object
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = desc.d3d12RootSignatureOverride
                                     ? static_cast<ID3D12RootSignature*>(desc.d3d12RootSignatureOverride)
                                     : program->m_rootObjectLayout->m_rootSignature;
    computeDesc.CS = {program->m_shaders[0].code.data(), SIZE_T(program->m_shaders[0].code.size())};

    ComPtr<ID3D12PipelineState> pipelineState;
    bool cached = false;
    size_t cacheSize = 0;
    Result result = createPipelineWithCache<D3D12_COMPUTE_PIPELINE_STATE_DESC, ID3D12PipelineState>(
        this,
        &computeDesc,
        [](DeviceImpl* device, D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** outPipeline) -> Result
        {
#if SLANG_RHI_ENABLE_NVAPI
            if (device->m_nvapiShaderExtension)
            {
                NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC extensionDesc;
                extensionDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc.psoExtension = NV_PSO_SET_SHADER_EXTENSION_SLOT_AND_SPACE;
                extensionDesc.version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
                extensionDesc.uavSlot = device->m_nvapiShaderExtension.uavSlot;
                extensionDesc.registerSpace = device->m_nvapiShaderExtension.registerSpace;

                const NVAPI_D3D12_PSO_EXTENSION_DESC* extensions[] = {&extensionDesc};

                NvAPI_Status status = NvAPI_D3D12_CreateComputePipelineState(
                    device->m_device,
                    desc,
                    SLANG_COUNT_OF(extensions),
                    extensions,
                    outPipeline
                );
                return status == NVAPI_OK ? SLANG_OK : SLANG_FAIL;
            }
            else
#endif // SLANG_RHI_ENABLE_NVAPI
            {
                HRESULT hr = device->m_device->CreateComputePipelineState(desc, IID_PPV_ARGS(outPipeline));
                return hr == S_OK ? SLANG_OK : SLANG_FAIL;
            }
        },
        pipelineState.writeRef(),
        cached,
        cacheSize
    );
    SLANG_RETURN_ON_FAIL(result);

    if (desc.label)
    {
        pipelineState->SetName(string::to_wstring(desc.label).c_str());
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Compute,
            startTime,
            Timer::now(),
            cached,
            cacheSize
        );
    }

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_pipelineState = pipelineState;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

RayTracingPipelineImpl::RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device, desc)
{
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

    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_shaders.empty());

    ComPtr<ID3D12StateObject> stateObject;

    std::vector<D3D12_STATE_SUBOBJECT> subObjects;
    stable_vector<D3D12_DXIL_LIBRARY_DESC> dxilLibraries;
    stable_vector<D3D12_HIT_GROUP_DESC> hitGroups;
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

    for (const ShaderBinary& shader : program->m_shaders)
    {
        D3D12_DXIL_LIBRARY_DESC library = {};
        library.DXILLibrary.BytecodeLength = shader.code.size();
        library.DXILLibrary.pShaderBytecode = shader.code.data();
        library.NumExports = 1;
        D3D12_EXPORT_DESC exportDesc = {};
        exportDesc.Name = getWStr(shader.entryPointInfo->getNameOverride());
        exportDesc.ExportToRename = nullptr;
        exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
        exports.push_back(exportDesc);
        library.pExports = &exports.back();

        D3D12_STATE_SUBOBJECT dxilSubObject = {};
        dxilSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        dxilLibraries.push_back(library);
        dxilSubObject.pDesc = &dxilLibraries.back();
        subObjects.push_back(dxilSubObject);
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

#if SLANG_RHI_ENABLE_NVAPI
    bool nvapiResetPipelineStateOptions = false;
    if (m_nvapiShaderExtension)
    {
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(
            m_device,
            m_nvapiShaderExtension.uavSlot,
            m_nvapiShaderExtension.registerSpace
        ));

        if (is_set(desc.flags, RayTracingPipelineFlags::EnableLinearSweptSpheres) ||
            is_set(desc.flags, RayTracingPipelineFlags::EnableSpheres) ||
            is_set(desc.flags, RayTracingPipelineFlags::EnableClusters))
        {
            NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
            params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;

            if (is_set(desc.flags, RayTracingPipelineFlags::EnableLinearSweptSpheres))
                params.flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_LSS_SUPPORT;
            if (is_set(desc.flags, RayTracingPipelineFlags::EnableSpheres))
                params.flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_SPHERE_SUPPORT;
            if (is_set(desc.flags, RayTracingPipelineFlags::EnableClusters))
                params.flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT;

            // TODO: This sets global state!
            // Need to revisit if createRayTracingPipeline2 can get called from multiple threads.
            SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetCreatePipelineStateOptions(m_device5, &params));
            nvapiResetPipelineStateOptions = true;
        }
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    D3D12_STATE_OBJECT_DESC rtpsoDesc = {};
    rtpsoDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpsoDesc.NumSubobjects = (UINT)subObjects.size();
    rtpsoDesc.pSubobjects = subObjects.data();
    SLANG_RETURN_ON_FAIL(m_device5->CreateStateObject(&rtpsoDesc, IID_PPV_ARGS(stateObject.writeRef())));

#if SLANG_RHI_ENABLE_NVAPI
    if (m_nvapiShaderExtension)
    {
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(m_device, 0xffffffff, 0));

        if (nvapiResetPipelineStateOptions)
        {
            NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
            params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;

            SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetCreatePipelineStateOptions(m_device5, &params));
        }
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    if (desc.label)
    {
        stateObject->SetName(string::to_wstring(desc.label).c_str());
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::RayTracing,
            startTime,
            Timer::now(),
            false,
            0
        );
    }

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_stateObject = stateObject;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::d3d12
