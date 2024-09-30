#include "d3d12-device.h"
#include "../nvapi/nvapi-util.h"
#include "d3d12-buffer.h"
#include "d3d12-fence.h"
#include "d3d12-helper-functions.h"
#include "d3d12-pipeline.h"
#include "d3d12-query.h"
#include "d3d12-texture-view.h"
#include "d3d12-sampler.h"
#include "d3d12-shader-object.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-table.h"
#include "d3d12-swap-chain.h"
#include "d3d12-vertex-layout.h"

#include "core/short_vector.h"
#include "core/string.h"

#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER 1
#else
#define ENABLE_DEBUG_LAYER 0
#endif

#ifdef SLANG_RHI_NVAPI
#include "../nvapi/nvapi-include.h"
#endif

#ifdef SLANG_RHI_NV_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi::d3d12 {

static const uint32_t D3D_FEATURE_LEVEL_12_2 = 0xc200;

#if SLANG_RHI_NV_AFTERMATH
const bool DeviceImpl::g_isAftermathEnabled = true;
#else
const bool DeviceImpl::g_isAftermathEnabled = false;
#endif

struct ShaderModelInfo
{
    D3D_SHADER_MODEL shaderModel;
    SlangCompileTarget compileTarget;
    const char* profileName;
};
// List of shader models. Do not change oldest to newest order.
static ShaderModelInfo kKnownShaderModels[] = {
#define SHADER_MODEL_INFO_DXBC(major, minor) {D3D_SHADER_MODEL_##major##_##minor, SLANG_DXBC, "sm_" #major "_" #minor}
    SHADER_MODEL_INFO_DXBC(5, 1),
#undef SHADER_MODEL_INFO_DXBC
#define SHADER_MODEL_INFO_DXIL(major, minor) {(D3D_SHADER_MODEL)0x##major##minor, SLANG_DXIL, "sm_" #major "_" #minor}
    SHADER_MODEL_INFO_DXIL(6, 0),
    SHADER_MODEL_INFO_DXIL(6, 1),
    SHADER_MODEL_INFO_DXIL(6, 2),
    SHADER_MODEL_INFO_DXIL(6, 3),
    SHADER_MODEL_INFO_DXIL(6, 4),
    SHADER_MODEL_INFO_DXIL(6, 5),
    SHADER_MODEL_INFO_DXIL(6, 6),
    SHADER_MODEL_INFO_DXIL(6, 7)
#undef SHADER_MODEL_INFO_DXIL
};

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
            auto encodeInfo = encodeResourceCommands();
            encodeInfo.d3dCommandList->CopyBufferRegion(resourceOut, 0, uploadResourceRef, 0, bufferSize);
            submitResourceCommandsAndWait(encodeInfo);
        }
    }

    return SLANG_OK;
}

Result DeviceImpl::getNativeDeviceHandles(NativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::D3D12Device;
    outHandles->handles[0].value = (uint64_t)m_device;
    outHandles->handles[1] = {};
    outHandles->handles[2] = {};
    return SLANG_OK;
}

Result DeviceImpl::_createDevice(
    DeviceCheckFlags deviceCheckFlags,
    const AdapterLUID* adapterLUID,
    D3D_FEATURE_LEVEL featureLevel,
    D3D12DeviceInfo& outDeviceInfo
)
{
    if (m_dxDebug && (deviceCheckFlags & DeviceCheckFlag::UseDebug) && !g_isAftermathEnabled)
    {
        m_dxDebug->EnableDebugLayer();
    }

    outDeviceInfo.clear();

    ComPtr<IDXGIFactory> dxgiFactory;
    SLANG_RETURN_ON_FAIL(D3DUtil::createFactory(deviceCheckFlags, dxgiFactory));

    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(D3DUtil::findAdapters(deviceCheckFlags, adapterLUID, dxgiFactory, dxgiAdapters));

    ComPtr<ID3D12Device> device;
    ComPtr<IDXGIAdapter> adapter;

    for (Index i = 0; i < dxgiAdapters.size(); ++i)
    {
        IDXGIAdapter* dxgiAdapter = dxgiAdapters[i];
        if (SLANG_SUCCEEDED(m_D3D12CreateDevice(dxgiAdapter, featureLevel, IID_PPV_ARGS(device.writeRef()))))
        {
            adapter = dxgiAdapter;
            break;
        }
    }

    if (!device)
    {
        return SLANG_FAIL;
    }

    if (m_dxDebug && (deviceCheckFlags & DeviceCheckFlag::UseDebug) && !g_isAftermathEnabled)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SLANG_SUCCEEDED(device->QueryInterface(infoQueue.writeRef())))
        {
            // Make break
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            if (m_extendedDesc.debugBreakOnD3D12Error)
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            }
            D3D12_MESSAGE_ID hideMessages[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
            };
            D3D12_INFO_QUEUE_FILTER f = {};
            f.DenyList.NumIDs = (UINT)SLANG_COUNT_OF(hideMessages);
            f.DenyList.pIDList = hideMessages;
            infoQueue->AddStorageFilterEntries(&f);

            // Apparently there is a problem with sm 6.3 with spurious errors, with debug layer
            // enabled
            D3D12_FEATURE_DATA_SHADER_MODEL featureShaderModel;
            featureShaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_3;
            SLANG_SUCCEEDED(
                device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &featureShaderModel, sizeof(featureShaderModel))
            );

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
    }

#ifdef SLANG_RHI_NV_AFTERMATH
    {
        if ((deviceCheckFlags & DeviceCheckFlag::UseDebug) && g_isAftermathEnabled)
        {
            // Initialize Nsight Aftermath for this device.
            // This combination of flags is not necessarily appropraite for real world usage
            const uint32_t aftermathFlags = GFSDK_Aftermath_FeatureFlags_EnableMarkers |      // Enable event marker
                                                                                              // tracking.
                                            GFSDK_Aftermath_FeatureFlags_CallStackCapturing | // Enable automatic call
                                                                                              // stack event markers.
                                            GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |  // Enable tracking of
                                                                                                   // resources.
                                            GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo | // Generate debug
                                                                                                   // information for
                                                                                                   // shaders.
                                            GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting; // Enable
                                                                                                     // additional
                                                                                                     // runtime shader
                                                                                                     // error reporting.

            auto initResult = GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, device);

            if (initResult != GFSDK_Aftermath_Result_Success)
            {
                SLANG_RHI_ASSERT_FAILURE("Unable to initialize aftermath");
                // Unable to initialize
                return SLANG_FAIL;
            }
        }
    }
#endif

    // Get the descs
    {
        adapter->GetDesc(&outDeviceInfo.m_desc);

        // Look up GetDesc1 info
        ComPtr<IDXGIAdapter1> adapter1;
        if (SLANG_SUCCEEDED(adapter->QueryInterface(adapter1.writeRef())))
        {
            adapter1->GetDesc1(&outDeviceInfo.m_desc1);
        }
    }

    // Save other info
    outDeviceInfo.m_device = device;
    outDeviceInfo.m_dxgiFactory = dxgiFactory;
    outDeviceInfo.m_adapter = adapter;
    outDeviceInfo.m_isWarp = D3DUtil::isWarp(dxgiFactory, adapter);
    const UINT kMicrosoftVendorId = 5140;
    outDeviceInfo.m_isSoftware = outDeviceInfo.m_isWarp ||
                                 ((outDeviceInfo.m_desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) ||
                                 outDeviceInfo.m_desc.VendorId == kMicrosoftVendorId;

    return SLANG_OK;
}

Result DeviceImpl::initialize(const Desc& desc)
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
        getDebugCallback()
            ->handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "error: failed load 'd3d12.dll'\n");
        return SLANG_FAIL;
    }

    // Find extended desc.
    for (GfxIndex i = 0; i < desc.extendedDescCount; i++)
    {
        StructType stype;
        memcpy(&stype, desc.extendedDescs[i], sizeof(stype));
        switch (stype)
        {
        case StructType::D3D12DeviceExtendedDesc:
            memcpy(&m_extendedDesc, desc.extendedDescs[i], sizeof(m_extendedDesc));
            break;
        case StructType::D3D12ExperimentalFeaturesDesc:
            processExperimentalFeaturesDesc(d3dModule, desc.extendedDescs[i]);
            break;
        }
    }

    // Initialize queue index allocator.
    // Support max 32 queues.
    m_queueIndexAllocator.initPool(32);

    // Initialize DeviceInfo
    {
        m_info.deviceType = DeviceType::D3D12;
        m_info.apiName = "D3D12";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
    }

    // Get all the dll entry points
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

#if SLANG_ENABLE_PIX
    HMODULE pixModule = LoadLibraryW(L"WinPixEventRuntime.dll");
    if (pixModule)
    {
        m_BeginEventOnCommandList =
            (PFN_BeginEventOnCommandList)GetProcAddress(pixModule, "PIXBeginEventOnCommandList");
        m_EndEventOnCommandList = (PFN_EndEventOnCommandList)GetProcAddress(pixModule, "PIXEndEventOnCommandList");
    }
#endif

    // If Aftermath is enabled, we can't enable the D3D12 debug layer as well
    if (ENABLE_DEBUG_LAYER || isRhiDebugLayerEnabled() && !g_isAftermathEnabled)
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

    m_D3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)loadProc(d3dModule, "D3D12CreateDevice");
    if (!m_D3D12CreateDevice)
    {
        return SLANG_FAIL;
    }

    if (!desc.existingDeviceHandles.handles[0])
    {
        FlagCombiner combiner;
        // TODO: we should probably provide a command-line option
        // to override UseDebug of default rather than leave it
        // up to each back-end to specify.
        if (ENABLE_DEBUG_LAYER || isRhiDebugLayerEnabled())
        {
            /// First try debug then non debug.
            combiner.add(DeviceCheckFlag::UseDebug, ChangeType::OnOff);
        }
        else
        {
            /// Don't bother with debug.
            combiner.add(DeviceCheckFlag::UseDebug, ChangeType::Off);
        }
        /// First try hardware, then reference.
        combiner.add(DeviceCheckFlag::UseHardwareDevice, ChangeType::OnOff);

        const D3D_FEATURE_LEVEL featureLevels[] =
            {(D3D_FEATURE_LEVEL)D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
        for (auto featureLevel : featureLevels)
        {
            const int numCombinations = combiner.getNumCombinations();
            for (int i = 0; i < numCombinations; ++i)
            {
                if (SLANG_SUCCEEDED(
                        _createDevice(combiner.getCombination(i), desc.adapterLUID, featureLevel, m_deviceInfo)
                    ))
                {
                    goto succ;
                }
            }
        }
    succ:
        if (!m_deviceInfo.m_adapter)
        {
            // Couldn't find an adapter
            return SLANG_FAIL;
        }
    }
    else
    {
        if (desc.existingDeviceHandles.handles[0].type != NativeHandleType::D3D12Device)
        {
            return SLANG_FAIL;
        }
        // Store the existing device handle in desc in m_deviceInfo
        m_deviceInfo.m_device = (ID3D12Device*)desc.existingDeviceHandles.handles[0].value;
    }

    // Set the device
    m_device = m_deviceInfo.m_device;

    if (m_deviceInfo.m_isSoftware)
    {
        m_features.push_back("software-device");
    }
    else
    {
        m_features.push_back("hardware-device");
    }

    // NVAPI
    if (desc.nvapiExtnSlot >= 0)
    {
        if (SLANG_FAILED(NVAPIUtil::initialize()))
        {
            return SLANG_E_NOT_AVAILABLE;
        }

#ifdef SLANG_RHI_NVAPI
        // From DOCS: Applications are expected to bind null UAV to this slot.
        // NOTE! We don't currently do this, but doesn't seem to be a problem.

        const NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpace(m_device, NvU32(desc.nvapiExtnSlot), NvU32(0));

        if (status != NVAPI_OK)
        {
            return SLANG_E_NOT_AVAILABLE;
        }

        if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_UINT64_ATOMIC))
        {
            m_features.push_back("atomic-int64");
        }
        if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP32_ATOMIC))
        {
            m_features.push_back("atomic-float");
        }

        // If we have NVAPI well assume we have realtime clock
        {
            m_features.push_back("realtime-clock");
        }

        m_nvapi = true;
#endif
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModelData = {};

    // Find what features are supported
    {
        // Check this is how this is laid out...
        static_assert(D3D_SHADER_MODEL_6_0 == 0x60);

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
                if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(
                        D3D12_FEATURE_SHADER_MODEL,
                        &shaderModelData,
                        sizeof(shaderModelData)
                    )))
                    break;
            }

            // TODO: Currently warp causes a crash when using half, so disable for now
            if (m_deviceInfo.m_isWarp == false && shaderModelData.HighestShaderModel >= D3D_SHADER_MODEL_6_2)
            {
                // With sm_6_2 we have half
                m_features.push_back("half");
            }
        }
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options;
            if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
            {
                // Check double precision support
                if (options.DoublePrecisionFloatShaderOps)
                    m_features.push_back("double");

                // Check conservative-rasterization support
                auto conservativeRasterTier = options.ConservativeRasterizationTier;
                if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_3)
                {
                    m_features.push_back("conservative-rasterization-3");
                    m_features.push_back("conservative-rasterization-2");
                    m_features.push_back("conservative-rasterization-1");
                }
                else if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_2)
                {
                    m_features.push_back("conservative-rasterization-2");
                    m_features.push_back("conservative-rasterization-1");
                }
                else if (conservativeRasterTier == D3D12_CONSERVATIVE_RASTERIZATION_TIER_1)
                {
                    m_features.push_back("conservative-rasterization-1");
                }

                // Check rasterizer ordered views support
                if (options.ROVsSupported)
                {
                    m_features.push_back("rasterizer-ordered-views");
                }
            }
        }
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 options;
            if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options))))
            {
                // Check wave operations support
                if (options.WaveOps)
                    m_features.push_back("wave-ops");
            }
        }
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS2 options;
            if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options, sizeof(options))))
            {
                // Check programmable sample positions support
                switch (options.ProgrammableSamplePositionsTier)
                {
                case D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_2:
                    m_features.push_back("programmable-sample-positions-2");
                    m_features.push_back("programmable-sample-positions-1");
                    break;
                case D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_1:
                    m_features.push_back("programmable-sample-positions-1");
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
                    m_features.push_back("barycentrics");
                }
            }
        }
        // Check ray tracing support
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
            if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))))
            {
                if (options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
                {
                    m_features.push_back("ray-tracing");
                }
                if (options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
                {
                    m_features.push_back("ray-query");
                }
            }
        }
        // Check mesh shader support
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 options;
            if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options))))
            {
                if (options.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
                {
                    m_features.push_back("mesh-shader");
                }
            }
        }
    }

    m_desc = desc;

    // Create a command queue for internal resource transfer operations.
    SLANG_RETURN_ON_FAIL(createCommandQueueImpl(m_resourceCommandQueue.writeRef()));
    // `CommandQueueImpl` holds a back reference to `D3D12Device`, make it a weak reference here
    // since this object is already owned by `D3D12Device`.
    m_resourceCommandQueue->breakStrongReferenceToDevice();
    // Retrieve timestamp frequency.
    m_resourceCommandQueue->m_d3dQueue->GetTimestampFrequency(&m_info.timestampFrequency);

    // Get device limits.
    {
        DeviceLimits limits = {};
        limits.maxTextureDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        limits.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        limits.maxTextureDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        limits.maxTextureDimensionCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
        limits.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;

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

    SLANG_RETURN_ON_FAIL(createTransientResourceHeapImpl(
        ITransientResourceHeap::Flags::AllowResizing,
        0,
        8,
        4,
        m_resourceCommandTransientHeap.writeRef()
    ));
    // `TransientResourceHeap` holds a back reference to `D3D12Device`, make it a weak reference
    // here since this object is already owned by `D3D12Device`.
    m_resourceCommandTransientHeap->breakStrongReferenceToDevice();

    m_cpuViewHeap = new D3D12GeneralExpandingDescriptorHeap();
    SLANG_RETURN_ON_FAIL(
        m_cpuViewHeap
            ->init(m_device, 1024 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
    );
    m_cpuSamplerHeap = new D3D12GeneralExpandingDescriptorHeap();
    SLANG_RETURN_ON_FAIL(
        m_cpuSamplerHeap->init(m_device, 2048, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
    );

    m_rtvAllocator = new D3D12GeneralExpandingDescriptorHeap();
    SLANG_RETURN_ON_FAIL(
        m_rtvAllocator->init(m_device, 16 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
    );
    m_dsvAllocator = new D3D12GeneralExpandingDescriptorHeap();
    SLANG_RETURN_ON_FAIL(
        m_dsvAllocator->init(m_device, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
    );

    ComPtr<IDXGIDevice> dxgiDevice;
    if (m_deviceInfo.m_adapter)
    {
        DXGI_ADAPTER_DESC adapterDesc;
        m_deviceInfo.m_adapter->GetDesc(&adapterDesc);
        m_adapterName = string::from_wstring(adapterDesc.Description);
        m_info.adapterName = m_adapterName.data();
    }

    // Initialize DXR interface.
#if SLANG_RHI_DXR
    m_device->QueryInterface<ID3D12Device5>(m_deviceInfo.m_device5.writeRef());
    m_device5 = m_deviceInfo.m_device5.get();
#endif
    // Check shader model version.
    SlangCompileTarget compileTarget = SLANG_DXBC;
    const char* profileName = "sm_5_1";
    for (auto& sm : kKnownShaderModels)
    {
        if (sm.shaderModel <= shaderModelData.HighestShaderModel)
        {
            m_features.push_back(sm.profileName);
            profileName = sm.profileName;
            compileTarget = sm.compileTarget;
        }
        else
        {
            break;
        }
    }
    // If user specified a higher shader model than what the system supports, return failure.
    int userSpecifiedShaderModel = D3DUtil::getShaderModelFromProfileName(desc.slang.targetProfile);
    if (userSpecifiedShaderModel > shaderModelData.HighestShaderModel)
    {
        getDebugCallback()->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "The requested shader model is not supported by the system."
        );
        return SLANG_E_NOT_AVAILABLE;
    }
    SLANG_RETURN_ON_FAIL(slangContext.initialize(
        desc.slang,
        desc.extendedDescCount,
        desc.extendedDescs,
        compileTarget,
        profileName,
        make_array(slang::PreprocessorMacroDesc{"__D3D12__", "1"})
    ));

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `DrawInstancedIndirect` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC desc;
        desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &args;
        desc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(
            m_device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(drawIndirectCmdSignature.writeRef()))
        );
    }

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `DrawIndexedInstancedIndirect` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC desc;
        desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &args;
        desc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(
            m_device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(drawIndexedIndirectCmdSignature.writeRef()))
        );
    }

    // Allocate a D3D12 "command signature" object that matches the behavior
    // of a D3D11-style `Dispatch` operation.
    {
        D3D12_INDIRECT_ARGUMENT_DESC args;
        args.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC desc;
        desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &args;
        desc.NodeMask = 0;

        SLANG_RETURN_ON_FAIL(
            m_device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(dispatchIndirectCmdSignature.writeRef()))
        );
    }
    m_isInitialized = true;
    return SLANG_OK;
}

Result DeviceImpl::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    RefPtr<TransientResourceHeapImpl> heap;
    SLANG_RETURN_ON_FAIL(createTransientResourceHeapImpl(
        desc.flags,
        desc.constantBufferSize,
        getViewDescriptorCount(desc),
        std::max(1024, desc.samplerDescriptorCount),
        heap.writeRef()
    ));
    returnComPtr(outHeap, heap);
    return SLANG_OK;
}

Result DeviceImpl::createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue)
{
    RefPtr<CommandQueueImpl> queue;
    SLANG_RETURN_ON_FAIL(createCommandQueueImpl(queue.writeRef()));
    returnComPtr(outQueue, queue);
    return SLANG_OK;
}

Result DeviceImpl::createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain)
{
    RefPtr<SwapchainImpl> swapchain = new SwapchainImpl();
    SLANG_RETURN_ON_FAIL(swapchain->init(this, desc, window));
    returnComPtr(outSwapchain, swapchain);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize)
{
    TextureImpl* textureImpl = static_cast<TextureImpl*>(texture);

    auto& resource = textureImpl->m_resource;

    const TextureDesc& rhiDesc = textureImpl->getDesc();
    const D3D12_RESOURCE_DESC desc = resource.getResource()->GetDesc();

    // Don't bother supporting MSAA for right now
    if (desc.SampleDesc.Count > 1)
    {
        fprintf(stderr, "ERROR: cannot capture multi-sample texture\n");
        return SLANG_FAIL;
    }

    FormatInfo formatInfo;
    rhiGetFormatInfo(rhiDesc.format, &formatInfo);
    Size bytesPerPixel = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    Size rowPitch = int(desc.Width) * bytesPerPixel;
    static const Size align = 256;                    // D3D requires minimum 256 byte alignment for texture data.
    rowPitch = (rowPitch + align - 1) & ~(align - 1); // Bit trick for rounding up
    Size bufferSize = rowPitch * int(desc.Height) * int(desc.DepthOrArraySize);
    if (outRowPitch)
        *outRowPitch = rowPitch;
    if (outPixelSize)
        *outPixelSize = bytesPerPixel;

    D3D12Resource stagingResource;
    {
        D3D12_RESOURCE_DESC stagingDesc;
        initBufferDesc(bufferSize, stagingDesc);

        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        SLANG_RETURN_ON_FAIL(stagingResource.initCommitted(
            m_device,
            heapProps,
            D3D12_HEAP_FLAG_NONE,
            stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr
        ));
    }

    auto encodeInfo = encodeResourceCommands();

    auto defaultState = D3DUtil::getResourceState(rhiDesc.defaultState);
    {
        D3D12BarrierSubmitter submitter(encodeInfo.d3dCommandList);
        resource.transition(defaultState, D3D12_RESOURCE_STATE_COPY_SOURCE, submitter);
    }

    // Do the copy
    {
        D3D12_TEXTURE_COPY_LOCATION srcLoc;
        srcLoc.pResource = resource;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc;
        dstLoc.pResource = stagingResource;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint.Offset = 0;
        dstLoc.PlacedFootprint.Footprint.Format = desc.Format;
        dstLoc.PlacedFootprint.Footprint.Width = UINT(desc.Width);
        dstLoc.PlacedFootprint.Footprint.Height = UINT(desc.Height);
        dstLoc.PlacedFootprint.Footprint.Depth = UINT(desc.DepthOrArraySize);
        dstLoc.PlacedFootprint.Footprint.RowPitch = UINT(rowPitch);

        encodeInfo.d3dCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

    {
        D3D12BarrierSubmitter submitter(encodeInfo.d3dCommandList);
        resource.transition(D3D12_RESOURCE_STATE_COPY_SOURCE, defaultState, submitter);
    }

    // Submit the copy, and wait for copy to complete
    submitResourceCommandsAndWait(encodeInfo);

    {
        ID3D12Resource* dxResource = stagingResource;

        UINT8* data;
        D3D12_RANGE readRange = {0, bufferSize};

        SLANG_RETURN_ON_FAIL(dxResource->Map(0, &readRange, reinterpret_cast<void**>(&data)));

        auto blob = OwnedBlob::create(bufferSize);
        memcpy((void*)blob->getBufferPointer(), data, bufferSize);
        dxResource->Unmap(0, nullptr);

        returnComPtr(outBlob, blob);
        return SLANG_OK;
    }
}

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    TextureDesc srcDesc = fixupTextureDesc(desc);
    D3D12_RESOURCE_DESC resourceDesc = {};
    initTextureDesc(resourceDesc, srcDesc);
    auto allocInfo = m_device->GetResourceAllocationInfo(0, 1, &resourceDesc);
    *outSize = (Size)allocInfo.SizeInBytes;
    *outAlignment = (Size)allocInfo.Alignment;
    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Size* outAlignment)
{
    *outAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    return SLANG_OK;
}

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    // Description of uploading on Dx12
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn899215%28v=vs.85%29.aspx

    TextureDesc srcDesc = fixupTextureDesc(descIn);

    D3D12_RESOURCE_DESC resourceDesc = {};
    initTextureDesc(resourceDesc, srcDesc);

    RefPtr<TextureImpl> texture(new TextureImpl(this, srcDesc));

    // Create the target resource
    {
        D3D12_HEAP_PROPERTIES heapProps;

        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        if (descIn.isShared)
            flags |= D3D12_HEAP_FLAG_SHARED;

        D3D12_CLEAR_VALUE clearValue;
        D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
        clearValue.Format = resourceDesc.Format;
        if (descIn.optimalClearValue)
        {
            memcpy(clearValue.Color, &descIn.optimalClearValue->color, sizeof(clearValue.Color));
            clearValue.DepthStencil.Depth = descIn.optimalClearValue->depthStencil.depth;
            clearValue.DepthStencil.Stencil = descIn.optimalClearValue->depthStencil.stencil;
            clearValuePtr = &clearValue;
        }
        if ((resourceDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            ) == 0)
        {
            clearValuePtr = nullptr;
        }
        if (isTypelessDepthFormat(resourceDesc.Format))
        {
            clearValuePtr = nullptr;
        }
        SLANG_RETURN_ON_FAIL(
            texture->m_resource
                .initCommitted(m_device, heapProps, flags, resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, clearValuePtr)
        );

        if (srcDesc.label)
        {
            texture->m_resource.setDebugName(srcDesc.label);
        }
    }

    // Calculate the layout
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
    layouts.resize(srcDesc.mipLevelCount);
    std::vector<uint64_t> mipRowSizeInBytes;
    mipRowSizeInBytes.resize(srcDesc.mipLevelCount);
    std::vector<uint32_t> mipNumRows;
    mipNumRows.resize(srcDesc.mipLevelCount);

    // NOTE! This is just the size for one array upload -> not for the whole texture
    uint64_t requiredSize = 0;
    m_device->GetCopyableFootprints(
        &resourceDesc,
        0,
        srcDesc.mipLevelCount,
        0,
        layouts.data(),
        mipNumRows.data(),
        mipRowSizeInBytes.data(),
        &requiredSize
    );

    // Sub resource indexing
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn705766(v=vs.85).aspx#subresource_indexing
    if (initData)
    {
        // Create the upload texture
        D3D12Resource uploadTexture;

        {
            D3D12_HEAP_PROPERTIES heapProps;

            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC uploadResourceDesc;

            uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            uploadResourceDesc.Width = requiredSize;
            uploadResourceDesc.Height = 1;
            uploadResourceDesc.DepthOrArraySize = 1;
            uploadResourceDesc.MipLevels = 1;
            uploadResourceDesc.SampleDesc.Count = 1;
            uploadResourceDesc.SampleDesc.Quality = 0;
            uploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            uploadResourceDesc.Alignment = 0;

            SLANG_RETURN_ON_FAIL(uploadTexture.initCommitted(
                m_device,
                heapProps,
                D3D12_HEAP_FLAG_NONE,
                uploadResourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr
            ));

            uploadTexture.setDebugName(L"TextureUpload");
        }
        // Get the pointer to the upload resource
        ID3D12Resource* uploadResource = uploadTexture;

        int subresourceIndex = 0;
        int arrayLayerCount = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);
        for (int arrayIndex = 0; arrayIndex < arrayLayerCount; arrayIndex++)
        {
            uint8_t* p;
            uploadResource->Map(0, nullptr, reinterpret_cast<void**>(&p));

            for (int j = 0; j < srcDesc.mipLevelCount; ++j)
            {
                auto srcSubresource = initData[subresourceIndex + j];

                const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[j];
                const D3D12_SUBRESOURCE_FOOTPRINT& footprint = layout.Footprint;

                Extents mipSize = calcMipSize(srcDesc.size, j);
                if (rhiIsCompressedFormat(descIn.format))
                {
                    mipSize.width = int(D3DUtil::calcAligned(mipSize.width, 4));
                    mipSize.height = int(D3DUtil::calcAligned(mipSize.height, 4));
                }

                SLANG_RHI_ASSERT(
                    footprint.Width == mipSize.width && footprint.Height == mipSize.height &&
                    footprint.Depth == mipSize.depth
                );

                auto mipRowSize = mipRowSizeInBytes[j];

                const ptrdiff_t dstMipRowPitch = ptrdiff_t(footprint.RowPitch);
                const ptrdiff_t srcMipRowPitch = ptrdiff_t(srcSubresource.strideY);

                const ptrdiff_t dstMipLayerPitch = ptrdiff_t(footprint.RowPitch * footprint.Height);
                const ptrdiff_t srcMipLayerPitch = ptrdiff_t(srcSubresource.strideZ);

                // Our outer loop will copy the depth layers one at a time.
                //
                const uint8_t* srcLayer = (const uint8_t*)srcSubresource.data;
                uint8_t* dstLayer = p + layouts[j].Offset;
                for (int l = 0; l < mipSize.depth; l++)
                {
                    // Our inner loop will copy the rows one at a time.
                    //
                    const uint8_t* srcRow = srcLayer;
                    uint8_t* dstRow = dstLayer;
                    int j = rhiIsCompressedFormat(descIn.format) ? 4 : 1; // BC compressed formats are organized into
                                                                          // 4x4 blocks
                    for (int k = 0; k < mipSize.height; k += j)
                    {
                        ::memcpy(dstRow, srcRow, (Size)mipRowSize);

                        srcRow += srcMipRowPitch;
                        dstRow += dstMipRowPitch;
                    }

                    srcLayer += srcMipLayerPitch;
                    dstLayer += dstMipLayerPitch;
                }

                // SLANG_RHI_ASSERT(srcRow == (const uint8_t*)(srcMip.getBuffer() + srcMip.getCount()));
            }
            uploadResource->Unmap(0, nullptr);

            auto encodeInfo = encodeResourceCommands();
            for (int mipIndex = 0; mipIndex < srcDesc.mipLevelCount; ++mipIndex)
            {
                // https://msdn.microsoft.com/en-us/library/windows/desktop/dn903862(v=vs.85).aspx

                D3D12_TEXTURE_COPY_LOCATION src;
                src.pResource = uploadTexture;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint = layouts[mipIndex];

                D3D12_TEXTURE_COPY_LOCATION dst;
                dst.pResource = texture->m_resource;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = subresourceIndex;
                encodeInfo.d3dCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                subresourceIndex++;
            }

            // Block - waiting for copy to complete (so can drop upload texture)
            submitResourceCommandsAndWait(encodeInfo);
        }
    }
    {
        auto encodeInfo = encodeResourceCommands();
        {
            D3D12BarrierSubmitter submitter(encodeInfo.d3dCommandList);
            texture->m_resource.transition(D3D12_RESOURCE_STATE_COPY_DEST, texture->m_defaultState, submitter);
        }
        submitResourceCommandsAndWait(encodeInfo);
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture)
{
    RefPtr<TextureImpl> texture(new TextureImpl(this, srcDesc));

    if (handle.type == NativeHandleType::D3D12Resource)
    {
        texture->m_resource.setResource((ID3D12Resource*)handle.value);
    }
    else
    {
        return SLANG_FAIL;
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    BufferDesc srcDesc = fixupBufferDesc(descIn);

    RefPtr<BufferImpl> buffer(new BufferImpl(this, srcDesc));

    D3D12_RESOURCE_DESC bufferDesc;
    initBufferDesc(descIn.size, bufferDesc);

    bufferDesc.Flags |= calcResourceFlags(srcDesc.usage);

    const D3D12_RESOURCE_STATES initialState = buffer->m_defaultState;
    SLANG_RETURN_ON_FAIL(createBuffer(
        bufferDesc,
        initData,
        srcDesc.size,
        initialState,
        buffer->m_resource,
        descIn.isShared,
        descIn.memoryType
    ));

    if (srcDesc.label)
    {
        buffer->m_resource.setDebugName(srcDesc.label);
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    RefPtr<BufferImpl> buffer(new BufferImpl(this, srcDesc));

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

Result DeviceImpl::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    D3D12_FILTER_REDUCTION_TYPE dxReduction = translateFilterReduction(desc.reductionOp);
    D3D12_FILTER dxFilter;
    if (desc.maxAnisotropy > 1)
    {
        dxFilter = D3D12_ENCODE_ANISOTROPIC_FILTER(dxReduction);
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

    auto& samplerHeap = m_cpuSamplerHeap;

    D3D12Descriptor cpuDescriptor;
    samplerHeap->allocate(&cpuDescriptor);
    m_device->CreateSampler(&dxDesc, cpuDescriptor.cpuHandle);

    // TODO: We really ought to have a free-list of sampler-heap
    // entries that we check before we go to the heap, and then
    // when we are done with a sampler we simply add it to the free list.
    //
    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(desc);
    samplerImpl->m_allocator = samplerHeap;
    samplerImpl->m_descriptor = cpuDescriptor;
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(desc);
    view->m_texture = static_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Unknown)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

Result DeviceImpl::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT featureData;
    featureData.Format = D3DUtil::getMapFormat(format);
    SLANG_RETURN_ON_FAIL(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &featureData, sizeof(featureData))
    );

    FormatSupport support = FormatSupport::None;

    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER)
        support = support | FormatSupport::Buffer;
    if (featureData.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                                D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE))
        support = support | FormatSupport::Texture;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)
        support = support | FormatSupport::DepthStencil;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)
        support = support | FormatSupport::RenderTarget;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)
        support = support | FormatSupport::Blendable;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER)
        support = support | FormatSupport::IndexBuffer;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER)
        support = support | FormatSupport::VertexBuffer;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
        support = support | FormatSupport::ShaderLoad;
    if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
        support = support | FormatSupport::ShaderSample;
    if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD)
        support = support | FormatSupport::ShaderAtomic;
    if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
        support = support | FormatSupport::ShaderUavLoad;
    if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)
        support = support | FormatSupport::ShaderUavStore;

    *outFormatSupport = support;
    return SLANG_OK;
}

Result DeviceImpl::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    RefPtr<InputLayoutImpl> layout(new InputLayoutImpl);

    // Work out a buffer size to hold all text
    Size textSize = 0;
    auto inputElementCount = desc.inputElementCount;
    auto inputElements = desc.inputElements;
    auto vertexStreamCount = desc.vertexStreamCount;
    auto vertexStreams = desc.vertexStreams;
    for (int i = 0; i < Int(inputElementCount); ++i)
    {
        const char* text = inputElements[i].semanticName;
        textSize += text ? (::strlen(text) + 1) : 0;
    }
    layout->m_text.resize(textSize);
    char* textPos = layout->m_text.data();

    std::vector<D3D12_INPUT_ELEMENT_DESC>& elements = layout->m_elements;
    SLANG_RHI_ASSERT(inputElementCount > 0);
    elements.resize(inputElementCount);

    for (Int i = 0; i < inputElementCount; ++i)
    {
        const InputElementDesc& srcEle = inputElements[i];
        const auto& srcStream = vertexStreams[srcEle.bufferSlotIndex];
        D3D12_INPUT_ELEMENT_DESC& dstEle = elements[i];

        // Add text to the buffer
        const char* semanticName = srcEle.semanticName;
        if (semanticName)
        {
            const int len = int(::strlen(semanticName));
            ::memcpy(textPos, semanticName, len + 1);
            semanticName = textPos;
            textPos += len + 1;
        }

        dstEle.SemanticName = semanticName;
        dstEle.SemanticIndex = (UINT)srcEle.semanticIndex;
        dstEle.Format = D3DUtil::getMapFormat(srcEle.format);
        dstEle.InputSlot = (UINT)srcEle.bufferSlotIndex;
        dstEle.AlignedByteOffset = (UINT)srcEle.offset;
        dstEle.InputSlotClass = D3DUtil::getInputSlotClass(srcStream.slotClass);
        dstEle.InstanceDataStepRate = (UINT)srcStream.instanceDataStepRate;
    }

    auto& vertexStreamStrides = layout->m_vertexStreamStrides;
    vertexStreamStrides.resize(vertexStreamCount);
    for (GfxIndex i = 0; i < vertexStreamCount; ++i)
    {
        vertexStreamStrides[i] = (UINT)vertexStreams[i].stride;
    }

    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
}

Result DeviceImpl::readBuffer(IBuffer* bufferIn, Offset offset, Size size, ISlangBlob** outBlob)
{

    BufferImpl* buffer = static_cast<BufferImpl*>(bufferIn);

    const Size bufferSize = buffer->m_desc.size;

    // This will be slow!!! - it blocks CPU on GPU completion
    D3D12Resource& resource = buffer->m_resource;

    D3D12Resource stageBuf;
    if (buffer->m_desc.memoryType != MemoryType::ReadBack)
    {
        auto encodeInfo = encodeResourceCommands();

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
        encodeInfo.d3dCommandList->CopyBufferRegion(stageBuf, 0, resource, offset, size);

        // Wait until complete
        submitResourceCommandsAndWait(encodeInfo);
    }

    D3D12Resource& stageBufRef = buffer->m_desc.memoryType != MemoryType::ReadBack ? stageBuf : resource;

    // Map and copy
    auto blob = OwnedBlob::create(size);
    {
        UINT8* data;
        D3D12_RANGE readRange = {0, size};

        SLANG_RETURN_ON_FAIL(stageBufRef.getResource()->Map(0, &readRange, reinterpret_cast<void**>(&data)));

        // Copy to memory buffer
        ::memcpy((void*)blob->getBufferPointer(), data, size);

        stageBufRef.getResource()->Unmap(0, nullptr);
    }
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
    shaderProgram->init(desc);
    ComPtr<ID3DBlob> d3dDiagnosticBlob;
    auto rootShaderLayoutResult = RootShaderObjectLayoutImpl::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootObjectLayout.writeRef(),
        d3dDiagnosticBlob.writeRef()
    );
    if (!SLANG_SUCCEEDED(rootShaderLayoutResult))
    {
        if (outDiagnosticBlob && d3dDiagnosticBlob)
        {
            auto diagnosticBlob =
                OwnedBlob::create(d3dDiagnosticBlob->GetBufferPointer(), d3dDiagnosticBlob->GetBufferSize());
            returnComPtr(outDiagnosticBlob, diagnosticBlob);
        }
        return rootShaderLayoutResult;
    }

    if (!shaderProgram->isSpecializable())
    {
        SLANG_RETURN_ON_FAIL(shaderProgram->compileShaders(this));
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

Result DeviceImpl::createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectImpl> shaderObject;
    SLANG_RETURN_ON_FAIL(
        ShaderObjectImpl::create(this, reinterpret_cast<ShaderObjectLayoutImpl*>(layout), shaderObject.writeRef())
    );
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result DeviceImpl::createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    auto result = createShaderObject(layout, outObject);
    SLANG_RETURN_ON_FAIL(result);
    static_cast<ShaderObjectImpl*>(*outObject)->m_isMutable = true;
    return result;
}

Result DeviceImpl::createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    RefPtr<MutableRootShaderObjectImpl> result = new MutableRootShaderObjectImpl();
    result->init(this);
    auto programImpl = static_cast<ShaderProgramImpl*>(program);
    result->resetImpl(this, programImpl->m_rootObjectLayout, m_cpuViewHeap.Ptr(), m_cpuSamplerHeap.Ptr(), true);
    returnComPtr(outObject, result);
    return SLANG_OK;
}

Result DeviceImpl::createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outShaderTable)
{
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl();
    result->m_device = this;
    result->init(desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
}

Result DeviceImpl::createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline)
{
    RefPtr<PipelineImpl> pipelineImpl = new PipelineImpl(this);
    pipelineImpl->init(desc);
    returnComPtr(outPipeline, pipelineImpl);
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline)
{
    RefPtr<PipelineImpl> pipelineImpl = new PipelineImpl(this);
    pipelineImpl->init(desc);
    returnComPtr(outPipeline, pipelineImpl);
    return SLANG_OK;
}

DeviceImpl::ResourceCommandRecordInfo DeviceImpl::encodeResourceCommands()
{
    ResourceCommandRecordInfo info;
    m_resourceCommandTransientHeap->createCommandBuffer(info.commandBuffer.writeRef());
    info.d3dCommandList = static_cast<CommandBufferImpl*>(info.commandBuffer.get())->m_cmdList;
    return info;
}

void DeviceImpl::submitResourceCommandsAndWait(const DeviceImpl::ResourceCommandRecordInfo& info)
{
    info.commandBuffer->close();
    m_resourceCommandQueue->executeCommandBuffer(info.commandBuffer);
    m_resourceCommandTransientHeap->finish();
    m_resourceCommandTransientHeap->synchronizeAndReset();
}

void DeviceImpl::processExperimentalFeaturesDesc(SharedLibraryHandle d3dModule, void* inDesc)
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
        getDebugCallback()->handleMessage(
            DebugMessageType::Warning,
            DebugMessageSource::Layer,
            "cannot enable D3D12 experimental features, 'D3D12EnableExperimentalFeatures' function "
            "not found."
        );
        return;
    }
    if (!SLANG_SUCCEEDED(enableExperimentalFeaturesFunc(
            desc.featureCount,
            (IID*)desc.featureIIDs,
            desc.configurationStructs,
            desc.configurationStructSizes
        )))
    {
        getDebugCallback()->handleMessage(
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
        RefPtr<PlainBufferProxyQueryPoolImpl> queryPoolImpl = new PlainBufferProxyQueryPoolImpl();
        uint32_t stride = 8;
        if (desc.type == QueryType::AccelerationStructureSerializedSize)
            stride = 16;
        SLANG_RETURN_ON_FAIL(queryPoolImpl->init(desc, this, stride));
        returnComPtr(outState, queryPoolImpl);
        return SLANG_OK;
    }
    default:
    {
        RefPtr<QueryPoolImpl> queryPoolImpl = new QueryPoolImpl();
        SLANG_RETURN_ON_FAIL(queryPoolImpl->init(desc, this));
        returnComPtr(outState, queryPoolImpl);
        return SLANG_OK;
    }
    }
}

Result DeviceImpl::createFence(const FenceDesc& desc, IFence** outFence)
{
    RefPtr<FenceImpl> fence = new FenceImpl();
    SLANG_RETURN_ON_FAIL(fence->init(this, desc));
    returnComPtr(outFence, fence);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    GfxCount fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    short_vector<HANDLE> waitHandles;
    for (GfxCount i = 0; i < fenceCount; ++i)
    {
        auto fenceImpl = static_cast<FenceImpl*>(fences[i]);
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

Result DeviceImpl::getAccelerationStructurePrebuildInfo(
    const IAccelerationStructure::BuildInputs& buildInputs,
    IAccelerationStructure::PrebuildInfo* outPrebuildInfo
)
{
    if (!m_device5)
        return SLANG_E_NOT_AVAILABLE;

    D3DAccelerationStructureInputsBuilder inputsBuilder;
    SLANG_RETURN_ON_FAIL(inputsBuilder.build(buildInputs, getDebugCallback()));

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputsBuilder.desc, &prebuildInfo);

    outPrebuildInfo->resultDataMaxSize = (Size)prebuildInfo.ResultDataMaxSizeInBytes;
    outPrebuildInfo->scratchDataSize = (Size)prebuildInfo.ScratchDataSizeInBytes;
    outPrebuildInfo->updateScratchDataSize = (Size)prebuildInfo.UpdateScratchDataSizeInBytes;
    return SLANG_OK;
}

Result DeviceImpl::createAccelerationStructure(
    const IAccelerationStructure::CreateDesc& desc,
    IAccelerationStructure** outAS
)
{
#if SLANG_RHI_DXR
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    result->m_device5 = m_device5;
    result->m_buffer = static_cast<BufferImpl*>(desc.buffer);
    result->m_size = desc.size;
    result->m_offset = desc.offset;
    SLANG_RETURN_ON_FAIL(m_cpuViewHeap->allocate(&result->m_descriptor));
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = result->m_buffer->getDeviceAddress() + desc.offset;
    m_device->CreateShaderResourceView(nullptr, &srvDesc, result->m_descriptor.cpuHandle);
    returnComPtr(outAS, result);
    return SLANG_OK;
#else
    *outAS = nullptr;
    return SLANG_FAIL;
#endif
}

Result DeviceImpl::createRayTracingPipeline(const RayTracingPipelineDesc& inDesc, IPipeline** outPipeline)
{
    if (!m_device5)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    RefPtr<RayTracingPipelineImpl> pipelineImpl = new RayTracingPipelineImpl(this);
    pipelineImpl->init(inDesc);
    returnComPtr(outPipeline, pipelineImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTransientResourceHeapImpl(
    ITransientResourceHeap::Flags::Enum flags,
    Size constantBufferSize,
    uint32_t viewDescriptors,
    uint32_t samplerDescriptors,
    TransientResourceHeapImpl** outHeap
)
{
    RefPtr<TransientResourceHeapImpl> result = new TransientResourceHeapImpl();
    ITransientResourceHeap::Desc desc = {};
    desc.flags = flags;
    desc.samplerDescriptorCount = samplerDescriptors;
    desc.constantBufferSize = constantBufferSize;
    desc.constantBufferDescriptorCount = viewDescriptors;
    desc.accelerationStructureDescriptorCount = viewDescriptors;
    desc.srvDescriptorCount = viewDescriptors;
    desc.uavDescriptorCount = viewDescriptors;
    SLANG_RETURN_ON_FAIL(result->init(desc, this, viewDescriptors, samplerDescriptors));
    returnRefPtrMove(outHeap, result);
    return SLANG_OK;
}

Result DeviceImpl::createCommandQueueImpl(CommandQueueImpl** outQueue)
{
    int queueIndex = m_queueIndexAllocator.alloc(1);
    // If we run out of queue index space, then the user is requesting too many queues.
    if (queueIndex == -1)
        return SLANG_FAIL;

    RefPtr<CommandQueueImpl> queue = new CommandQueueImpl();
    SLANG_RETURN_ON_FAIL(queue->init(this, (uint32_t)queueIndex));
    returnRefPtrMove(outQueue, queue);
    return SLANG_OK;
}

void* DeviceImpl::loadProc(SharedLibraryHandle module, char const* name)
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
    m_shaderObjectLayoutCache = decltype(m_shaderObjectLayoutCache)();
}

} // namespace rhi::d3d12
