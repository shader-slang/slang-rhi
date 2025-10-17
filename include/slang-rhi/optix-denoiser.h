#pragma once

#include "slang-rhi.h"

#include "cuda-driver-api.h"

// This file defines a minimal subset of the OptiX API needed to use the OptiX denoiser.
// slang-rhi has support for multiple versions of OptiX via an internal abstraction layer.
// To avoid introducing a hard dependency on the OptiX SDK, we define the necessary parts of
// the OptiX API here instead of including the OptiX headers directly.
// For documentation on the OptiX API, consult the official OptiX documentation from NVIDIA:
// https://developer.nvidia.com/optix

namespace rhi::optix_denoiser {

typedef void* OptixDeviceContext;
typedef void* OptixDenoiser;

enum OptixResult
{
    OPTIX_SUCCESS = 0,
    OPTIX_ERROR_INVALID_VALUE = 7001,
    OPTIX_ERROR_HOST_OUT_OF_MEMORY = 7002,
    OPTIX_ERROR_INVALID_OPERATION = 7003,
    OPTIX_ERROR_FILE_IO_ERROR = 7004,
    OPTIX_ERROR_INVALID_FILE_FORMAT = 7005,
    OPTIX_ERROR_DISK_CACHE_INVALID_PATH = 7010,
    OPTIX_ERROR_DISK_CACHE_PERMISSION_ERROR = 7011,
    OPTIX_ERROR_DISK_CACHE_DATABASE_ERROR = 7012,
    OPTIX_ERROR_DISK_CACHE_INVALID_DATA = 7013,
    OPTIX_ERROR_LAUNCH_FAILURE = 7050,
    OPTIX_ERROR_INVALID_DEVICE_CONTEXT = 7051,
    OPTIX_ERROR_CUDA_NOT_INITIALIZED = 7052,
    OPTIX_ERROR_VALIDATION_FAILURE = 7053,
    OPTIX_ERROR_INVALID_INPUT = 7200,
    OPTIX_ERROR_INVALID_LAUNCH_PARAMETER = 7201,
    OPTIX_ERROR_INVALID_PAYLOAD_ACCESS = 7202,
    OPTIX_ERROR_INVALID_ATTRIBUTE_ACCESS = 7203,
    OPTIX_ERROR_INVALID_FUNCTION_USE = 7204,
    OPTIX_ERROR_INVALID_FUNCTION_ARGUMENTS = 7205,
    OPTIX_ERROR_PIPELINE_OUT_OF_CONSTANT_MEMORY = 7250,
    OPTIX_ERROR_PIPELINE_LINK_ERROR = 7251,
    OPTIX_ERROR_ILLEGAL_DURING_TASK_EXECUTE = 7270,
    OPTIX_ERROR_INTERNAL_COMPILER_ERROR = 7299,
    OPTIX_ERROR_DENOISER_MODEL_NOT_SET = 7300,
    OPTIX_ERROR_DENOISER_NOT_INITIALIZED = 7301,
    OPTIX_ERROR_NOT_COMPATIBLE = 7400,
    OPTIX_ERROR_PAYLOAD_TYPE_MISMATCH = 7500,
    OPTIX_ERROR_PAYLOAD_TYPE_RESOLUTION_FAILED = 7501,
    OPTIX_ERROR_PAYLOAD_TYPE_ID_INVALID = 7502,
    OPTIX_ERROR_NOT_SUPPORTED = 7800,
    OPTIX_ERROR_UNSUPPORTED_ABI_VERSION = 7801,
    OPTIX_ERROR_FUNCTION_TABLE_SIZE_MISMATCH = 7802,
    OPTIX_ERROR_INVALID_ENTRY_FUNCTION_OPTIONS = 7803,
    OPTIX_ERROR_LIBRARY_NOT_FOUND = 7804,
    OPTIX_ERROR_ENTRY_SYMBOL_NOT_FOUND = 7805,
    OPTIX_ERROR_LIBRARY_UNLOAD_FAILURE = 7806,
    OPTIX_ERROR_DEVICE_OUT_OF_MEMORY = 7807,
    OPTIX_ERROR_INVALID_POINTER = 7808,
    OPTIX_ERROR_CUDA_ERROR = 7900,
    OPTIX_ERROR_INTERNAL_ERROR = 7990,
    OPTIX_ERROR_UNKNOWN = 7999,
};

typedef void (*OptixLogCallback)(unsigned int level, const char* tag, const char* message, void* cbdata);

enum OptixDeviceContextValidationMode
{
    OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF = 0,
    OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL = 0xFFFFFFFF
};

struct OptixDeviceContextOptions
{
    OptixLogCallback logCallbackFunction;
    void* logCallbackData;
    int logCallbackLevel;
    OptixDeviceContextValidationMode validationMode;
};

enum OptixPixelFormat
{
    OPTIX_PIXEL_FORMAT_HALF1 = 0x220a,
    OPTIX_PIXEL_FORMAT_HALF2 = 0x2207,
    OPTIX_PIXEL_FORMAT_HALF3 = 0x2201,
    OPTIX_PIXEL_FORMAT_HALF4 = 0x2202,
    OPTIX_PIXEL_FORMAT_FLOAT1 = 0x220b,
    OPTIX_PIXEL_FORMAT_FLOAT2 = 0x2208,
    OPTIX_PIXEL_FORMAT_FLOAT3 = 0x2203,
    OPTIX_PIXEL_FORMAT_FLOAT4 = 0x2204,
    OPTIX_PIXEL_FORMAT_UCHAR3 = 0x2205,
    OPTIX_PIXEL_FORMAT_UCHAR4 = 0x2206,
    OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER = 0x2209
};

struct OptixImage2D
{
    CUdeviceptr data;
    unsigned int width;
    unsigned int height;
    unsigned int rowStrideInBytes;
    unsigned int pixelStrideInBytes;
    OptixPixelFormat format;
};

enum OptixDenoiserModelKind
{
    OPTIX_DENOISER_MODEL_KIND_AOV = 0x2324,
    OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV = 0x2326,
    OPTIX_DENOISER_MODEL_KIND_UPSCALE2X = 0x2327,
    OPTIX_DENOISER_MODEL_KIND_TEMPORAL_UPSCALE2X = 0x2328,
    OPTIX_DENOISER_MODEL_KIND_LDR = 0x2322,
    OPTIX_DENOISER_MODEL_KIND_HDR = 0x2323,
    OPTIX_DENOISER_MODEL_KIND_TEMPORAL = 0x2325
};

enum OptixDenoiserAlphaMode
{
    OPTIX_DENOISER_ALPHA_MODE_COPY = 0,
    OPTIX_DENOISER_ALPHA_MODE_DENOISE = 1
};

struct OptixDenoiserOptions
{
    unsigned int guideAlbedo;
    unsigned int guideNormal;
    OptixDenoiserAlphaMode denoiseAlpha;
};

struct OptixDenoiserGuideLayer
{
    OptixImage2D albedo;
    OptixImage2D normal;
    OptixImage2D flow;
    OptixImage2D previousOutputInternalGuideLayer;
    OptixImage2D outputInternalGuideLayer;
    OptixImage2D flowTrustworthiness;
};

enum OptixDenoiserAOVType
{
    OPTIX_DENOISER_AOV_TYPE_NONE = 0,
    OPTIX_DENOISER_AOV_TYPE_BEAUTY = 0x7000,
    OPTIX_DENOISER_AOV_TYPE_SPECULAR = 0x7001,
    OPTIX_DENOISER_AOV_TYPE_REFLECTION = 0x7002,
    OPTIX_DENOISER_AOV_TYPE_REFRACTION = 0x7003,
    OPTIX_DENOISER_AOV_TYPE_DIFFUSE = 0x7004
};

struct OptixDenoiserLayer
{
    OptixImage2D input;
    OptixImage2D previousOutput;
    OptixImage2D output;
    OptixDenoiserAOVType type;
};

struct OptixDenoiserParams
{
    CUdeviceptr hdrIntensity;
    float blendFactor;
    CUdeviceptr hdrAverageColor;
    unsigned int temporalModeUsePreviousLayers;
};

struct OptixDenoiserSizes
{
    size_t stateSizeInBytes;
    size_t withOverlapScratchSizeInBytes;
    size_t withoutOverlapScratchSizeInBytes;
    unsigned int overlapWindowSizeInPixels;
    size_t computeAverageColorSizeInBytes;
    size_t computeIntensitySizeInBytes;
    size_t internalGuideLayerPixelSizeInBytes;
};

class IOptixDenoiserAPI : public ISlangUnknown
{
public:
    SLANG_COM_INTERFACE(0x746a5883, 0x2a7e, 0x4d67, {0xbe, 0x2e, 0x62, 0x65, 0x8c, 0x02, 0x9e, 0x89});

    virtual ~IOptixDenoiserAPI() = default;

    virtual SLANG_NO_THROW const char* SLANG_MCALL optixGetErrorName(OptixResult result) = 0;

    virtual SLANG_NO_THROW const char* SLANG_MCALL optixGetErrorString(OptixResult result) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDeviceContextCreate(
        CUcontext fromContext,
        const OptixDeviceContextOptions* options,
        OptixDeviceContext* context
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDeviceContextDestroy(OptixDeviceContext context) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserCreate(
        OptixDeviceContext context,
        OptixDenoiserModelKind modelKind,
        const OptixDenoiserOptions* options,
        OptixDenoiser* returnHandle
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserCreateWithUserModel(
        OptixDeviceContext context,
        const void* data,
        size_t dataSizeInBytes,
        OptixDenoiser* returnHandle
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserDestroy(OptixDenoiser handle) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeMemoryResources(
        const OptixDenoiser handle,
        unsigned int maximumInputWidth,
        unsigned int maximumInputHeight,
        OptixDenoiserSizes* returnSizes
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserSetup(
        OptixDenoiser denoiser,
        CUstream stream,
        unsigned int inputWidth,
        unsigned int inputHeight,
        CUdeviceptr denoiserState,
        size_t denoiserStateSizeInBytes,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserInvoke(
        OptixDenoiser handle,
        CUstream stream,
        const OptixDenoiserParams* params,
        CUdeviceptr denoiserData,
        size_t denoiserDataSize,
        const OptixDenoiserGuideLayer* guideLayer,
        const OptixDenoiserLayer* layers,
        unsigned int numLayers,
        unsigned int inputOffsetX,
        unsigned int inputOffsetY,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeIntensity(
        OptixDenoiser handle,
        CUstream stream,
        const OptixImage2D* inputImage,
        CUdeviceptr outputIntensity,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) = 0;

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeAverageColor(
        OptixDenoiser handle,
        CUstream stream,
        const OptixImage2D* inputImage,
        CUdeviceptr outputAverageColor,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) = 0;
};

} // namespace rhi::optix_denoiser

/// Create an instance of the IOptixDenoiserAPI for the specified OptiX version.
/// The format matches the OPTIX_VERSION macro, e.g. 90000 for version 9.0.0.
/// \param optixVersion The specific OptiX version to target or 0 to target the highest version available.
/// \param outAPI The created API instance.
/// \return SLANG_OK if successful, SLANG_E_NOT_AVAILABLE if the specified version is not available, or another error code if the creation failed.
extern "C" SLANG_RHI_API rhi::Result SLANG_STDCALL
rhiCreateOptixDenoiserAPI(uint32_t optixVersion, rhi::optix_denoiser::IOptixDenoiserAPI** outAPI);
