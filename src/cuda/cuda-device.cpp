#include "cuda-device.h"
#include "cuda-command.h"
#include "cuda-buffer.h"
#include "cuda-pipeline.h"
#include "cuda-query.h"
#include "cuda-shader-object-layout.h"
#include "cuda-shader-object.h"
#include "cuda-shader-program.h"
#include "cuda-texture.h"
#include "cuda-acceleration-structure.h"
#include "cuda-shader-table.h"

namespace rhi::cuda {

int DeviceImpl::_calcSMCountPerMultiProcessor(int major, int minor)
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

Result DeviceImpl::_findMaxFlopsDeviceIndex(int* outDeviceIndex)
{
    int smPerMultiproc = 0;
    int maxPerfDevice = -1;
    int deviceCount = 0;

    uint64_t maxComputePerf = 0;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDeviceGetCount(&deviceCount), this);

    // Find the best CUDA capable GPU device
    for (int currentDevice = 0; currentDevice < deviceCount; ++currentDevice)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDeviceGet(&device, currentDevice), this);
        int computeMode = -1, major = 0, minor = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&computeMode, CU_DEVICE_ATTRIBUTE_COMPUTE_MODE, device),
            this
        );
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device),
            this
        );
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device),
            this
        );

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
                smPerMultiproc = _calcSMCountPerMultiProcessor(major, minor);
            }

            int multiProcessorCount = 0, clockRate = 0;
            SLANG_CUDA_RETURN_ON_FAIL_REPORT(
                cuDeviceGetAttribute(&multiProcessorCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device),
                this
            );
            SLANG_CUDA_RETURN_ON_FAIL_REPORT(
                cuDeviceGetAttribute(&clockRate, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device),
                this
            );
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

Result DeviceImpl::_initCuda()
{
    if (!rhiCudaDriverApiInit())
    {
        printError("Failed to initialize CUDA driver API.");
        return SLANG_FAIL;
    }
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuInit(0), this);
    return SLANG_OK;
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

#if SLANG_RHI_ENABLE_OPTIX
        if (m_ctx.optixContext)
        {
            optixDeviceContextDestroy(m_ctx.optixContext);
        }
#endif
    }

    if (m_ctx.context)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuCtxDestroy(m_ctx.context));
    }
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::CUdevice;
    outHandles->handles[0].value = m_ctx.device;
#if SLANG_RHI_ENABLE_OPTIX
    outHandles->handles[1].type = NativeHandleType::OptixDeviceContext;
    outHandles->handles[1].value = (uint64_t)m_ctx.optixContext;
#else
    outHandles->handles[1] = {};
#endif
    outHandles->handles[2] = {};
    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    SLANG_RETURN_ON_FAIL(_initCuda());

    int selectedDeviceIndex = -1;
    if (desc.adapterLUID)
    {
        int deviceCount = -1;
        SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetCount(&deviceCount));
        for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
        {
            if (cuda::getAdapterLUID(deviceIndex) == *desc.adapterLUID)
            {
                selectedDeviceIndex = deviceIndex;
                break;
            }
        }
    }
    else
    {
        SLANG_RETURN_ON_FAIL(_findMaxFlopsDeviceIndex(&selectedDeviceIndex));
    }

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuDeviceGet(&m_ctx.device, selectedDeviceIndex), this);

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuCtxCreate(&m_ctx.context, 0, m_ctx.device), this);

    SLANG_CUDA_CTX_SCOPE(this);

    // Initialize device info
    {
        m_info.deviceType = DeviceType::CUDA;
        m_info.apiName = "CUDA";
        char deviceName[256];
        SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetName(deviceName, sizeof(deviceName), m_ctx.device));
        m_adapterName = deviceName;
        m_info.adapterName = m_adapterName.data();
        m_info.adapterLUID = cuda::getAdapterLUID(m_ctx.device);
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
#if SLANG_RHI_ENABLE_VULKAN
    // Supports surface/swapchain (implemented in Vulkan).
    addFeature(Feature::Surface);
#endif
    addFeature(Feature::TimestampQuery);
    addFeature(Feature::RealtimeClock);
    // Not clear how to detect half support on CUDA. For now we'll assume we have it
    addFeature(Feature::Half);
    addFeature(Feature::Pointer);

    addCapability(Capability::cuda);


#if SLANG_RHI_ENABLE_OPTIX
    {
        OptixResult result = optixInit();
        if (result == OPTIX_SUCCESS)
        {
            static auto logCallback = [](unsigned int level, const char* tag, const char* message, void* userData)
            {
                DeviceImpl* device = static_cast<DeviceImpl*>(userData);
                DebugMessageType type;
                switch (level)
                {
                case 1: // fatal
                    type = DebugMessageType::Error;
                    break;
                case 2: // error
                    type = DebugMessageType::Error;
                    break;
                case 3: // warning
                    type = DebugMessageType::Warning;
                    break;
                case 4: // print
                    type = DebugMessageType::Info;
                    break;
                default:
                    return;
                }

                char msg[4096];
                int msgSize = snprintf(msg, sizeof(msg), "[%s]: %s", tag, message);
                if (msgSize < 0)
                    return;
                else if (msgSize >= int(sizeof(msg)))
                    msg[sizeof(msg) - 1] = 0;

                device->handleMessage(type, DebugMessageSource::Driver, msg);
            };

            OptixDeviceContextOptions options = {};
            options.logCallbackFunction = logCallback;
            options.logCallbackLevel = 4;
            options.logCallbackData = this;
            options.validationMode = desc.enableRayTracingValidation ? OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL
                                                                     : OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;

            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixDeviceContextCreate(m_ctx.context, &options, &m_ctx.optixContext),
                this
            );

            addFeature(Feature::AccelerationStructure);
            addFeature(Feature::AccelerationStructureSpheres);
            addFeature(Feature::RayTracing);
        }
        else
        {
            printWarning("Failed to initialize OptiX: %s (%s)", optixGetErrorString(result), optixGetErrorName(result));
        }
    }
#endif

    // Initialize slang context
    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_PTX, "sm_7_5", std::array{slang::PreprocessorMacroDesc{"__CUDA__", "1"}})
    );

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

    SLANG_RETURN_ON_FAIL(m_clearEngine.initialize(m_debugCallback));

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

#if SLANG_RHI_ENABLE_OPTIX
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl(this, desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
#else
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outShaderTable);
    return SLANG_E_NOT_AVAILABLE;
#endif
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
    shaderProgram->init(desc);
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
    SLANG_UNUSED(desc);
    *outSampler = nullptr;
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

#if SLANG_RHI_ENABLE_OPTIX
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    AccelerationStructureBuildDescConverter converter;
    SLANG_RETURN_ON_FAIL(converter.convert(desc, m_debugCallback));
    OptixAccelBufferSizes sizes;
    SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
        optixAccelComputeMemoryUsage(
            m_ctx.optixContext,
            &converter.buildOptions,
            converter.buildInputs.data(),
            converter.buildInputs.size(),
            &sizes
        ),
        this
    );
    outSizes->accelerationStructureSize = sizes.outputSizeInBytes;
    outSizes->scratchSize = sizes.tempSizeInBytes;
    outSizes->updateScratchSize = sizes.tempUpdateSizeInBytes;

    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_CUDA_CTX_SCOPE(this);

#if SLANG_RHI_ENABLE_OPTIX
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&result->m_buffer, desc.size), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&result->m_propertyBuffer, 8), this);
    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
#else
    return SLANG_E_NOT_AVAILABLE;
#endif
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
