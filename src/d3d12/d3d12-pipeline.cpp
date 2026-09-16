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
#include "core/deferred.h"

#include <algorithm>
#include <climits>

#include <set>
#include <string>

namespace rhi::d3d12 {

#if SLANG_RHI_ENABLE_NVAPI
// NvAPI_D3D12_SetCreatePipelineStateOptions affects subsequent pipeline creations for
// a native device. Use a process-wide lock because multiple RHI devices can wrap the
// same ID3D12Device5.
SLANG_RHI_STATIC_MUTEX_BEGIN
static std::mutex s_nvapiRayTracingPipelineCreationMutex;
SLANG_RHI_STATIC_MUTEX_END
#endif

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
    size_t& outCacheSize,
    ComPtr<ISlangBlob>& outCacheKey
)
{
    outCached = false;
    outCacheSize = 0;
    outCacheKey = nullptr;

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
            outCacheKey = pipelineCacheKey;
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
            outCacheKey = pipelineCacheKey;
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
    ComPtr<ISlangBlob> cacheKey;
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
        if (!m_device5)
            return SLANG_E_NOT_AVAILABLE;

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
        SLANG_D3D_RETURN_ON_FAIL_REPORT(
            m_device5->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pipelineState.writeRef())),
            this
        );
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
            cacheSize,
            cacheKey
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
            cacheSize,
            cacheKey
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
    ComPtr<ISlangBlob> cacheKey;
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
        cacheSize,
        cacheKey
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
            cacheSize,
            cacheKey
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

namespace {

/// Describes the local-root data expected by one compiler-generated structural stage export.
struct StructuralStageRecordInfo
{
    /// Separates a non-void, possibly zero-sized Record from the void Record contract.
    bool hasStructuralRecord = false;
    Size size = 0;
    UINT shaderRegister = 0;
    UINT registerSpace = 0;

    bool operator==(const StructuralStageRecordInfo& other) const
    {
        return hasStructuralRecord == other.hasStructuralRecord && size == other.size &&
               shaderRegister == other.shaderRegister && registerSpace == other.registerSpace;
    }
};

/// The pair is `(shader register, register space)`. Sorting these pairs gives a stable native
/// local-root-argument order even when stages enter a hit group in a different order.
using StructuralRecordBinding = std::pair<UINT, UINT>;
using StructuralRecordBindings = std::vector<StructuralRecordBinding>;

/// Describes all local-root data expected by one shader-table export. A direct stage has at most
/// one binding, while a hit group contains the union required by every hit group connected to it
/// through a reused closest-hit, any-hit, or intersection export.
struct StructuralExportRecordInfo
{
    /// True when this export's own stages declare a non-void structural Record. Hit groups can
    /// still inherit `bindings` from the connected component when this is false.
    bool hasStructuralRecord = false;
    Size size = 0;
    StructuralRecordBindings bindings;

    bool operator==(const StructuralExportRecordInfo& other) const
    {
        return hasStructuralRecord == other.hasStructuralRecord && size == other.size && bindings == other.bindings;
    }
};

/// Creates the local root signature used to expose structural `Record` data at every cbuffer
/// binding referenced by an export. Each root parameter receives the same backing-record address
/// in the shader table; multiple parameters are needed only because separately compiled hit-group
/// stages can reserve different `(bN, spaceM)` bindings.
static Result createStructuralRecordLocalRootSignature(
    DeviceImpl* device,
    const StructuralRecordBindings& bindings,
    ID3D12RootSignature** outRootSignature
)
{
    SLANG_RHI_ASSERT(!bindings.empty());

    std::vector<D3D12_ROOT_PARAMETER1> rootParameters(bindings.size());
    for (size_t i = 0; i < bindings.size(); ++i)
    {
        auto& rootParameter = rootParameters[i];
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.ShaderRegister = bindings[i].first;
        rootParameter.Descriptor.RegisterSpace = bindings[i].second;
        rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.NumParameters = UINT(rootParameters.size());
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters.data();
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ComPtr<ID3DBlob> serializedSignature;
    ComPtr<ID3DBlob> errors;
    HRESULT serializeResult = device->m_D3D12SerializeVersionedRootSignature(
        &rootSignatureDesc,
        serializedSignature.writeRef(),
        errors.writeRef()
    );
    if (FAILED(serializeResult))
    {
        device->printError("Failed to serialize the D3D12 local root signature for structural shader-record data.\n");
        if (errors)
        {
            device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                static_cast<const char*>(errors->GetBufferPointer())
            );
        }
        return SLANG_FAIL;
    }

    SLANG_D3D_RETURN_ON_FAIL_REPORT(
        device->m_device->CreateRootSignature(
            0,
            serializedSignature->GetBufferPointer(),
            serializedSignature->GetBufferSize(),
            IID_PPV_ARGS(outRootSignature)
        ),
        device
    );
    return SLANG_OK;
}

/// Adds the structural Record contract reflected by a selected entry point when the Slang API
/// exposes that contract. The reflection API can be newer than the released Slang package used to
/// build slang-rhi, so the dependent requires expression keeps this source compatible with both
/// versions. An older compiler cannot produce the structural contract consumed by this backend and
/// therefore retains the legacy shader-table ABI.
template<typename EntryPointReflection, typename AddRecordInfo>
Result addSelectedEntryPointRecordInfo(
    DeviceImpl* device,
    EntryPointReflection* entryPoint,
    AddRecordInfo&& addRecordInfo
)
{
    if constexpr (requires(EntryPointReflection* value) {
                      value->getStructuralRayTracingRecordType();
                      value->getStructuralRayTracingRecordTypeLayout();
                      value->getStructuralRayTracingRecordBindingIndex();
                      value->getStructuralRayTracingRecordBindingSpace();
                  })
    {
        auto recordType = entryPoint->getStructuralRayTracingRecordType();
        if (!recordType)
            return SLANG_OK;
        const char* exportName = entryPoint->getNameOverride();
        if (!exportName)
        {
            device->printError("A selected structural ray-tracing stage has no native export name.\n");
            return SLANG_FAIL;
        }
        return addRecordInfo(
            exportName,
            recordType,
            entryPoint->getStructuralRayTracingRecordTypeLayout(),
            entryPoint->getStructuralRayTracingRecordBindingIndex(),
            entryPoint->getStructuralRayTracingRecordBindingSpace()
        );
    }
    else
    {
        return SLANG_OK;
    }
}

} // namespace

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
    stable_vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatureDescs;
    stable_vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> localRootAssociations;
    stable_vector<std::vector<const wchar_t*>> localRootAssociationExports;
    std::vector<size_t> localRootSubobjectIndices;
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
    if (is_set(desc.flags, RayTracingPipelineFlags::EnableOpacityMicromaps))
        pipelineConfig.Flags |= D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS;

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
        exportDesc.Name = getWStr(shader.entryPointName.c_str());
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

    // Structural stages receive `Record` through a compiler-reserved cbuffer register. D3D12
    // obtains a per-record CBV address from the selected shader-table entry, so associate that
    // local-root descriptor with the native shader-table export.
    std::vector<ComPtr<ID3D12RootSignature>> localRootSignatures;
    std::map<std::string, RayTracingPipelineImpl::StructuralRecordInfo> structuralRecordInfoByName;

    // A SingleProgram exposes all selected stages through `linkedProgram`. Separate entry-point
    // compilation instead leaves only the global scope there and puts each selected stage in its
    // own linked component. Retain every selected-entry layout so renamed exports keep their final
    // native identity and each separately compiled library uses its own reflected binding.
    std::vector<slang::ProgramLayout*> programLayouts;
    auto globalProgramLayout = program->linkedProgram->getLayout();
    SLANG_RHI_ASSERT(globalProgramLayout);
    programLayouts.push_back(globalProgramLayout);
    for (const auto& linkedEntryPoint : program->linkedEntryPoints)
    {
        auto entryPointLayout = linkedEntryPoint->getLayout();
        if (!entryPointLayout)
            return SLANG_FAIL;
        programLayouts.push_back(entryPointLayout);
    }

    // A root CBV has no size in its descriptor. D3D12 nevertheless limits shader accesses to 4096
    // 16-byte constants from the descriptor's base address.
    constexpr Size kMaximumStructuralRecordSize = Size(D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT) * 4 * sizeof(uint32_t);
    std::map<std::string, StructuralStageRecordInfo> stageRecordInfoByName;

    auto addRecordInfo = [&](const char* entryPointName,
                             slang::TypeReflection* recordType,
                             slang::TypeLayoutReflection* recordTypeLayout,
                             SlangInt bindingIndex,
                             SlangInt bindingSpace) -> Result
    {
        if (!recordType || !recordTypeLayout)
        {
            printError("Structural ray-tracing stage '%s' has no reflected Record type layout.\n", entryPointName);
            return SLANG_FAIL;
        }

        StructuralStageRecordInfo info = {};
        info.hasStructuralRecord = recordType->getScalarType() != slang::TypeReflection::ScalarType::Void;
        if (info.hasStructuralRecord)
        {
            if (bindingIndex < 0 || bindingSpace < 0)
            {
                printError("Structural ray-tracing stage '%s' has no D3D Record binding.\n", entryPointName);
                return SLANG_FAIL;
            }
            if (uint64_t(bindingIndex) > UINT_MAX || uint64_t(bindingSpace) > UINT_MAX)
            {
                printError(
                    "The D3D structural shader-record binding for '%s' does not fit in a D3D12 "
                    "register and space.\n",
                    entryPointName
                );
                return SLANG_E_INVALID_ARG;
            }
            info.shaderRegister = UINT(bindingIndex);
            info.registerSpace = UINT(bindingSpace);
            info.size = recordTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
            if (info.size == SLANG_UNKNOWN_SIZE || info.size == SLANG_UNBOUNDED_SIZE)
            {
                printError(
                    "Structural ray-tracing stage '%s' has a Record type without a fixed D3D layout.\n",
                    entryPointName
                );
                return SLANG_E_INVALID_ARG;
            }
            if (info.size > kMaximumStructuralRecordSize)
            {
                printError(
                    "Structural ray-tracing stage '%s' requires a %zu-byte Record, exceeding the D3D12 "
                    "root-CBV limit of %zu bytes.\n",
                    entryPointName,
                    info.size,
                    kMaximumStructuralRecordSize
                );
                return SLANG_E_INVALID_ARG;
            }
        }

        auto [it, inserted] = stageRecordInfoByName.emplace(entryPointName, info);
        if (!inserted && !(it->second == info))
        {
            printError("Structural ray-tracing export '%s' has conflicting Record contracts.\n", entryPointName);
            return SLANG_E_INVALID_ARG;
        }
        return SLANG_OK;
    };

    // Selected entry points expose the checked structural stage contract directly. This is the
    // authoritative path for standalone stages and for entry points renamed during component
    // composition, neither of which can be recovered reliably from an IHitGroup catalogue.
    for (auto programLayout : programLayouts)
    {
        for (SlangUInt i = 0; i < programLayout->getEntryPointCount(); ++i)
        {
            auto entryPoint = programLayout->getEntryPointByIndex(i);
            if (!entryPoint)
                continue;
            SLANG_RETURN_ON_FAIL(addSelectedEntryPointRecordInfo(this, entryPoint, addRecordInfo));
        }
    }

    // Exports with the same ordered binding set can share a local root signature. One root CBV is
    // one 64-bit local-root argument, independent of the application-data size behind its address.
    std::map<StructuralRecordBindings, std::set<std::string>> exportsByLocalRecordBindings;
    std::map<std::string, StructuralExportRecordInfo> structuralRecordContractByExportName;
    auto requireLocalRecord = [&](const std::string& exportName, const StructuralExportRecordInfo& info) -> Result
    {
        if (info.bindings.empty())
        {
            if (!info.hasStructuralRecord)
                return SLANG_OK;
            printError(
                "D3D12 shader-table export '%s' has structural Record data but no local-CBV binding.\n",
                exportName.c_str()
            );
            return SLANG_E_INVALID_ARG;
        }

        auto [contractIt, inserted] = structuralRecordContractByExportName.emplace(exportName, info);
        if (!inserted)
        {
            if (!(contractIt->second == info))
            {
                printError(
                    "D3D12 shader-table export '%s' has incompatible structural Record bindings or layouts.\n",
                    exportName.c_str()
                );
                return SLANG_E_INVALID_ARG;
            }
            return SLANG_OK;
        }

        // DXR local root signatures are exempt from the ordinary 64-DWORD root-signature limit.
        // Their argument footprint is instead bounded by the maximum native shader-record stride:
        // 4096 bytes minus the 32-byte shader identifier. A root CBV contributes one 64-bit GPU
        // address to that footprint.
        constexpr size_t kMaximumLocalRootCBVCount =
            (D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE - D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) /
            sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
        if (info.bindings.size() > kMaximumLocalRootCBVCount)
        {
            printError(
                "D3D12 shader-table export '%s' requires %zu structural Record CBVs; a local root "
                "signature supports at most %zu.\n",
                exportName.c_str(),
                info.bindings.size(),
                kMaximumLocalRootCBVCount
            );
            return SLANG_E_INVALID_ARG;
        }

        RayTracingPipelineImpl::StructuralRecordInfo pipelineInfo = {};
        pipelineInfo.hasStructuralRecord = info.hasStructuralRecord;
        pipelineInfo.dataSize = info.size;
        pipelineInfo.localRootCBVCount = uint32_t(info.bindings.size());
        structuralRecordInfoByName.emplace(exportName, pipelineInfo);
        exportsByLocalRecordBindings[info.bindings].insert(exportName);
        return SLANG_OK;
    };

    // Miss and callable shader identifiers select their stage exports directly, so associate
    // those exports with their local root signature. Hit shaders imported by a hit group are
    // associated through that group below. DXR requires a hit-group association and associations
    // on its component shaders to match exactly, so only orphan hit shaders receive a direct
    // association after every hit-group import has been collected.
    for (const ShaderBinary& shader : program->m_shaders)
    {
        if (shader.stage != SLANG_STAGE_MISS && shader.stage != SLANG_STAGE_CALLABLE)
            continue;
        auto it = stageRecordInfoByName.find(shader.entryPointName);
        if (it != stageRecordInfoByName.end() && it->second.hasStructuralRecord)
        {
            StructuralExportRecordInfo exportInfo = {};
            exportInfo.hasStructuralRecord = true;
            exportInfo.size = it->second.size;
            exportInfo.bindings.push_back({it->second.shaderRegister, it->second.registerSpace});
            SLANG_RETURN_ON_FAIL(requireLocalRecord(shader.entryPointName, exportInfo));
        }
    }

    std::vector<StructuralExportRecordInfo> hitGroupRecordInfos(desc.hitGroupCount);
    std::vector<uint32_t> hitGroupComponentParents(desc.hitGroupCount);
    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
        hitGroupComponentParents[i] = i;

    auto findHitGroupComponent = [&](uint32_t groupIndex)
    {
        uint32_t rootIndex = groupIndex;
        while (hitGroupComponentParents[rootIndex] != rootIndex)
            rootIndex = hitGroupComponentParents[rootIndex];

        while (hitGroupComponentParents[groupIndex] != groupIndex)
        {
            uint32_t parentIndex = hitGroupComponentParents[groupIndex];
            hitGroupComponentParents[groupIndex] = rootIndex;
            groupIndex = parentIndex;
        }
        return rootIndex;
    };

    auto joinHitGroupComponents = [&](uint32_t leftGroupIndex, uint32_t rightGroupIndex)
    {
        uint32_t leftRootIndex = findHitGroupComponent(leftGroupIndex);
        uint32_t rightRootIndex = findHitGroupComponent(rightGroupIndex);
        if (leftRootIndex == rightRootIndex)
            return;

        // Always retain the lower group index so the representative does not depend on which
        // reused stage caused two existing components to be joined.
        if (leftRootIndex > rightRootIndex)
            std::swap(leftRootIndex, rightRootIndex);
        hitGroupComponentParents[rightRootIndex] = leftRootIndex;
    };

    std::map<std::string, uint32_t> firstHitGroupByStageExport;
    std::set<std::string> importedHitStageExports;
    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
    {
        const auto& hitGroup = desc.hitGroups[i];
        auto& hitGroupRecordInfo = hitGroupRecordInfos[i];
        auto mergeStageRecordInfo = [&](const char* stageName) -> Result
        {
            if (!stageName)
                return SLANG_OK;

            importedHitStageExports.insert(stageName);
            auto [groupIt, inserted] = firstHitGroupByStageExport.emplace(stageName, i);
            if (!inserted)
                joinHitGroupComponents(i, groupIt->second);

            auto it = stageRecordInfoByName.find(stageName);
            if (it == stageRecordInfoByName.end())
                return SLANG_OK;
            const auto& stageInfo = it->second;
            if (!stageInfo.hasStructuralRecord)
                return SLANG_OK;
            if (!hitGroupRecordInfo.hasStructuralRecord)
            {
                hitGroupRecordInfo.hasStructuralRecord = true;
                hitGroupRecordInfo.size = stageInfo.size;
            }
            else if (hitGroupRecordInfo.size != stageInfo.size)
            {
                printError(
                    "RayTracingPipelineDesc.hitGroups[%u] combines structural stages whose Record "
                    "layouts have different sizes.\n",
                    i
                );
                return SLANG_E_INVALID_ARG;
            }
            hitGroupRecordInfo.bindings.push_back({stageInfo.shaderRegister, stageInfo.registerSpace});
            return SLANG_OK;
        };

        SLANG_RETURN_ON_FAIL(mergeStageRecordInfo(hitGroup.closestHitEntryPoint));
        SLANG_RETURN_ON_FAIL(mergeStageRecordInfo(hitGroup.anyHitEntryPoint));
        SLANG_RETURN_ON_FAIL(mergeStageRecordInfo(hitGroup.intersectionEntryPoint));
        std::sort(hitGroupRecordInfo.bindings.begin(), hitGroupRecordInfo.bindings.end());
        hitGroupRecordInfo.bindings.erase(
            std::unique(hitGroupRecordInfo.bindings.begin(), hitGroupRecordInfo.bindings.end()),
            hitGroupRecordInfo.bindings.end()
        );
    }

    // A hit-group association also applies to every closest-hit, any-hit, and intersection export
    // imported by that group. When an export is reused by two groups, DXR therefore requires both
    // groups to have identical local root signatures. This requirement is transitive: if A shares
    // a stage with B and B shares another stage with C, all three groups need the union of every
    // structural Record binding in the connected component.
    std::vector<StructuralRecordBindings> hitGroupComponentBindings(desc.hitGroupCount);
    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
    {
        uint32_t componentIndex = findHitGroupComponent(i);
        auto& componentBindings = hitGroupComponentBindings[componentIndex];
        const auto& groupBindings = hitGroupRecordInfos[i].bindings;
        componentBindings.insert(componentBindings.end(), groupBindings.begin(), groupBindings.end());
    }
    for (auto& componentBindings : hitGroupComponentBindings)
    {
        std::sort(componentBindings.begin(), componentBindings.end());
        componentBindings.erase(
            std::unique(componentBindings.begin(), componentBindings.end()),
            componentBindings.end()
        );
    }

    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
    {
        const auto& hitGroup = desc.hitGroups[i];
        StructuralExportRecordInfo exportInfo = hitGroupRecordInfos[i];
        exportInfo.bindings = hitGroupComponentBindings[findHitGroupComponent(i)];
        if (!exportInfo.bindings.empty())
        {
            if (!hitGroup.hitGroupName)
            {
                printError(
                    "RayTracingPipelineDesc.hitGroups[%u] requires a name for its structural Record "
                    "contract.\n",
                    i
                );
                return SLANG_E_INVALID_ARG;
            }
            SLANG_RETURN_ON_FAIL(requireLocalRecord(hitGroup.hitGroupName, exportInfo));
        }
    }

    // A selected hit shader that is not imported by any hit group is not covered by a composed
    // hit-group association. Associate only those orphan exports directly; adding this association
    // to an imported shader would conflict with its component-wide hit-group signature.
    for (const ShaderBinary& shader : program->m_shaders)
    {
        if (shader.stage != SLANG_STAGE_CLOSEST_HIT && shader.stage != SLANG_STAGE_ANY_HIT &&
            shader.stage != SLANG_STAGE_INTERSECTION)
            continue;
        if (importedHitStageExports.find(shader.entryPointName) != importedHitStageExports.end())
            continue;
        auto it = stageRecordInfoByName.find(shader.entryPointName);
        if (it != stageRecordInfoByName.end() && it->second.hasStructuralRecord)
        {
            StructuralExportRecordInfo exportInfo = {};
            exportInfo.hasStructuralRecord = true;
            exportInfo.size = it->second.size;
            exportInfo.bindings.push_back({it->second.shaderRegister, it->second.registerSpace});
            SLANG_RETURN_ON_FAIL(requireLocalRecord(shader.entryPointName, exportInfo));
        }
    }

    for (const auto& [recordBindings, exportNames] : exportsByLocalRecordBindings)
    {
        ComPtr<ID3D12RootSignature> localRootSignature;
        SLANG_RETURN_ON_FAIL(
            createStructuralRecordLocalRootSignature(this, recordBindings, localRootSignature.writeRef())
        );
        localRootSignatures.push_back(localRootSignature);

        D3D12_LOCAL_ROOT_SIGNATURE localRootSignatureDesc = {};
        localRootSignatureDesc.pLocalRootSignature = localRootSignature.get();
        localRootSignatureDescs.push_back(localRootSignatureDesc);
        D3D12_STATE_SUBOBJECT localRootSignatureSubobject = {};
        localRootSignatureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        localRootSignatureSubobject.pDesc = &localRootSignatureDescs.back();
        localRootSubobjectIndices.push_back(subObjects.size());
        subObjects.push_back(localRootSignatureSubobject);

        localRootAssociationExports.emplace_back();
        auto& associationExports = localRootAssociationExports.back();
        associationExports.reserve(exportNames.size());
        for (const auto& exportName : exportNames)
            associationExports.push_back(getWStr(exportName.c_str()));

        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association = {};
        association.NumExports = UINT(associationExports.size());
        association.pExports = associationExports.data();
        localRootAssociations.push_back(association);
        D3D12_STATE_SUBOBJECT associationSubobject = {};
        associationSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        associationSubobject.pDesc = &localRootAssociations.back();
        subObjects.push_back(associationSubobject);
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

    // Associations point into the contiguous state-subobject array. Assign those pointers only
    // after every push_back so vector reallocation cannot invalidate them.
    for (size_t i = 0; i < localRootSubobjectIndices.size(); ++i)
    {
        localRootAssociations[i].pSubobjectToAssociate = &subObjects[localRootSubobjectIndices[i]];
    }

    D3D12_STATE_OBJECT_DESC rtpsoDesc = {};
    rtpsoDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpsoDesc.NumSubobjects = (UINT)subObjects.size();
    rtpsoDesc.pSubobjects = subObjects.data();

    auto createStateObject = [&]() -> Result
    {
        SLANG_D3D_RETURN_ON_FAIL_REPORT(
            m_device5->CreateStateObject(&rtpsoDesc, IID_PPV_ARGS(stateObject.writeRef())),
            this
        );
        return SLANG_OK;
    };

#if SLANG_RHI_ENABLE_NVAPI
    {
        std::lock_guard<std::mutex> lock(s_nvapiRayTracingPipelineCreationMutex);
        bool resetShaderExtensionSlot = false;
        bool resetPipelineStateOptions = false;
        SLANG_RHI_DEFERRED({
            if (resetPipelineStateOptions)
            {
                NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
                params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
                SLANG_RHI_NVAPI_CHECK(NvAPI_D3D12_SetCreatePipelineStateOptions(m_device5, &params));
            }
            if (resetShaderExtensionSlot)
            {
                SLANG_RHI_NVAPI_CHECK(NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(m_device, 0xffffffff, 0));
            }
        });

        if (m_nvapiShaderExtension)
        {
            resetShaderExtensionSlot = true;
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

                resetPipelineStateOptions = true;
                SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_SetCreatePipelineStateOptions(m_device5, &params));
            }
        }

        SLANG_RETURN_ON_FAIL(createStateObject());
    }
#else
    {
        SLANG_RETURN_ON_FAIL(createStateObject());
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    if (desc.label)
    {
        stateObject->SetName(string::to_wstring(desc.label).c_str());
    }

    // Query shader identifiers and cache them for later use in shader binding tables.
    // Only ray generation, miss, and callable shaders and hit groups have shader identifiers.
    std::map<std::string, void*> shaderIdentifierByName;
    {
        ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
        stateObject->QueryInterface(stateObjectProperties.writeRef());
        if (stateObjectProperties)
        {
            for (const ShaderBinary& shader : program->m_shaders)
            {
                if (shader.stage != SLANG_STAGE_RAY_GENERATION && shader.stage != SLANG_STAGE_MISS &&
                    shader.stage != SLANG_STAGE_CALLABLE)
                    continue;
                std::string name = shader.entryPointName;
                void* id = stateObjectProperties->GetShaderIdentifier(string::to_wstring(name).data());
                if (id)
                {
                    shaderIdentifierByName[name] = id;
                }
            }
            for (uint32_t i = 0; i < desc.hitGroupCount; i++)
            {
                if (desc.hitGroups[i].hitGroupName)
                {
                    std::string name = desc.hitGroups[i].hitGroupName;
                    void* id = stateObjectProperties->GetShaderIdentifier(string::to_wstring(name).data());
                    if (id)
                    {
                        shaderIdentifierByName[name] = id;
                    }
                }
            }
        }
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
            0,
            nullptr
        );
    }

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_stateObject = stateObject;
    pipeline->m_shaderIdentifierByName = std::move(shaderIdentifierByName);
    pipeline->m_localRootSignatures = std::move(localRootSignatures);
    pipeline->m_structuralRecordInfoByName = std::move(structuralRecordInfoByName);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::d3d12
