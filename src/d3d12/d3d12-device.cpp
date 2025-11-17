#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-fence.h"
#include "d3d12-utils.h"
#include "d3d12-pipeline.h"
#include "d3d12-query.h"
#include "d3d12-sampler.h"
#include "d3d12-shader-object.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-table.h"
#include "d3d12-surface.h"
#include "d3d12-input-layout.h"
#include "d3d12-acceleration-structure.h"

#include "cooperative-vector-utils.h"

#include "core/short_vector.h"
#include "core/string.h"

#ifdef SLANG_RHI_NV_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

#include <algorithm>

namespace rhi::d3d12 {

// List of validation messages that are filtered out by default.
static const D3D12_MESSAGE_ID kFilteredValidationMessages[] = {
    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
    D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
    D3D12_MESSAGE_ID_FENCE_ZERO_WAIT,
};

struct ShaderModelInfo
{
    D3D_SHADER_MODEL shaderModel;
    SlangCompileTarget compileTarget;
    const char* profileName;
    Feature feature;
    Capability capability;
};
// List of shader models. Do not change oldest to newest order.
static ShaderModelInfo kKnownShaderModels[] = {
#define SHADER_MODEL_INFO_DXBC(major, minor)                                                                           \
    {D3D_SHADER_MODEL_##major##_##minor,                                                                               \
     SLANG_DXBC,                                                                                                       \
     "sm_" #major "_" #minor,                                                                                          \
     Feature::SM_##major##_##minor,                                                                                    \
     Capability::_sm_##major##_##minor}
    SHADER_MODEL_INFO_DXBC(5, 1),
#undef SHADER_MODEL_INFO_DXBC
#define SHADER_MODEL_INFO_DXIL(major, minor)                                                                           \
    {(D3D_SHADER_MODEL)0x##major##minor,                                                                               \
     SLANG_DXIL,                                                                                                       \
     "sm_" #major "_" #minor,                                                                                          \
     Feature::SM_##major##_##minor,                                                                                    \
     Capability::_sm_##major##_##minor}
    SHADER_MODEL_INFO_DXIL(6, 0),
    SHADER_MODEL_INFO_DXIL(6, 1),
    SHADER_MODEL_INFO_DXIL(6, 2),
    SHADER_MODEL_INFO_DXIL(6, 3),
    SHADER_MODEL_INFO_DXIL(6, 4),
    SHADER_MODEL_INFO_DXIL(6, 5),
    SHADER_MODEL_INFO_DXIL(6, 6),
    SHADER_MODEL_INFO_DXIL(6, 7),
    SHADER_MODEL_INFO_DXIL(6, 8),
    SHADER_MODEL_INFO_DXIL(6, 9)
#undef SHADER_MODEL_INFO_DXIL
};

inline int getShaderModelFromProfileName(const char* name)
{
    if (!name)
    {
        return -1;
    }

    std::string_view nameStr(name);

    for (int i = 0; i < SLANG_COUNT_OF(kKnownShaderModels); ++i)
    {
        std::string_view versionStr(kKnownShaderModels[i].profileName + 3, 3);
        if (string::ends_with(nameStr, versionStr))
        {
            return kKnownShaderModels[i].shaderModel;
        }
    }
    return -1;
}

inline Result getAdaptersImpl(std::vector<AdapterImpl>& outAdapters)
{
    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(enumAdapters(dxgiAdapters));

    for (const auto& dxgiAdapter : dxgiAdapters)
    {
        AdapterInfo info = getAdapterInfo(dxgiAdapter);
        info.deviceType = DeviceType::D3D12;

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_dxgiAdapter = dxgiAdapter;
        outAdapters.push_back(adapter);
    }

    // Mark default adapter (prefer discrete if available).
    markDefaultAdapter(outAdapters);

    return SLANG_OK;
}

std::vector<AdapterImpl>& getAdapters()
{
    static std::vector<AdapterImpl> adapters;
    static Result initResult = getAdaptersImpl(adapters);
    SLANG_UNUSED(initResult);
    return adapters;
}

static void validationMessageCallback(
    D3D12_MESSAGE_CATEGORY Category,
    D3D12_MESSAGE_SEVERITY Severity,
    D3D12_MESSAGE_ID ID,
    LPCSTR pDescription,
    void* pContext
)
{
    for (size_t i = 0; i < SLANG_COUNT_OF(kFilteredValidationMessages); ++i)
        if (ID == kFilteredValidationMessages[i])
            return;

    DeviceImpl* device = static_cast<DeviceImpl*>(pContext);

    DebugMessageType type = DebugMessageType::Info;
    switch (Severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
        type = DebugMessageType::Error;
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        type = DebugMessageType::Warning;
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        type = DebugMessageType::Info;
        break;
    }

    device->m_debugCallback->handleMessage(type, DebugMessageSource::Driver, pDescription);
}

#if SLANG_RHI_ENABLE_NVAPI
// Raytracing validation callback
static void __stdcall raytracingValidationMessageCallback(
    void* userData,
    NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity,
    const char* messageCode,
    const char* message,
    const char* messageDetails
)
{
    DeviceImpl* device = static_cast<DeviceImpl*>(userData);
    DebugMessageType type = DebugMessageType::Info;
    switch (severity)
    {
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR:
        type = DebugMessageType::Error;
        break;
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING:
        type = DebugMessageType::Warning;
        break;
    default:
        return;
    }

    char msg[4096];
    int msgSize = snprintf(msg, sizeof(msg), "[%s] %s\n%s", messageCode, message, messageDetails);
    if (msgSize < 0)
        return;
    else if (msgSize >= int(sizeof(msg)))
        msg[sizeof(msg) - 1] = 0;

    device->m_debugCallback->handleMessage(type, DebugMessageSource::Driver, msg);
}
#endif // SLANG_RHI_ENABLE_NVAPI

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // Rather than statically link against D3D, we load it dynamically.
    SharedLibraryHandle d3dModule;
#if SLANG_WINDOWS_FAMILY
    const char* libName = "d3d12";
#else
    const char* libName = "libvkd3d-proton-d3d12.so";
#endif
    if (SLANG_FAILED(loadSharedLibrary(libName, d3dModule)))
    {
        printError("Failed to load '%s'\n", libName);
        return SLANG_FAIL;
    }

    // Process chained descs
    for (const DescStructHeader* header = static_cast<const DescStructHeader*>(desc.next); header;
         header = header->next)
    {
        switch (header->type)
        {
        case StructType::D3D12DeviceExtendedDesc:
            memcpy(static_cast<void*>(&m_extendedDesc), header, sizeof(m_extendedDesc));
            break;
        case StructType::D3D12ExperimentalFeaturesDesc:
            processExperimentalFeaturesDesc(d3dModule, header);
            break;
        default:
            break;
        }
    }

    // If Aftermath is enabled, we can't enable the D3D12 debug layer as well
    if (isDebugLayersEnabled() && !desc.enableAftermath)
    {
        m_D3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)loadProc(d3dModule, "D3D12GetDebugInterface");
        if (m_D3D12GetDebugInterface)
        {
            if (SLANG_SUCCEEDED(m_D3D12GetDebugInterface(IID_PPV_ARGS(m_dxDebug.writeRef()))))
            {
#if 0
                // Can enable for extra validation. NOTE! That d3d12 warns if you do....
                // D3D12 MESSAGE : Device Debug Layer Startup Options : GPU - Based Validation is enabled(disabled by default).
                // This results in new validation not possible during API calls on the CPU, by creating patched shaders that have validation
                // added directly to the shader. However, it can slow things down a lot, especially for applications with numerous
                // PSOs.Time to see the first render frame may take several minutes.
                // [INITIALIZATION MESSAGE #1016: CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS]

                ComPtr<ID3D12Debug1> debug1;
                if (SLANG_SUCCEEDED(m_dxDebug->QueryInterface(debug1.writeRef())))
                {
                    debug1->SetEnableGPUBasedValidation(true);
                }
#endif
            }
        }
    }

    // Get D3D12 entry points.
    {
        m_D3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)loadProc(d3dModule, "D3D12CreateDevice");
        if (!m_D3D12CreateDevice)
        {
            return SLANG_FAIL;
        }
        m_D3D12SerializeRootSignature =
            (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)loadProc(d3dModule, "D3D12SerializeRootSignature");
        if (!m_D3D12SerializeRootSignature)
        {
            return SLANG_FAIL;
        }

        m_D3D12SerializeVersionedRootSignature =
            (PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)loadProc(d3dModule, "D3D12SerializeVersionedRootSignature");
        if (!m_D3D12SerializeVersionedRootSignature)
        {
            return SLANG_FAIL;
        }
    }

    // Get PIX entry points.
    {
        HMODULE pixModule = LoadLibraryW(L"WinPixEventRuntime.dll");
        if (pixModule)
        {
            m_BeginEventOnCommandList =
                (PFN_BeginEventOnCommandList)GetProcAddress(pixModule, "PIXBeginEventOnCommandList");
            m_EndEventOnCommandList = (PFN_EndEventOnCommandList)GetProcAddress(pixModule, "PIXEndEventOnCommandList");
            m_SetMarkerOnCommandList =
                (PFN_SetMarkerOnCommandList)GetProcAddress(pixModule, "PIXSetMarkerOnCommandList");
        }
    }

    m_dxgiFactory = getDXGIFactory();
    AdapterImpl* adapter = nullptr;

    if (!desc.existingDeviceHandles.handles[0])
    {
        SLANG_RETURN_ON_FAIL(selectAdapter(this, getAdapters(), desc, adapter));
        m_dxgiAdapter = adapter->m_dxgiAdapter;

        const D3D_FEATURE_LEVEL featureLevels[] =
            {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
        for (auto featureLevel : featureLevels)
        {
            if (SUCCEEDED(m_D3D12CreateDevice(m_dxgiAdapter, featureLevel, IID_PPV_ARGS(m_device.writeRef()))))
            {
                break;
            }
        }
        if (!m_device)
        {
            return SLANG_FAIL;
        }
    }
    else
    {
        if (desc.existingDeviceHandles.handles[0].type != NativeHandleType::D3D12Device)
        {
            return SLANG_FAIL;
        }
        m_device = (ID3D12Device*)desc.existingDeviceHandles.handles[0].value;
        AdapterLUID luid = getAdapterLUID(m_device->GetAdapterLuid());
        auto it = std::find_if(
            getAdapters().begin(),
            getAdapters().end(),
            [&](const AdapterImpl& a)
            {
                return luid == a.m_info.luid;
            }
        );
        if (it == getAdapters().end())
        {
            return SLANG_FAIL;
        }
        adapter = &*it;
        m_dxgiAdapter = adapter->m_dxgiAdapter;
    }

    // Query for ID3D12Device5 interface.
    m_device->QueryInterface<ID3D12Device5>(m_device5.writeRef());

    if (m_dxDebug && isDebugLayersEnabled() && !desc.enableAftermath)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SLANG_SUCCEEDED(m_device->QueryInterface(infoQueue.writeRef())))
        {
            // Make break
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            if (m_extendedDesc.debugBreakOnD3D12Error)
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            }
            D3D12_INFO_QUEUE_FILTER f = {};
            f.DenyList.NumIDs = (UINT)SLANG_COUNT_OF(kFilteredValidationMessages);
            f.DenyList.pIDList = (D3D12_MESSAGE_ID*)kFilteredValidationMessages;
            infoQueue->AddStorageFilterEntries(&f);

            // Apparently there is a problem with sm 6.3 with spurious errors, with debug layer
            // enabled
            D3D12_FEATURE_DATA_SHADER_MODEL featureShaderModel;
            featureShaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_3;
            SLANG_SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_SHADER_MODEL,
                &featureShaderModel,
                sizeof(featureShaderModel)
            ));

            if (featureShaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_3)
            {
                // Filter out any messages that cause issues
                // TODO: Remove this when the debug layers work properly
                D3D12_MESSAGE_ID messageIds[] = {
                    // When the debug layer is enabled this error is triggered sometimes after a
                    // CopyDescriptorsSimple call The failed check validates that the source and
                    // destination ranges of the copy do not overlap. The check assumes descriptor
                    // handles are pointers to memory, but this is not always the case and the check
                    // fails (even though everything is okay).
                    D3D12_MESSAGE_ID_COPY_DESCRIPTORS_INVALID_RANGES,
                };

                // We filter INFO messages because they are way too many
                D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};

                D3D12_INFO_QUEUE_FILTER infoQueueFilter = {};
                infoQueueFilter.DenyList.NumSeverities = SLANG_COUNT_OF(severities);
                infoQueueFilter.DenyList.pSeverityList = severities;
                infoQueueFilter.DenyList.NumIDs = SLANG_COUNT_OF(messageIds);
                infoQueueFilter.DenyList.pIDList = messageIds;

                infoQueue->PushStorageFilter(&infoQueueFilter);
            }
        }
        ComPtr<ID3D12InfoQueue1> infoQueue1;
        if (SLANG_SUCCEEDED(m_device->QueryInterface(infoQueue1.writeRef())))
        {
            if (SLANG_FAILED(infoQueue1->RegisterMessageCallback(
                    validationMessageCallback,
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                    this,
                    &m_validationMessageCallbackCookie
                )))
            {
                printWarning("Failed to register D3D12 validation message callback.");
            }
        }
    }

#ifdef SLANG_RHI_NV_AFTERMATH
    if (desc.enableAftermath && adapter->isNVIDIA())
    {
        // Initialize Nsight Aftermath for this device.
        // This combination of flags is not necessarily appropraite for real world usage
        const uint32_t aftermathFlags =
            GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_CallStackCapturing |
            GFSDK_Aftermath_FeatureFlags_EnableResourceTracking | GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |
            GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;

        auto initResult = GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, m_device);

        if (initResult != GFSDK_Aftermath_Result_Success)
        {
            printWarning("Failed to initialize aftermath: %d\n", int(initResult));
        }
    }
#endif

    // Initialize descriptor heaps.
    {
        SLANG_RETURN_ON_FAIL(
            CPUDescriptorHeap::create(
                m_device,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                16 * 1024,
                m_cpuCbvSrvUavHeap.writeRef()
            )
        );
        SLANG_RETURN_ON_FAIL(
            CPUDescriptorHeap::create(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4 * 1024, m_cpuRtvHeap.writeRef())
        );
        SLANG_RETURN_ON_FAIL(
            CPUDescriptorHeap::create(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4 * 1024, m_cpuDsvHeap.writeRef())
        );
        SLANG_RETURN_ON_FAIL(
            CPUDescriptorHeap::create(
                m_device,
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                4 * 1024,
                m_cpuSamplerHeap.writeRef()
            )
        );

        SLANG_RETURN_ON_FAIL(
            GPUDescriptorHeap::create(
                m_device,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                1000000,
                16 * 1024,
                m_gpuCbvSrvUavHeap.writeRef()
            )
        );
        SLANG_RETURN_ON_FAIL(
            GPUDescriptorHeap::create(
                m_device,
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                2048,
                2048,
                m_gpuSamplerHeap.writeRef()
            )
        );
    }

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `DrawInstancedIndirect` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc;
        signatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &args;
        signatureDesc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(
            m_device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(drawIndirectCmdSignature.writeRef()))
        );
    }

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `DrawIndexedInstancedIndirect` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc;
        signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &args;
        signatureDesc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(m_device->CreateCommandSignature(
            &signatureDesc,
            nullptr,
            IID_PPV_ARGS(drawIndexedIndirectCmdSignature.writeRef())
        ));
    }

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `Dispatch` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc;
        signatureDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &args;
        signatureDesc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(m_device->CreateCommandSignature(
            &signatureDesc,
            nullptr,
            IID_PPV_ARGS(dispatchIndirectCmdSignature.writeRef())
        ));
    }

    // Initialize device info.
    {
        m_info.deviceType = DeviceType::D3D12;
        m_info.apiName = "D3D12";
    }

    // Query adapter name & LUID.
    {
        DXGI_ADAPTER_DESC adapterDesc;
        m_dxgiAdapter->GetDesc(&adapterDesc);
        m_adapterName = string::from_wstring(adapterDesc.Description);
        m_info.adapterName = m_adapterName.data();
        m_info.adapterLUID = getAdapterLUID(adapterDesc.AdapterLuid);
    }

    // Query device limits.
    {
        DeviceLimits limits = {};
        limits.maxBufferSize = 0x80000000ull; // Assume 2GB
        limits.maxTextureDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        limits.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        limits.maxTextureDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        limits.maxTextureDimensionCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
        limits.maxTextureLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;

        limits.maxVertexInputElements = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
        limits.maxVertexInputElementOffset = 256; // TODO
        limits.maxVertexStreams = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        limits.maxVertexStreamStride = D3D12_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES;

        limits.maxComputeThreadsPerGroup = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
        limits.maxComputeThreadGroupSize[0] = D3D12_CS_THREAD_GROUP_MAX_X;
        limits.maxComputeThreadGroupSize[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
        limits.maxComputeThreadGroupSize[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
        limits.maxComputeDispatchThreadGroups[0] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        limits.maxComputeDispatchThreadGroups[1] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        limits.maxComputeDispatchThreadGroups[2] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;

        limits.maxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        limits.maxViewportDimensions[0] = D3D12_VIEWPORT_BOUNDS_MAX;
        limits.maxViewportDimensions[1] = D3D12_VIEWPORT_BOUNDS_MAX;
        limits.maxFramebufferDimensions[0] = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        limits.maxFramebufferDimensions[1] = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        limits.maxFramebufferDimensions[2] = 1;

        limits.maxShaderVisibleSamplers = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

        m_info.limits = limits;
    }

    // Initialize features & capabilities.
    bool isSoftwareDevice = adapter->m_info.adapterType == AdapterType::Software;
    addFeature(isSoftwareDevice ? Feature::SoftwareDevice : Feature::HardwareDevice);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Surface);
    addFeature(Feature::PipelineCache);
    addFeature(Feature::Rasterization);
    addFeature(Feature::CustomBorderColor);
    addFeature(Feature::TimestampQuery);

    addCapability(Capability::hlsl);
    addCapability(Capability::vertex);
    addCapability(Capability::fragment);
    addCapability(Capability::compute);
    addCapability(Capability::geometry);

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModelData = {};

    {
        // CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL) can fail if the runtime/driver does not yet know the
        // specified highest shader model. Therefore we assemble a list of shader models to check and
        // walk it from highest to lowest to find the supported shader model.
        short_vector<D3D_SHADER_MODEL> shaderModels;
        if (m_extendedDesc.highestShaderModel != 0)
            shaderModels.push_back((D3D_SHADER_MODEL)m_extendedDesc.highestShaderModel);
        for (int i = SLANG_COUNT_OF(kKnownShaderModels) - 1; i >= 0; --i)
            shaderModels.push_back(kKnownShaderModels[i].shaderModel);
        for (D3D_SHADER_MODEL shaderModel : shaderModels)
        {
            shaderModelData.HighestShaderModel = shaderModel;
            if (SLANG_SUCCEEDED(
                    m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelData, sizeof(shaderModelData))
                ))
                break;
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        {
            // Check double precision support
            if (options.DoublePrecisionFloatShaderOps)
            {
                addFeature(Feature::Double);
            }

            // Check conservative-rasterization support
            auto conservativeRasterTier = options.ConservativeRasterizationTier;
            if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_3)
            {
                addFeature(Feature::ConservativeRasterization);
                addFeature(Feature::ConservativeRasterization1);
                addFeature(Feature::ConservativeRasterization2);
                addFeature(Feature::ConservativeRasterization3);
            }
            else if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_2)
            {
                addFeature(Feature::ConservativeRasterization1);
                addFeature(Feature::ConservativeRasterization2);
            }
            else if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_1)
            {
                addFeature(Feature::ConservativeRasterization1);
            }

            // Check rasterizer ordered views support
            if (options.ROVsSupported)
            {
                addFeature(Feature::RasterizerOrderedViews);
            }

            // Check for bindless resources support
            if (options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3 &&
                shaderModelData.HighestShaderModel >= D3D_SHADER_MODEL_6_6)
            {
                addFeature(Feature::Bindless);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options))))
        {
            // Check wave operations support
            if (options.WaveOps)
            {
                addFeature(Feature::WaveOps);
            }
            if (options.Int64ShaderOps)
            {
                addFeature(Feature::Int64);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options, sizeof(options))))
        {
            // Check programmable sample positions support
            switch (options.ProgrammableSamplePositionsTier)
            {
            case D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_1:
                addFeature(Feature::ProgrammableSamplePositions1);
                break;
            case D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_2:
                addFeature(Feature::ProgrammableSamplePositions1);
                addFeature(Feature::ProgrammableSamplePositions2);
                break;
            default:
                break;
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options))))
        {
            // Check barycentrics support
            if (options.BarycentricsSupported)
            {
                addFeature(Feature::Barycentrics);
            }
            // Check multi view support
            if (options.ViewInstancingTier >= D3D12_VIEW_INSTANCING_TIER_3)
            {
                addFeature(Feature::MultiView);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options))))
        {
            if (options.Native16BitShaderOpsSupported)
            {
                addFeature(Feature::Half);
                addFeature(Feature::Int16);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))))
        {
            // Check ray tracing support
            if (options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
            {
                addFeature(Feature::AccelerationStructure);
                addFeature(Feature::RayTracing);
                addCapability(Capability::_raygen);
                addCapability(Capability::_intersection);
                addCapability(Capability::_anyhit);
                addCapability(Capability::_closesthit);
                addCapability(Capability::_callable);
                addCapability(Capability::_miss);
            }
            // Check ray query support
            if (options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
            {
                addFeature(Feature::RayQuery);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))))
        {
            // Check fragment shading rate support
            if (options.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
            {
                addFeature(Feature::FragmentShadingRate);
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options;
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options))))
        {
            // Check mesh shader support
            if (options.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
            {
                addFeature(Feature::MeshShader);
                addCapability(Capability::_mesh);
                addCapability(Capability::_amplification);
            }
            // Check sampler feedback support
            if (options.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_1_0)
            {
                addFeature(Feature::SamplerFeedback);
            }
        }
    }

    // Initialize NVAPI
#if SLANG_RHI_ENABLE_NVAPI
    {
        if (adapter->isNVIDIA() && SLANG_SUCCEEDED(NVAPIUtil::initialize()))
        {
            m_nvapiEnabled = true;
            m_nvapiShaderExtension = NVAPIShaderExtension{desc.nvapiExtUavSlot, desc.nvapiExtRegisterSpace};
            if (m_nvapiShaderExtension)
            {
                addCapability(Capability::hlsl_nvapi);

                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_UINT64_ATOMIC))
                {
                    addFeature(Feature::AtomicInt64);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP16_ATOMIC))
                {
                    addFeature(Feature::AtomicHalf);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP32_ATOMIC))
                {
                    addFeature(Feature::AtomicFloat);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_GET_SPECIAL))
                {
                    addFeature(Feature::RealtimeClock);
                }
                NVAPI_D3D12_RAYTRACING_SPHERES_CAPS spheresCaps;
                if (NvAPI_D3D12_GetRaytracingCaps(
                        m_device,
                        NVAPI_D3D12_RAYTRACING_CAPS_TYPE_SPHERES,
                        &spheresCaps,
                        sizeof(spheresCaps)
                    ) == NVAPI_OK &&
                    spheresCaps == NVAPI_D3D12_RAYTRACING_SPHERES_CAP_STANDARD)
                {
                    addFeature(Feature::AccelerationStructureSpheres);
                }
                NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS lssCaps;
                if (NvAPI_D3D12_GetRaytracingCaps(
                        m_device,
                        NVAPI_D3D12_RAYTRACING_CAPS_TYPE_LINEAR_SWEPT_SPHERES,
                        &lssCaps,
                        sizeof(lssCaps)
                    ) == NVAPI_OK &&
                    lssCaps == NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_STANDARD)
                {
                    addFeature(Feature::AccelerationStructureLinearSweptSpheres);
                }
                NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAPS reorderingCaps;
                if (NvAPI_D3D12_GetRaytracingCaps(
                        m_device,
                        NVAPI_D3D12_RAYTRACING_CAPS_TYPE_THREAD_REORDERING,
                        &reorderingCaps,
                        sizeof(reorderingCaps)
                    ) == NVAPI_OK &&
                    reorderingCaps == NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD)
                {
                    addFeature(Feature::ShaderExecutionReordering);
                }

                // Check for cooperative vector support. NVAPI doesn't have a direct way to check for this,
                // so we query the number of cooperative vector properties to determine if it is supported.
                NvU32 propertyCount = 0;
                if (NvAPI_D3D12_GetPhysicalDeviceCooperativeVectorProperties(m_device, &propertyCount, nullptr) ==
                        NVAPI_OK &&
                    propertyCount > 0)
                {
                    // TODO: for now we don't report support because NVAPI doesn't provide a reliable way to detect
                    // hardware/driver support.
                    addFeature(Feature::CooperativeVector);
                }
            }
        }

        // Enable ray tracing validation if requested
        if (desc.enableRayTracingValidation)
        {
            if (NvAPI_D3D12_EnableRaytracingValidation(m_device5, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE) ==
                NVAPI_OK)
            {
                SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(
                    m_device5,
                    &raytracingValidationMessageCallback,
                    this,
                    &m_raytracingValidationHandle
                ));
            }
        }
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    // Initialize format support table
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        const FormatMapping& formatMapping = getFormatMapping(format);
        FormatSupport formatSupport = FormatSupport::None;

#define UPDATE_FLAGS(d3dFlags, formatSupportFlags)                                                                     \
    formatSupport |= (flags & d3dFlags) ? formatSupportFlags : FormatSupport::None;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT d3dFormatSupport = {formatMapping.srvFormat};
        if (SLANG_SUCCEEDED(
                m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &d3dFormatSupport, sizeof(d3dFormatSupport))
            ))
        {
            UINT flags = d3dFormatSupport.Support1;
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_BUFFER, FormatSupport::Buffer);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER, FormatSupport::VertexBuffer);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER, FormatSupport::IndexBuffer);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_TEXTURE1D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_TEXTURE2D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_TEXTURE3D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_TEXTURECUBE, FormatSupport::Texture);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_SHADER_LOAD, FormatSupport::ShaderLoad);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, FormatSupport::ShaderSample);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_RENDER_TARGET, FormatSupport::RenderTarget);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_BLENDABLE, FormatSupport::Blendable);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL, FormatSupport::DepthStencil);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE, FormatSupport::Resolvable);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET, FormatSupport::Multisampling);

            flags = d3dFormatSupport.Support2;
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD, FormatSupport::ShaderUavLoad);
            UPDATE_FLAGS(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE, FormatSupport::ShaderUavStore);
            if (is_set(formatSupport, FormatSupport::ShaderUavStore))
            {
                UPDATE_FLAGS(
                    (D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                     D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
                     D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                     D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX),
                    FormatSupport::ShaderAtomic
                );
            }
        }

#undef UPDATE_FLAGS

        if (formatSupport != FormatSupport::None)
        {
            formatSupport |= FormatSupport::CopySource | FormatSupport::CopyDestination;
        }

        m_formatSupport[formatIndex] = formatSupport;
    }

    // If user specified a higher shader model than what the system supports, return failure.
    int userSpecifiedShaderModel = getShaderModelFromProfileName(desc.slang.targetProfile);
    if (userSpecifiedShaderModel > shaderModelData.HighestShaderModel)
    {
        handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "The requested shader model is not supported by the system."
        );
        return SLANG_E_NOT_AVAILABLE;
    }

    // Check shader model version.
    SlangCompileTarget compileTarget = SLANG_DXBC;
    const char* profileName = "sm_5_1";
    for (auto& sm : kKnownShaderModels)
    {
        if (sm.shaderModel <= shaderModelData.HighestShaderModel)
        {
            addFeature(sm.feature);
            addCapability(sm.capability);
            profileName = sm.profileName;
            compileTarget = sm.compileTarget;
        }
        else
        {
            break;
        }
    }

    // Initialize slang context.
    SLANG_RETURN_ON_FAIL(m_slangContext.initialize(
        desc.slang,
        compileTarget,
        profileName,
        std::array{slang::PreprocessorMacroDesc{"__D3D12__", "1"}}
    ));

    // Create queue.
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    SLANG_RETURN_ON_FAIL(m_queue->init(0));
    m_queue->setInternalReferenceCount(1);

    // Retrieve timestamp frequency.
    m_queue->m_d3dQueue->GetTimestampFrequency(&m_info.timestampFrequency);

    // Initialize bindless descriptor set if supported.
    if (hasFeature(Feature::Bindless))
    {
        m_bindlessDescriptorSet = new BindlessDescriptorSet(this, desc.bindless);
        SLANG_RETURN_ON_FAIL(m_bindlessDescriptorSet->initialize());
    }

    return SLANG_OK;
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::D3D12Device;
    outHandles->handles[0].value = (uint64_t)m_device.get();
    outHandles->handles[1] = {};
    outHandles->handles[2] = {};
    return SLANG_OK;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
    {
        return SLANG_FAIL;
    }
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(
    const D3D12_RESOURCE_DESC& resourceDesc,
    const void* srcData,
    Size srcDataSize,
    D3D12_RESOURCE_STATES finalState,
    D3D12Resource& resourceOut,
    bool isShared,
    MemoryType memoryType
)
{
    const Size bufferSize = Size(resourceDesc.Width);

    D3D12_HEAP_PROPERTIES heapProps;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
    if (isShared)
        flags |= D3D12_HEAP_FLAG_SHARED;

    D3D12_RESOURCE_DESC desc = resourceDesc;

    D3D12_RESOURCE_STATES initialState = finalState;

    switch (memoryType)
    {
    case MemoryType::ReadBack:
        SLANG_RHI_ASSERT(!srcData);

        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        initialState |= D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    case MemoryType::Upload:

        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        initialState |= D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    case MemoryType::DeviceLocal:
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (initialState != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
            initialState = D3D12_RESOURCE_STATE_COMMON;
        break;
    default:
        return SLANG_FAIL;
    }

    // Create the resource.
    SLANG_RETURN_ON_FAIL(resourceOut.initCommitted(m_device, heapProps, flags, desc, initialState, nullptr));

    if (srcData)
    {
        D3D12Resource uploadResource;

        if (memoryType == MemoryType::DeviceLocal)
        {
            // If the buffer is on the default heap, create upload buffer.
            D3D12_RESOURCE_DESC uploadDesc(resourceDesc);
            uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            SLANG_RETURN_ON_FAIL(uploadResource.initCommitted(
                m_device,
                heapProps,
                D3D12_HEAP_FLAG_NONE,
                uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr
            ));
        }

        // Be careful not to actually copy a resource here.
        D3D12Resource& uploadResourceRef = (memoryType == MemoryType::DeviceLocal) ? uploadResource : resourceOut;

        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the vertex buffer.
        UINT8* dstData;
        D3D12_RANGE readRange = {}; // We do not intend to read from this resource on the CPU.

        ID3D12Resource* dxUploadResource = uploadResourceRef.getResource();

        SLANG_RETURN_ON_FAIL(dxUploadResource->Map(0, &readRange, reinterpret_cast<void**>(&dstData)));
        ::memcpy(dstData, srcData, srcDataSize);
        dxUploadResource->Unmap(0, nullptr);

        if (memoryType == MemoryType::DeviceLocal)
        {
            ID3D12GraphicsCommandList* commandList = beginImmediateCommandList();
            commandList->CopyBufferRegion(resourceOut, 0, uploadResourceRef, 0, bufferSize);
            endImmediateCommandList();
        }
    }

    return SLANG_OK;
}

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc_, Size* outSize, Size* outAlignment)
{
    TextureDesc desc = fixupTextureDesc(desc_);
    bool isTypeless = is_set(desc.usage, TextureUsage::Typeless);
    D3D12_RESOURCE_DESC resourceDesc = {};
    initTextureDesc(resourceDesc, desc, isTypeless);
    auto allocInfo = m_device->GetResourceAllocationInfo(0, 1, &resourceDesc);
    *outSize = (Size)allocInfo.SizeInBytes;
    *outAlignment = (Size)allocInfo.Alignment;
    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    return SLANG_OK;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    // Description of uploading on Dx12
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn899215%28v=vs.85%29.aspx

    TextureDesc desc = fixupTextureDesc(desc_);

    bool isTypeless = is_set(desc.usage, TextureUsage::Typeless);
    if (isDepthFormat(desc.format) &&
        (is_set(desc.usage, TextureUsage::ShaderResource) || is_set(desc.usage, TextureUsage::UnorderedAccess)))
    {
        isTypeless = true;
    }
    D3D12_RESOURCE_DESC resourceDesc = {};
    SLANG_RETURN_ON_FAIL(initTextureDesc(resourceDesc, desc, isTypeless));

    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));

    texture->m_format = resourceDesc.Format;
    texture->m_isTypeless = isTypeless;
    texture->m_defaultState = translateResourceState(desc.defaultState);

    // Create the target resource
    {
        D3D12_HEAP_PROPERTIES heapProps;

        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        if (is_set(desc.usage, TextureUsage::Shared))
            flags |= D3D12_HEAP_FLAG_SHARED;

        D3D12_CLEAR_VALUE clearValue;
        D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
        clearValue.Format = resourceDesc.Format;
        if (desc.optimalClearValue)
        {
            memcpy(clearValue.Color, &desc.optimalClearValue->color, sizeof(clearValue.Color));
            clearValue.DepthStencil.Depth = desc.optimalClearValue->depthStencil.depth;
            clearValue.DepthStencil.Stencil = desc.optimalClearValue->depthStencil.stencil;
            clearValuePtr = &clearValue;
        }
        if ((resourceDesc.Flags &
             (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == 0)
        {
            clearValuePtr = nullptr;
        }
        if (isTypelessDepthFormat(resourceDesc.Format))
        {
            clearValuePtr = nullptr;
        }
        SLANG_RETURN_ON_FAIL(
            texture->m_resource
                .initCommitted(m_device, heapProps, flags, resourceDesc, texture->m_defaultState, clearValuePtr)
        );

        if (desc.label)
        {
            texture->m_resource.setDebugName(desc.label);
        }
    }

    // Upload init data if we have some
    if (initData)
    {
        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));

        SubresourceRange range;
        range.layer = 0;
        range.layerCount = desc.getLayerCount();
        range.mip = 0;
        range.mipCount = desc.mipCount;

        commandEncoder->uploadTextureData(
            texture,
            range,
            {0, 0, 0},
            Extent3D::kWholeTexture,
            initData,
            range.layerCount * desc.mipCount
        );

        SLANG_RETURN_ON_FAIL(queue->submit(commandEncoder->finish()));
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc, ITexture** outTexture)
{
    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));

    if (handle.type == NativeHandleType::D3D12Resource)
    {
        texture->m_resource.setResource((ID3D12Resource*)handle.value);
    }
    else
    {
        return SLANG_FAIL;
    }

    bool isTypeless = is_set(desc.usage, TextureUsage::Typeless);
    if (isDepthFormat(desc.format) &&
        (is_set(desc.usage, TextureUsage::ShaderResource) || is_set(desc.usage, TextureUsage::UnorderedAccess)))
    {
        isTypeless = true;
    }

    texture->m_format =
        isTypeless ? getFormatMapping(desc.format).typelessFormat : getFormatMapping(desc.format).rtvFormat;
    texture->m_isTypeless = isTypeless;
    texture->m_defaultState = translateResourceState(desc.defaultState);

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));

    D3D12_RESOURCE_DESC bufferDesc;
    initBufferDesc(desc.size, bufferDesc);

    bufferDesc.Flags |= calcResourceFlags(desc.usage);

    const D3D12_RESOURCE_STATES initialState = buffer->m_defaultState;
    SLANG_RETURN_ON_FAIL(createBuffer(
        bufferDesc,
        initData,
        desc.size,
        initialState,
        buffer->m_resource,
        is_set(desc.usage, BufferUsage::Shared),
        desc.memoryType
    ));

    if (desc.label)
    {
        buffer->m_resource.setDebugName(desc.label);
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));

    if (handle.type == NativeHandleType::D3D12Resource)
    {
        buffer->m_resource.setResource((ID3D12Resource*)handle.value);
    }
    else
    {
        return SLANG_FAIL;
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    D3D12_FILTER_REDUCTION_TYPE dxReduction = translateFilterReduction(desc.reductionOp);
    D3D12_FILTER dxFilter;
    if (desc.maxAnisotropy > 1)
    {
        if (desc.mipFilter == TextureFilteringMode::Linear)
        {
            dxFilter = D3D12_ENCODE_ANISOTROPIC_FILTER(dxReduction);
        }
        else
        {
            dxFilter = D3D12_ENCODE_MIN_MAG_ANISOTROPIC_MIP_POINT_FILTER(dxReduction);
        }
    }
    else
    {
        D3D12_FILTER_TYPE dxMin = translateFilterMode(desc.minFilter);
        D3D12_FILTER_TYPE dxMag = translateFilterMode(desc.magFilter);
        D3D12_FILTER_TYPE dxMip = translateFilterMode(desc.mipFilter);

        dxFilter = D3D12_ENCODE_BASIC_FILTER(dxMin, dxMag, dxMip, dxReduction);
    }

    D3D12_SAMPLER_DESC dxDesc = {};
    dxDesc.Filter = dxFilter;
    dxDesc.AddressU = translateAddressingMode(desc.addressU);
    dxDesc.AddressV = translateAddressingMode(desc.addressV);
    dxDesc.AddressW = translateAddressingMode(desc.addressW);
    dxDesc.MipLODBias = desc.mipLODBias;
    dxDesc.MaxAnisotropy = desc.maxAnisotropy;
    dxDesc.ComparisonFunc = translateComparisonFunc(desc.comparisonFunc);
    for (int ii = 0; ii < 4; ++ii)
        dxDesc.BorderColor[ii] = desc.borderColor[ii];
    dxDesc.MinLOD = desc.minLOD;
    dxDesc.MaxLOD = desc.maxLOD;

    CPUDescriptorAllocation descriptor = m_cpuSamplerHeap->allocate();
    if (!descriptor)
    {
        return SLANG_FAIL;
    }
    m_device->CreateSampler(&dxDesc, descriptor.cpuHandle);

    // TODO: We really ought to have a free-list of sampler-heap
    // entries that we check before we go to the heap, and then
    // when we are done with a sampler we simply add it to the free list.
    //
    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(this, desc);
    samplerImpl->m_descriptor = descriptor;
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

Result DeviceImpl::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    RefPtr<InputLayoutImpl> layout = new InputLayoutImpl();

    // Work out a buffer size to hold all text
    size_t textSize = 0;
    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const char* text = desc.inputElements[i].semanticName;
        textSize += text ? (::strlen(text) + 1) : 0;
    }
    layout->m_text.resize(textSize);
    char* textPos = layout->m_text.data();

    layout->m_elements.resize(desc.inputElementCount);

    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& srcElement = desc.inputElements[i];
        const VertexStreamDesc& srcStream = desc.vertexStreams[srcElement.bufferSlotIndex];
        D3D12_INPUT_ELEMENT_DESC& dstElement = layout->m_elements[i];

        // Add text to the buffer
        const char* semanticName = srcElement.semanticName;
        if (semanticName)
        {
            const int len = int(::strlen(semanticName));
            ::memcpy(textPos, semanticName, len + 1);
            semanticName = textPos;
            textPos += len + 1;
        }

        dstElement.SemanticName = semanticName;
        dstElement.SemanticIndex = (UINT)srcElement.semanticIndex;
        dstElement.Format = getVertexFormat(srcElement.format);
        dstElement.InputSlot = (UINT)srcElement.bufferSlotIndex;
        dstElement.AlignedByteOffset = (UINT)srcElement.offset;
        dstElement.InputSlotClass = translateInputSlotClass(srcStream.slotClass);
        dstElement.InstanceDataStepRate = (UINT)srcStream.instanceDataStepRate;
    }

    layout->m_vertexStreamStrides.resize(desc.vertexStreamCount);
    for (uint32_t i = 0; i < desc.vertexStreamCount; ++i)
    {
        layout->m_vertexStreamStrides[i] = (UINT)desc.vertexStreams[i].stride;
    }

    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    auto bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

    // This will be slow!!! - it blocks CPU on GPU completion
    D3D12Resource& resource = bufferImpl->m_resource;

    D3D12Resource stageBuf;
    if (bufferImpl->m_desc.memoryType != MemoryType::ReadBack)
    {
        ID3D12GraphicsCommandList* commandList = beginImmediateCommandList();

        // Readback heap
        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        // Resource to readback to
        D3D12_RESOURCE_DESC stagingDesc;
        initBufferDesc(size, stagingDesc);

        SLANG_RETURN_ON_FAIL(stageBuf.initCommitted(
            m_device,
            heapProps,
            D3D12_HEAP_FLAG_NONE,
            stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr
        ));

        // Do the copy
        commandList->CopyBufferRegion(stageBuf, 0, resource, offset, size);

        // Wait until complete
        endImmediateCommandList();
    }

    D3D12Resource& stageBufRef = bufferImpl->m_desc.memoryType != MemoryType::ReadBack ? stageBuf : resource;

    // Map and copy
    {
        UINT8* data;
        D3D12_RANGE readRange = {0, size};

        SLANG_RETURN_ON_FAIL(stageBufRef.getResource()->Map(0, &readRange, reinterpret_cast<void**>(&data)));

        // Copy to memory buffer
        std::memcpy(outData, data, size);

        stageBufRef.getResource()->Unmap(0, nullptr);
    }

    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    ComPtr<ID3DBlob> d3dDiagnosticBlob;
    auto rootShaderLayoutResult = RootShaderObjectLayoutImpl::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootObjectLayout.writeRef(),
        d3dDiagnosticBlob.writeRef()
    );
    if (SLANG_FAILED(rootShaderLayoutResult))
    {
        if (outDiagnosticBlob && d3dDiagnosticBlob)
        {
            auto diagnosticBlob =
                OwnedBlob::create(d3dDiagnosticBlob->GetBufferPointer(), d3dDiagnosticBlob->GetBufferSize());
            returnComPtr(outDiagnosticBlob, diagnosticBlob);
        }
        return rootShaderLayoutResult;
    }
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

Result DeviceImpl::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable)
{
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl(this, desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
}

ID3D12GraphicsCommandList* DeviceImpl::beginImmediateCommandList()
{
    m_immediateCommandList.mutex.lock();
    if (!m_immediateCommandList.commandAllocator)
    {
        SLANG_RETURN_NULL_ON_FAIL(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(m_immediateCommandList.commandAllocator.writeRef())
        ));
    }
    else
    {
        m_immediateCommandList.commandAllocator->Reset();
    }
    if (!m_immediateCommandList.commandList)
    {
        SLANG_RETURN_NULL_ON_FAIL(m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_immediateCommandList.commandAllocator,
            nullptr,
            IID_PPV_ARGS(m_immediateCommandList.commandList.writeRef())
        ));
    }
    else
    {
        m_immediateCommandList.commandList->Reset(m_immediateCommandList.commandAllocator, nullptr);
    }
    return m_immediateCommandList.commandList;
}

void DeviceImpl::endImmediateCommandList()
{
    m_immediateCommandList.commandList->Close();
    ID3D12CommandList* commandLists[] = {m_immediateCommandList.commandList.get()};
    m_queue->m_d3dQueue->ExecuteCommandLists(1, commandLists);
    m_queue->waitOnHost();
    m_immediateCommandList.mutex.unlock();
}

void DeviceImpl::flushValidationMessages()
{
#if SLANG_RHI_ENABLE_NVAPI
    if (m_raytracingValidationHandle)
    {
        SLANG_RHI_NVAPI_CHECK(NvAPI_D3D12_FlushRaytracingValidationMessages(m_device5));
    }
#endif
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceImpl::getNullDescriptor(
    slang::BindingType bindingType,
    SlangResourceShape resourceShape
)
{
    std::lock_guard<std::mutex> lock(m_nullDescriptorsMutex);
    NullDescriptorKey key = {bindingType, resourceShape};
    auto it = m_nullDescriptors.find(key);
    if (it != m_nullDescriptors.end())
    {
        return it->second.cpuHandle;
    }
    CPUDescriptorAllocation allocation = m_cpuCbvSrvUavHeap->allocate();
    Result result = createNullDescriptor(m_device, allocation.cpuHandle, bindingType, resourceShape);
    if (SLANG_FAILED(result))
    {
        SLANG_RHI_ASSERT_FAILURE("Failed to create null descriptor");
    }
    m_nullDescriptors[key] = allocation;
    return allocation.cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceImpl::getNullSamplerDescriptor()
{
    std::lock_guard<std::mutex> lock(m_nullDescriptorsMutex);
    if (m_nullSamplerDescriptor)
    {
        return m_nullSamplerDescriptor.cpuHandle;
    }
    m_nullSamplerDescriptor = m_cpuSamplerHeap->allocate();
    D3D12_SAMPLER_DESC desc = {};
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    m_device->CreateSampler(&desc, m_nullSamplerDescriptor.cpuHandle);
    return m_nullSamplerDescriptor.cpuHandle;
}

void DeviceImpl::processExperimentalFeaturesDesc(SharedLibraryHandle d3dModule, const void* inDesc)
{
    typedef HRESULT(WINAPI * PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES)(
        UINT NumFeatures,
        const IID* pIIDs,
        void* pConfigurationStructs,
        UINT* pConfigurationStructSizes
    );

    D3D12ExperimentalFeaturesDesc desc = {};
    memcpy(&desc, inDesc, sizeof(desc));
    auto enableExperimentalFeaturesFunc =
        (PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES)loadProc(d3dModule, "D3D12EnableExperimentalFeatures");
    if (!enableExperimentalFeaturesFunc)
    {
        handleMessage(
            DebugMessageType::Warning,
            DebugMessageSource::Layer,
            "cannot enable D3D12 experimental features, 'D3D12EnableExperimentalFeatures' function "
            "not found."
        );
        return;
    }
    if (SLANG_FAILED(enableExperimentalFeaturesFunc(
            (UINT)desc.featureCount,
            (const IID*)desc.featureIIDs,
            (void*)desc.configurationStructs,
            (UINT*)desc.configurationStructSizes
        )))
    {
        handleMessage(
            DebugMessageType::Warning,
            DebugMessageSource::Layer,
            "cannot enable D3D12 experimental features, 'D3D12EnableExperimentalFeatures' call "
            "failed."
        );
        return;
    }
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outState)
{
    switch (desc.type)
    {
    case QueryType::AccelerationStructureCompactedSize:
    case QueryType::AccelerationStructureSerializedSize:
    case QueryType::AccelerationStructureCurrentSize:
    {
        RefPtr<PlainBufferProxyQueryPoolImpl> queryPoolImpl = new PlainBufferProxyQueryPoolImpl(this, desc);
        uint32_t stride = 8;
        if (desc.type == QueryType::AccelerationStructureSerializedSize)
            stride = 16;
        SLANG_RETURN_ON_FAIL(queryPoolImpl->init(stride));
        returnComPtr(outState, queryPoolImpl);
        return SLANG_OK;
    }
    default:
    {
        RefPtr<QueryPoolImpl> queryPoolImpl = new QueryPoolImpl(this, desc);
        SLANG_RETURN_ON_FAIL(queryPoolImpl->init());
        returnComPtr(outState, queryPoolImpl);
        return SLANG_OK;
    }
    }
}

Result DeviceImpl::createFence(const FenceDesc& desc, IFence** outFence)
{
    RefPtr<FenceImpl> fence = new FenceImpl(this, desc);
    SLANG_RETURN_ON_FAIL(fence->init());
    returnComPtr(outFence, fence);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    const uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    short_vector<HANDLE> waitHandles;
    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto fenceImpl = checked_cast<FenceImpl*>(fences[i]);
        waitHandles.push_back(fenceImpl->getWaitEvent());
        SLANG_RETURN_ON_FAIL(fenceImpl->m_fence->SetEventOnCompletion(fenceValues[i], fenceImpl->getWaitEvent()));
    }
    auto result = WaitForMultipleObjects(
        fenceCount,
        waitHandles.data(),
        waitForAll ? TRUE : FALSE,
        timeout == kTimeoutInfinite ? INFINITE : (DWORD)(timeout / 1000000)
    );
    if (result == WAIT_TIMEOUT)
        return SLANG_E_TIME_OUT;
    return result == WAIT_FAILED ? SLANG_FAIL : SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    if (!m_device5)
        return SLANG_E_NOT_AVAILABLE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};

#if SLANG_RHI_ENABLE_NVAPI
    if (m_nvapiEnabled)
    {
        AccelerationStructureBuildDescConverterNVAPI converter;
        SLANG_RETURN_ON_FAIL(converter.convert(desc, m_debugCallback));

        NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params = {};
        params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
        params.pDesc = &converter.desc;
        params.pInfo = &prebuildInfo;
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(
            NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(m_device5, &params)
        );
    }
    else
#endif // SLANG_RHI_ENABLE_NVAPI
    {
        AccelerationStructureBuildDescConverter converter;
        SLANG_RETURN_ON_FAIL(converter.convert(desc, m_debugCallback));

        m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&converter.desc, &prebuildInfo);
    }

    outSizes->accelerationStructureSize = prebuildInfo.ResultDataMaxSizeInBytes;
    outSizes->scratchSize = prebuildInfo.ScratchDataSizeInBytes;
    outSizes->updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

    return SLANG_OK;
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    BufferDesc bufferDesc = {};
    bufferDesc.size = desc.size;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::AccelerationStructure;
    bufferDesc.defaultState = ResourceState::AccelerationStructure;
    SLANG_RETURN_ON_FAIL(createBuffer(bufferDesc, nullptr, (IBuffer**)result->m_buffer.writeRef()));
    result->m_descriptor = m_cpuCbvSrvUavHeap->allocate();
    if (!result->m_descriptor)
        return SLANG_FAIL;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = result->m_buffer->getDeviceAddress();
    m_device->CreateShaderResourceView(nullptr, &srvDesc, result->m_descriptor.cpuHandle);
    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
}

Result DeviceImpl::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
#if SLANG_RHI_ENABLE_NVAPI
    if (!m_nvapiEnabled)
        return SLANG_E_NOT_AVAILABLE;

    if (m_cooperativeVectorProperties.empty())
    {
        NvU32 nvPropertyCount = 0;
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(
            NvAPI_D3D12_GetPhysicalDeviceCooperativeVectorProperties(m_device, &nvPropertyCount, nullptr)
        );
        std::vector<NVAPI_COOPERATIVE_VECTOR_PROPERTIES> nvProperties(nvPropertyCount);
        SLANG_RHI_NVAPI_RETURN_ON_FAIL(
            NvAPI_D3D12_GetPhysicalDeviceCooperativeVectorProperties(m_device, &nvPropertyCount, nvProperties.data())
        );
        for (const auto& nvProps : nvProperties)
        {
            CooperativeVectorProperties props;
            props.inputType = translateCooperativeVectorComponentType(nvProps.inputType);
            props.inputInterpretation = translateCooperativeVectorComponentType(nvProps.inputInterpretation);
            props.matrixInterpretation = translateCooperativeVectorComponentType(nvProps.matrixInterpretation);
            props.biasInterpretation = translateCooperativeVectorComponentType(nvProps.biasInterpretation);
            props.resultType = translateCooperativeVectorComponentType(nvProps.resultType);
            props.transpose = nvProps.transpose;
            m_cooperativeVectorProperties.push_back(props);
        }
    }

    return Device::getCooperativeVectorProperties(properties, propertiesCount);
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

Result DeviceImpl::getCooperativeVectorMatrixSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    size_t rowColumnStride,
    size_t* outSize
)
{
#if SLANG_RHI_ENABLE_NVAPI
    if (!m_nvapiEnabled)
        return SLANG_E_NOT_AVAILABLE;

    if (rowColumnStride == 0)
    {
        rowColumnStride = getTightRowColumnStride(rowCount, colCount, componentType, layout);
    }

    NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC nvDesc = {};
    nvDesc.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER1;
    nvDesc.pDstSize = outSize;
    nvDesc.srcComponentType = translateCooperativeVectorComponentType(componentType);
    nvDesc.dstComponentType = translateCooperativeVectorComponentType(componentType);
    nvDesc.numRows = rowCount;
    nvDesc.numColumns = colCount;
    nvDesc.srcLayout = translateCooperativeVectorMatrixLayout(layout);
    nvDesc.srcStride = rowColumnStride;
    nvDesc.dstLayout = translateCooperativeVectorMatrixLayout(layout);
    nvDesc.dstStride = rowColumnStride;
    SLANG_RHI_NVAPI_RETURN_ON_FAIL(NvAPI_D3D12_ConvertCooperativeVectorMatrix(m_device, nullptr, &nvDesc));
    *outSize = math::calcAligned(*outSize, 64);
    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

Result DeviceImpl::convertCooperativeVectorMatrix(
    void* dstBuffer,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    const void* srcBuffer,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
#if SLANG_RHI_ENABLE_NVAPI
    short_vector<NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC> nvDescs;
    for (uint32_t i = 0; i < matrixCount; ++i)
    {
        const CooperativeVectorMatrixDesc& dstDesc = dstDescs[i];
        const CooperativeVectorMatrixDesc& srcDesc = srcDescs[i];
        NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC nvDesc = {};
        nvDesc.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER1;
        nvDesc.srcSize = srcDesc.size;
        nvDesc.srcData.pHostAddress = (uint8_t*)srcBuffer + srcDesc.offset;
        nvDesc.pDstSize = (size_t*)&dstDesc.size;
        nvDesc.dstData.pHostAddress = (uint8_t*)dstBuffer + dstDesc.offset;
        nvDesc.srcComponentType = translateCooperativeVectorComponentType(srcDesc.componentType);
        nvDesc.dstComponentType = translateCooperativeVectorComponentType(dstDesc.componentType);
        nvDesc.numRows = srcDesc.rowCount;
        nvDesc.numColumns = srcDesc.colCount;
        nvDesc.srcLayout = translateCooperativeVectorMatrixLayout(srcDesc.layout);
        nvDesc.srcStride = srcDesc.rowColumnStride;
        nvDesc.dstLayout = translateCooperativeVectorMatrixLayout(dstDesc.layout);
        nvDesc.dstStride = dstDesc.rowColumnStride;
        nvDescs.push_back(nvDesc);
    }
    SLANG_RHI_NVAPI_RETURN_ON_FAIL(
        NvAPI_D3D12_ConvertCooperativeVectorMatrixMultiple(m_device, nullptr, nvDescs.data(), (NvU32)nvDescs.size())
    );
    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

void* DeviceImpl::loadProc(SharedLibraryHandle module, const char* name)
{
    void* proc = findSymbolAddressByName(module, name);
    if (!proc)
    {
        fprintf(stderr, "error: failed load symbol '%s'\n", name);
        return nullptr;
    }
    return proc;
}

DeviceImpl::~DeviceImpl()
{
#if SLANG_RHI_ENABLE_NVAPI
    if (m_raytracingValidationHandle)
    {
        SLANG_RHI_NVAPI_CHECK(
            NvAPI_D3D12_UnregisterRaytracingValidationMessageCallback(m_device5, m_raytracingValidationHandle)
        );
    }
#endif

    m_shaderObjectLayoutCache = decltype(m_shaderObjectLayoutCache)();
    m_queue.setNull();

    m_bindlessDescriptorSet.setNull();

    for (const auto& [_, allocation] : m_nullDescriptors)
    {
        m_cpuCbvSrvUavHeap->free(allocation);
    }
    if (m_nullSamplerDescriptor)
    {
        m_cpuSamplerHeap->free(m_nullSamplerDescriptor);
    }

    if (m_validationMessageCallbackCookie)
    {
        ComPtr<ID3D12InfoQueue1> infoQueue1;
        if (SLANG_SUCCEEDED(m_device->QueryInterface(infoQueue1.writeRef())))
        {
            infoQueue1->UnregisterMessageCallback(m_validationMessageCallbackCookie);
            m_validationMessageCallbackCookie = 0;
        }
    }
}

} // namespace rhi::d3d12

namespace rhi {

IAdapter* getD3D12Adapter(uint32_t index)
{
    std::vector<d3d12::AdapterImpl>& adapters = d3d12::getAdapters();
    return index < adapters.size() ? &adapters[index] : nullptr;
}

Result createD3D12Device(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<d3d12::DeviceImpl> result = new d3d12::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

void enableD3D12DebugLayerIfAvailable()
{
    SharedLibraryHandle d3dModule;
#if SLANG_WINDOWS_FAMILY
    const char* libName = "d3d12";
#else
    const char* libName = "libvkd3d-proton-d3d12.so";
#endif
    if (SLANG_FAILED(loadSharedLibrary(libName, d3dModule)))
        return;
    PFN_D3D12_GET_DEBUG_INTERFACE d3d12GetDebugInterface =
        (PFN_D3D12_GET_DEBUG_INTERFACE)findSymbolAddressByName(d3dModule, "D3D12GetDebugInterface");
    ComPtr<ID3D12Debug> dxDebug;
    if (d3d12GetDebugInterface && SLANG_SUCCEEDED(d3d12GetDebugInterface(IID_PPV_ARGS(dxDebug.writeRef()))))
        dxDebug->EnableDebugLayer();
}

} // namespace rhi
