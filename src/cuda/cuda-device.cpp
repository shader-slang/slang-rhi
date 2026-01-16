#include "cuda-device.h"
#include "cuda-command.h"
#include "cuda-buffer.h"
#include "cuda-pipeline.h"
#include "cuda-query.h"
#include "cuda-sampler.h"
#include "cuda-shader-object-layout.h"
#include "cuda-shader-object.h"
#include "cuda-shader-program.h"
#include "cuda-texture.h"
#include "cuda-acceleration-structure.h"
#include "cuda-shader-table.h"
#include "cuda-utils.h"
#include "cuda-heap.h"

namespace rhi::cuda {

struct ComputeCapabilityInfo
{
    int major;
    int minor;
    Capability capability;
};

// List of compute capabilities. This is in order from lowest to highest.
// Note: This currently only contains versions exposed as a Slang capability.
static ComputeCapabilityInfo kKnownComputeCapabilities[] = {
#define COMPUTE_CAPABILITY(major, minor) {major, minor, Capability::_cuda_sm_##major##_##minor}
    COMPUTE_CAPABILITY(1, 0),
    COMPUTE_CAPABILITY(2, 0),
    COMPUTE_CAPABILITY(3, 0),
    COMPUTE_CAPABILITY(3, 5),
    COMPUTE_CAPABILITY(4, 0),
    COMPUTE_CAPABILITY(5, 0),
    COMPUTE_CAPABILITY(6, 0),
    COMPUTE_CAPABILITY(7, 0),
    COMPUTE_CAPABILITY(8, 0),
    COMPUTE_CAPABILITY(9, 0),
#undef COMPUTE_CAPABILITY
};

inline int calcSMCountPerMultiProcessor(int major, int minor)
{
    // Defines for GPU Architecture types (using the SM version to determine
    // the # of cores per SM
    struct SMInfo
    {
        int sm; // 0xMm (hexadecimal notation), M = SM Major version, and m = SM minor version
        int coreCount;
    };

    static const SMInfo infos[] = {
        {0x30, 192},
        {0x32, 192},
        {0x35, 192},
        {0x37, 192},
        {0x50, 128},
        {0x52, 128},
        {0x53, 128},
        {0x60, 64},
        {0x61, 128},
        {0x62, 128},
        {0x70, 64},
        {0x72, 64},
        {0x75, 64}
    };

    const int sm = ((major << 4) + minor);
    for (size_t i = 0; i < SLANG_COUNT_OF(infos); ++i)
    {
        if (infos[i].sm == sm)
        {
            return infos[i].coreCount;
        }
    }

    const auto& last = infos[SLANG_COUNT_OF(infos) - 1];

    // It must be newer presumably
    SLANG_RHI_ASSERT(sm > last.sm);

    // Default to the last entry
    return last.coreCount;
}

inline Result findMaxFlopsDeviceIndex(int* outDeviceIndex)
{
    int smPerMultiproc = 0;
    int maxPerfDevice = -1;
    int deviceCount = 0;

    uint64_t maxComputePerf = 0;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));

    // Find the best CUDA capable GPU device
    for (int currentDevice = 0; currentDevice < deviceCount; ++currentDevice)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, currentDevice));
        int computeMode = -1, major = 0, minor = 0;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&computeMode, CU_DEVICE_ATTRIBUTE_COMPUTE_MODE, device));
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device));
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device));

        // If this GPU is not running on Compute Mode prohibited,
        // then we can add it to the list
        if (computeMode != CU_COMPUTEMODE_PROHIBITED)
        {
            if (major == 9999 && minor == 9999)
            {
                smPerMultiproc = 1;
            }
            else
            {
                smPerMultiproc = calcSMCountPerMultiProcessor(major, minor);
            }

            int multiProcessorCount = 0, clockRate = 0;
            SLANG_CUDA_RETURN_ON_FAIL(
                cuDeviceGetAttribute(&multiProcessorCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device)
            );
            SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&clockRate, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device));
            uint64_t compute_perf = uint64_t(multiProcessorCount) * smPerMultiproc * clockRate;

            if (compute_perf > maxComputePerf)
            {
                maxComputePerf = compute_perf;
                maxPerfDevice = currentDevice;
            }
        }
    }

    if (maxPerfDevice < 0)
    {
        return SLANG_FAIL;
    }

    *outDeviceIndex = maxPerfDevice;
    return SLANG_OK;
}

inline Result getAdaptersImpl(std::vector<AdapterImpl>& outAdapters)
{
    if (!rhiCudaDriverApiInit())
    {
        return SLANG_FAIL;
    }

    SLANG_CUDA_RETURN_ON_FAIL(cuInit(0));

    int deviceCount;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));

    for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, deviceIndex));

        AdapterInfo info = {};
        info.deviceType = DeviceType::CUDA;
        info.adapterType = AdapterType::Discrete;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetName(info.name, sizeof(info.name), device));
        info.luid = getAdapterLUID(deviceIndex);

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_deviceIndex = deviceIndex;
        outAdapters.push_back(adapter);
    }

    // Find the max flops adapter and mark it as the default one.
    if (!outAdapters.empty())
    {
        int defaultDeviceIndex = 0;
        SLANG_RETURN_ON_FAIL(findMaxFlopsDeviceIndex(&defaultDeviceIndex));
        SLANG_RHI_ASSERT(defaultDeviceIndex >= 0 && defaultDeviceIndex < (int)outAdapters.size());
        outAdapters[defaultDeviceIndex].m_isDefault = true;
    }

    return SLANG_OK;
}

std::vector<AdapterImpl>& getAdapters()
{
    static std::vector<AdapterImpl> adapters;
    static Result initResult = getAdaptersImpl(adapters);
    SLANG_UNUSED(initResult);
    return adapters;
}

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    if (m_ctx.context)
    {
        SLANG_CUDA_CTX_SCOPE(this);

        m_shaderCache.free();
        m_uploadHeap.release();
        m_readbackHeap.release();
        m_clearEngine.release();

        m_queue.setNull();
        m_deviceMemHeap.setNull();
        m_hostMemHeap.setNull();

        m_ctx.optixContext.setNull();
    }

    if (m_ownsContext && m_ctx.context)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuDevicePrimaryCtxRelease(m_ctx.device));
    }
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::CUdevice;
    outHandles->handles[0].value = m_ctx.device;
    outHandles->handles[1] = {};
    if (m_ctx.optixContext)
    {
        outHandles->handles[1].type = NativeHandleType::OptixDeviceContext;
        outHandles->handles[1].value = (uint64_t)m_ctx.optixContext->getOptixDeviceContext();
    }
    outHandles->handles[2].type = NativeHandleType::CUcontext;
    outHandles->handles[2].value = (uint64_t)m_ctx.context;
    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    if (!rhiCudaDriverApiInit())
    {
        printError("Failed to initialize CUDA driver API.");
        return SLANG_FAIL;
    }

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuInit(0), this);

    void* existingOptixDeviceContext = nullptr;

    for (const auto& handle : desc.existingDeviceHandles.handles)
    {
        if (handle.type == NativeHandleType::CUdevice)
        {
            m_ctx.device = (CUdevice)handle.value;
        }
        else if (handle.type == NativeHandleType::CUcontext)
        {
            m_ctx.context = (CUcontext)handle.value;
        }
        else if (handle.type == NativeHandleType::OptixDeviceContext)
        {
            existingOptixDeviceContext = (void*)handle.value;
        }
    }

    if (m_ctx.context)
    {
        // User provided context. Get the device from it to be sure it matches.
        SLANG_CUDA_CTX_SCOPE(this);
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuCtxGetDevice(&m_ctx.device), this);
    }
    else if (m_ctx.device >= 0)
    {
        // User provided device. Create a context for it.
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDevicePrimaryCtxRetain(&m_ctx.context, m_ctx.device), this);
        m_ownsContext = true;
    }
    else
    {
        // User provided no external handles, so we need to create a device and context.
        AdapterImpl* adapter = nullptr;
        SLANG_RETURN_ON_FAIL(selectAdapter(this, getAdapters(), desc, adapter));
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDeviceGet(&m_ctx.device, adapter->m_deviceIndex), this);
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDevicePrimaryCtxRetain(&m_ctx.context, m_ctx.device), this);
        m_ownsContext = true;
    }

    SLANG_CUDA_CTX_SCOPE(this);

    // Initialize device info
    {
        m_info.deviceType = DeviceType::CUDA;
        m_info.apiName = "CUDA";
        char deviceName[256];
        SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetName(deviceName, sizeof(deviceName), m_ctx.device));
        m_adapterName = deviceName;
        m_info.adapterName = m_adapterName.data();
        m_info.adapterLUID = getAdapterLUID(m_ctx.device);
        m_info.timestampFrequency = 1000000;
    }

    // Query device limits
    {
        auto getAttribute = [&](CUdevice_attribute attribute) -> int
        {
            int value = 0;
            cuDeviceGetAttribute(&value, attribute, m_ctx.device);
            return value;
        };

        DeviceLimits limits = {};

        size_t totalMem = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDeviceTotalMem(&totalMem, m_ctx.device), this);
        limits.maxBufferSize = totalMem;

        limits.maxTextureDimension1D = getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_WIDTH);
        limits.maxTextureDimension2D = min({
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_WIDTH),
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_HEIGHT),
        });
        limits.maxTextureDimension3D = min({
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_WIDTH),
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_HEIGHT),
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_DEPTH),
        });
        limits.maxTextureDimensionCube = getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_WIDTH);
        limits.maxTextureLayers = min({
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS),
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS),
        });

        limits.maxComputeThreadsPerGroup = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
        limits.maxComputeThreadGroupSize[0] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X);
        limits.maxComputeThreadGroupSize[1] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y);
        limits.maxComputeThreadGroupSize[2] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z);
        limits.maxComputeDispatchThreadGroups[0] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X);
        limits.maxComputeDispatchThreadGroups[1] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y);
        limits.maxComputeDispatchThreadGroups[2] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z);

        m_info.limits = limits;
    }

    // Initialize features & capabilities
    addFeature(Feature::HardwareDevice);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Bindless);
#if SLANG_RHI_ENABLE_VULKAN
    // Supports surface/swapchain (implemented in Vulkan).
    addFeature(Feature::Surface);
#endif
    addFeature(Feature::CombinedTextureSampler);
    addFeature(Feature::TimestampQuery);
    addFeature(Feature::RealtimeClock);
    // Not clear how to detect half support on CUDA. For now we'll assume we have it
    addFeature(Feature::Half);
    addFeature(Feature::Pointer);

    addCapability(Capability::cuda);

    // Detect supported compute capabilities
    {
        int major = 0, minor = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, m_ctx.device),
            this
        );
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, m_ctx.device),
            this
        );
        for (const auto& cc : kKnownComputeCapabilities)
        {
            if ((major == cc.major && minor >= cc.minor) || major > cc.major)
            {
                addCapability(cc.capability);
            }
        }
    }

    optix::ContextDesc optixContextDesc = {};
    optixContextDesc.device = this;
    optixContextDesc.requiredOptixVersion = desc.requiredOptixVersion;
    optixContextDesc.existingOptixDeviceContext = existingOptixDeviceContext;
    optixContextDesc.enableRayTracingValidation = desc.enableRayTracingValidation;
    if (SLANG_SUCCEEDED(optix::createContext(optixContextDesc, m_ctx.optixContext.writeRef())))
    {
        addFeature(Feature::AccelerationStructure);
        addFeature(Feature::RayTracing);
        uint32_t optixVersion = m_ctx.optixContext->getOptixVersion();
        m_info.optixVersion = optixVersion;
        if (optixVersion >= 80100)
        {
            addFeature(Feature::ShaderExecutionReordering);
        }
        if (optixVersion >= 90000)
        {
            addFeature(Feature::AccelerationStructureSpheres);
            addFeature(Feature::AccelerationStructureLinearSweptSpheres);
            if (m_ctx.optixContext->getClusterAccelerationSupport())
            {
                addFeature(Feature::ClusterAccelerationStructure);
            }
            if (m_ctx.optixContext->getCooperativeVectorSupport())
            {
                addFeature(Feature::CooperativeVector);
                // addCapability(Capability::optix_coopvec);
            }
        }
    }

    // Initialize slang context
    SLANG_RETURN_ON_FAIL(m_slangContext.initialize(
        desc.slang,
        SLANG_PTX,
        nullptr,
        getCapabilities(),
        std::array{slang::PreprocessorMacroDesc{"__CUDA__", "1"}}
    ));

    // Initialize format support table
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        FormatSupport formatSupport = FormatSupport::None;
        if (isFormatSupported(format))
        {
            formatSupport |= FormatSupport::CopySource;
            formatSupport |= FormatSupport::CopyDestination;
            formatSupport |= FormatSupport::Texture;
            formatSupport |= FormatSupport::ShaderLoad;
            formatSupport |= FormatSupport::ShaderSample;
            formatSupport |= FormatSupport::ShaderUavLoad;
            formatSupport |= FormatSupport::ShaderUavStore;
            formatSupport |= FormatSupport::ShaderAtomic;
        }
        m_formatSupport[formatIndex] = formatSupport;
    }

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    SLANG_RETURN_ON_FAIL(m_queue->init());
    m_queue->setInternalReferenceCount(1);

    // Create 2 heaps. On CUDA both Upload and ReadBack just use host memory,
    // so we only need one for DeviceLocal and one for Upload/ReadBack.
    ComPtr<IHeap> heapPtr;
    HeapDesc heapDesc = {};

    heapDesc.memoryType = MemoryType::Upload;
    heapDesc.label = "Device upload heap";
    SLANG_RETURN_ON_FAIL(createHeap(heapDesc, heapPtr.writeRef()));
    m_hostMemHeap = checked_cast<HeapImpl*>(heapPtr.get());
    m_hostMemHeap->breakStrongReferenceToDevice();

    heapDesc.memoryType = MemoryType::DeviceLocal;
    heapDesc.label = "Device local heap";
    SLANG_RETURN_ON_FAIL(createHeap(heapDesc, heapPtr.writeRef()));
    m_deviceMemHeap = checked_cast<HeapImpl*>(heapPtr.get());
    m_deviceMemHeap->breakStrongReferenceToDevice();

    // Register heaps with the base Device class for reporting
    m_globalHeaps.push_back(m_hostMemHeap);
    m_globalHeaps.push_back(m_deviceMemHeap);

    SLANG_RETURN_ON_FAIL(m_clearEngine.initialize(this));

    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    SLANG_CUDA_CTX_SCOPE(this);

    switch (desc.type)
    {
    case QueryType::Timestamp:
    {
        RefPtr<QueryPoolImpl> pool = new QueryPoolImpl(this, desc);
        SLANG_RETURN_ON_FAIL(pool->init());
        returnComPtr(outPool, pool);
        return SLANG_OK;
    }
    case QueryType::AccelerationStructureCompactedSize:
    {
        RefPtr<PlainBufferProxyQueryPoolImpl> pool = new PlainBufferProxyQueryPoolImpl(this, desc);
        SLANG_RETURN_ON_FAIL(pool->init());
        returnComPtr(outPool, pool);
        return SLANG_OK;
        break;
    }
    default:
        return SLANG_FAIL;
    }
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<ShaderObjectLayoutImpl> cudaLayout;
    cudaLayout = new ShaderObjectLayoutImpl(this, session, typeLayout);
    returnRefPtrMove(outLayout, cudaLayout);
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
    SLANG_CUDA_CTX_SCOPE(this);

    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl(this, desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    shaderProgram->m_rootObjectLayout = new RootShaderObjectLayoutImpl(this, shaderProgram->linkedProgram->getLayout());
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

void* DeviceImpl::map(IBuffer* buffer)
{
    return checked_cast<BufferImpl*>(buffer)->m_cudaMemory;
}

void DeviceImpl::unmap(IBuffer* buffer)
{
    SLANG_UNUSED(buffer);
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(this, desc);
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outLayout);
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    const SubresourceLayout& layout,
    void* outData
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    auto textureImpl = checked_cast<TextureImpl*>(texture);

    CUarray srcArray = textureImpl->m_cudaArray;
    if (textureImpl->m_cudaMipMappedArray)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuMipmappedArrayGetLevel(&srcArray, textureImpl->m_cudaMipMappedArray, mip),
            this
        );
    }

    CUDA_MEMCPY3D copyParam = {};
    copyParam.dstMemoryType = CU_MEMORYTYPE_HOST;
    copyParam.dstHost = outData;
    copyParam.dstPitch = layout.rowPitch;
    copyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    copyParam.srcArray = srcArray;
    copyParam.srcZ = layer;
    copyParam.WidthInBytes = layout.rowPitch;
    copyParam.Height = (layout.size.height + layout.blockHeight - 1) / layout.blockHeight;
    copyParam.Depth = layout.size.depth;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemcpy3D(&copyParam), this);

    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, size_t offset, size_t size, void* outData)
{
    SLANG_CUDA_CTX_SCOPE(this);

    auto bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuMemcpy((CUdeviceptr)outData, (CUdeviceptr)((uint8_t*)bufferImpl->m_cudaMemory + offset), size),
        this
    );
    return SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    return m_ctx.optixContext->getAccelerationStructureSizes(desc, outSizes);
}

Result DeviceImpl::getClusterOperationSizes(const ClusterOperationParams& params, ClusterOperationSizes* outSizes)
{
    SLANG_CUDA_CTX_SCOPE(this);

    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    return m_ctx.optixContext->getClusterOperationSizes(params, outSizes);
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&result->m_buffer, desc.size), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&result->m_propertyBuffer, 8), this);
    result->m_handle = 0;
    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
}

Result DeviceImpl::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
    if (!hasFeature(Feature::CooperativeVector))
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    if (m_cooperativeVectorProperties.empty())
    {
#define ADD_PROPERTIES(inputType, inputInterpretation, matrixInterpretation, biasInterpretation, resultType)           \
    m_cooperativeVectorProperties.push_back({                                                                          \
        CooperativeVectorComponentType::inputType,                                                                     \
        CooperativeVectorComponentType::inputInterpretation,                                                           \
        CooperativeVectorComponentType::matrixInterpretation,                                                          \
        CooperativeVectorComponentType::biasInterpretation,                                                            \
        CooperativeVectorComponentType::resultType,                                                                    \
        false /* transpose */                                                                                          \
    })
        // OptiX has hardcoded support for these cooperative vector types.
        ADD_PROPERTIES(Float16, Float16, Float16, Float16, Float16);
        ADD_PROPERTIES(Float16, FloatE4M3, FloatE4M3, Float16, Float16);
        ADD_PROPERTIES(Float16, FloatE5M2, FloatE5M2, Float16, Float16);
#undef ADD_PROPERTIES
    }
    return Device::getCooperativeVectorProperties(properties, propertiesCount);
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
    if (m_ctx.optixContext)
    {
        return m_ctx.optixContext
            ->getCooperativeVectorMatrixSize(rowCount, colCount, componentType, layout, rowColumnStride, outSize);
    }
    return SLANG_E_NOT_AVAILABLE;
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
    SLANG_CUDA_CTX_SCOPE(this);

    if (m_ctx.optixContext)
    {
        CUdeviceptr dstPtr = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&dstPtr, dstBufferSize), this);
        CUdeviceptr srcPtr = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&srcPtr, srcBufferSize), this);
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemcpyHtoD(srcPtr, srcBuffer, srcBufferSize), this);

        Result result =
            m_ctx.optixContext->convertCooperativeVectorMatrix(0, dstPtr, dstDescs, srcPtr, srcDescs, matrixCount);

        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemcpyDtoH(dstBuffer, dstPtr, dstBufferSize), this);

        return result;
    }
    return SLANG_E_NOT_AVAILABLE;
}

void DeviceImpl::customizeShaderObject(ShaderObject* shaderObject)
{
    shaderObject->m_setBindingHook = shaderObjectSetBinding;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = 1;
    return SLANG_OK;
}

} // namespace rhi::cuda

namespace rhi {

IAdapter* getCUDAAdapter(uint32_t index)
{
    std::vector<cuda::AdapterImpl>& adapters = cuda::getAdapters();
    return index < adapters.size() ? &adapters[index] : nullptr;
}

Result createCUDADevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<cuda::DeviceImpl> result = new cuda::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi
