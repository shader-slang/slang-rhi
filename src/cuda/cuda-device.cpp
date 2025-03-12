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
    int devicesProhibited = 0;

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
                smPerMultiproc = _calcSMCountPerMultiProcessor(major, minor);
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
        else
        {
            devicesProhibited++;
        }
    }

    if (maxPerfDevice < 0)
    {
        return SLANG_FAIL;
    }

    *outDeviceIndex = maxPerfDevice;
    return SLANG_OK;
}

Result DeviceImpl::_initCuda(CUDAReportStyle reportType)
{
    if (!rhiCudaDriverApiInit())
    {
        return SLANG_FAIL;
    }
    CUresult res = cuInit(0);
    SLANG_CUDA_RETURN_WITH_REPORT_ON_FAIL(res, reportType);
    return SLANG_OK;
}

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    m_queue.setNull();

    m_shaderCache.free();
    m_uploadHeap.release();
    m_readbackHeap.release();

#if SLANG_RHI_ENABLE_OPTIX
    if (m_ctx.optixContext)
    {
        optixDeviceContextDestroy(m_ctx.optixContext);
    }
#endif

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
    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_PTX, "sm_5_1", std::array{slang::PreprocessorMacroDesc{"__CUDA__", "1"}})
    );

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    SLANG_RETURN_ON_FAIL(_initCuda(kReportType));

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

    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&m_ctx.device, selectedDeviceIndex));

    SLANG_CUDA_RETURN_WITH_REPORT_ON_FAIL(cuCtxCreate(&m_ctx.context, 0, m_ctx.device), kReportType);

    // Supports ParameterBlock
    m_features.push_back("parameter-block");
#if SLANG_RHI_ENABLE_VULKAN
    // Supports surface/swapchain (implemented in Vulkan).
    m_features.push_back("surface");
#endif
    // Not clear how to detect half support on CUDA. For now we'll assume we have it
    m_features.push_back("half");
    // CUDA has support for realtime clock
    m_features.push_back("realtime-clock");
    // Allows use of a ptr like type
    m_features.push_back("has-ptr");

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

            SLANG_OPTIX_RETURN_ON_FAIL(optixDeviceContextCreate(m_ctx.context, &options, &m_ctx.optixContext));

            m_features.push_back("acceleration-structure");
            m_features.push_back("acceleration-structure-spheres");
            m_features.push_back("ray-tracing");
        }
        else
        {
            SLANG_OPTIX_HANDLE_ERROR(result);
        }
    }
#endif

    // Initialize DeviceInfo
    {
        m_info.deviceType = DeviceType::CUDA;
        m_info.apiName = "CUDA";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
        char deviceName[256];
        SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetName(deviceName, sizeof(deviceName), m_ctx.device));
        m_adapterName = deviceName;
        m_info.adapterName = m_adapterName.data();
        m_info.timestampFrequency = 1000000;
    }

    // Get device limits.
    {
        CUresult lastResult = CUDA_SUCCESS;
        auto getAttribute = [&](CUdevice_attribute attribute) -> int
        {
            int value;
            CUresult result = cuDeviceGetAttribute(&value, attribute, m_ctx.device);
            if (result != CUDA_SUCCESS)
                lastResult = result;
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
        limits.maxTextureArrayLayers = min({
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS),
            getAttribute(CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS),
        });

        // limits.maxVertexInputElements
        // limits.maxVertexInputElementOffset
        // limits.maxVertexStreams
        // limits.maxVertexStreamStride

        limits.maxComputeThreadsPerGroup = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
        limits.maxComputeThreadGroupSize[0] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X);
        limits.maxComputeThreadGroupSize[1] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y);
        limits.maxComputeThreadGroupSize[2] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z);
        limits.maxComputeDispatchThreadGroups[0] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X);
        limits.maxComputeDispatchThreadGroups[1] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y);
        limits.maxComputeDispatchThreadGroups[2] = getAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z);

        // limits.maxViewports
        // limits.maxViewportDimensions
        // limits.maxFramebufferDimensions

        // limits.maxShaderVisibleSamplers

        m_info.limits = limits;

        SLANG_CUDA_RETURN_ON_FAIL(lastResult);
    }

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);

    return SLANG_OK;
}

Result DeviceImpl::getCUDAFormat(Format format, CUarray_format* outFormat)
{
    // TODO: Expand to cover all available formats that can be supported in CUDA
    // NOTE: If adding to this, update getFormatSupport accordingly.
    switch (format)
    {
    case Format::RGBA32Float:
    case Format::RG32Float:
    case Format::R32Float:
    case Format::D32Float:
        *outFormat = CU_AD_FORMAT_FLOAT;
        return SLANG_OK;
    case Format::RGBA16Float:
    case Format::RG16Float:
    case Format::R16Float:
        *outFormat = CU_AD_FORMAT_HALF;
        return SLANG_OK;
    case Format::RGBA32Uint:
    case Format::RG32Uint:
    case Format::R32Uint:
        *outFormat = CU_AD_FORMAT_UNSIGNED_INT32;
        return SLANG_OK;
    case Format::RGBA16Uint:
    case Format::RG16Uint:
    case Format::R16Uint:
        *outFormat = CU_AD_FORMAT_UNSIGNED_INT16;
        return SLANG_OK;
    case Format::RGBA8Uint:
    case Format::RG8Uint:
    case Format::R8Uint:
    case Format::RGBA8Unorm:
        *outFormat = CU_AD_FORMAT_UNSIGNED_INT8;
        return SLANG_OK;
    case Format::RGBA32Sint:
    case Format::RG32Sint:
    case Format::R32Sint:
        *outFormat = CU_AD_FORMAT_SIGNED_INT32;
        return SLANG_OK;
    case Format::RGBA16Sint:
    case Format::RG16Sint:
    case Format::R16Sint:
        *outFormat = CU_AD_FORMAT_SIGNED_INT16;
        return SLANG_OK;
    case Format::RGBA8Sint:
    case Format::RG8Sint:
    case Format::R8Sint:
        *outFormat = CU_AD_FORMAT_SIGNED_INT8;
        return SLANG_OK;
    default:
        SLANG_RHI_ASSERT_FAILURE("Only support R32Float/RGBA8Unorm formats for now");
        return SLANG_FAIL;
    }
}

Result DeviceImpl::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_RETURN_ON_FAIL(Device::getFormatSupport(format, outFormatSupport));
    switch (format)
    {
    case Format::R8Uint:
    case Format::R8Sint:
    case Format::RG8Uint:
    case Format::RG8Sint:
    case Format::RGBA8Uint:
    case Format::RGBA8Sint:
    case Format::RGBA8Unorm:

    case Format::R16Uint:
    case Format::R16Sint:
    case Format::R16Float:
    case Format::RG16Uint:
    case Format::RG16Sint:
    case Format::RG16Float:
    case Format::RGBA16Uint:
    case Format::RGBA16Sint:
    case Format::RGBA16Float:

    case Format::R32Uint:
    case Format::R32Sint:
    case Format::R32Float:
    case Format::RG32Uint:
    case Format::RG32Sint:
    case Format::RG32Float:
    case Format::RGBA32Uint:
    case Format::RGBA32Sint:
    case Format::RGBA32Float:

    case Format::D32Float:
        break;
    default:
        // Disable formats not available in getCUDAFormat
        *outFormatSupport = (FormatSupport)0;
        break;
    }
    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
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

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
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
    uint32_t mipLevel,
    ISlangBlob** outBlob,
    Size* outRowPitch,
    Size* outPixelSize
)
{
    auto textureImpl = checked_cast<TextureImpl*>(texture);

    const TextureDesc& desc = textureImpl->m_desc;
    Extents mipSize = calcMipSize(desc.size, mipLevel);
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    size_t pixelSize = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    size_t rowPitch = mipSize.width * pixelSize;
    size_t size = mipSize.depth * mipSize.height * rowPitch;

    auto blob = OwnedBlob::create(size);

    CUarray srcArray = textureImpl->m_cudaArray;
    if (textureImpl->m_cudaMipMappedArray)
    {
        SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayGetLevel(&srcArray, textureImpl->m_cudaMipMappedArray, mipLevel));
    }

    CUDA_MEMCPY3D copyParam = {};
    copyParam.dstMemoryType = CU_MEMORYTYPE_HOST;
    copyParam.dstHost = (void*)blob->getBufferPointer();
    copyParam.dstPitch = rowPitch;
    copyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    copyParam.srcArray = srcArray;
    copyParam.srcZ = layer;
    copyParam.WidthInBytes = rowPitch;
    copyParam.Height = mipSize.height;
    copyParam.Depth = mipSize.depth;
    SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy3D(&copyParam));

    if (outRowPitch)
        *outRowPitch = rowPitch;
    if (outPixelSize)
        *outPixelSize = pixelSize;

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    auto bufferImpl = checked_cast<BufferImpl*>(buffer);

    auto blob = OwnedBlob::create(size);
    SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy(
        (CUdeviceptr)blob->getBufferPointer(),
        (CUdeviceptr)((uint8_t*)bufferImpl->m_cudaMemory + offset),
        size
    ));

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
#if SLANG_RHI_ENABLE_OPTIX
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    AccelerationStructureBuildDescConverter converter;
    SLANG_RETURN_ON_FAIL(converter.convert(desc, m_debugCallback));
    OptixAccelBufferSizes sizes;
    SLANG_OPTIX_RETURN_ON_FAIL(optixAccelComputeMemoryUsage(
        m_ctx.optixContext,
        &converter.buildOptions,
        converter.buildInputs.data(),
        converter.buildInputs.size(),
        &sizes
    ));
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
#if SLANG_RHI_ENABLE_OPTIX
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc(&result->m_buffer, desc.size));
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc(&result->m_propertyBuffer, 8));
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
