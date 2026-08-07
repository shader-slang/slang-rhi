#include "cuda-clear-engine.h"
#include "cuda-device.h"
#include "cuda-texture.h"
#include "cuda-utils.h"
#include "cuda-nvrtc.h"

#include "format-conversion.h"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(resources);

namespace rhi::cuda {

Result ClearEngine::initialize(DeviceImpl* device)
{
    // Load CUDA kernel source
    auto fs = cmrc::resources::get_filesystem();
    auto source = fs.open("src/cuda/kernels/clear-texture.cu");

    // Compile CUDA kernel to PTX
    NVRTC::CompileResult compileResult;
    {
        NVRTC nvrtc;
        SLANG_RETURN_ON_FAIL(nvrtc.initialize(device->m_debugCallback));
        SLANG_RETURN_ON_FAIL(nvrtc.compilePTX(source.begin(), compileResult));
    }

    // Load PTX module
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuModuleLoadData(&m_module, compileResult.ptx.data()), device);

    // Get clear kernel functions
    for (size_t dim = 0; dim < size_t(Dimension::Count); ++dim)
    {
        for (size_t size = 0; size < size_t(Size::Count); ++size)
        {
            for (size_t layered = 0; layered < size_t(Layered::Count); ++layered)
            {
                // Skip 3D layered
                if (dim == size_t(Dimension::_3D) && layered)
                {
                    continue;
                }
                const char* dimNames[] = {"1D", "2D", "3D", "Cube"};
                const char* sizeNames[] = {"8", "16", "32", "64", "128"};
                const char* layeredNames[] = {"0", "1"};
                char name[128];
                snprintf(
                    name,
                    sizeof(name),
                    "clearTexture%s_%s_%s",
                    dimNames[dim],
                    sizeNames[size],
                    layeredNames[layered]
                );
                SLANG_CUDA_RETURN_ON_FAIL_REPORT(
                    cuModuleGetFunction(&m_clearFunction[dim][size][layered], m_module, name),
                    device
                );
            }
        }
    }

    return SLANG_OK;
}

void ClearEngine::release()
{
    if (m_module)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuModuleUnload(m_module));
    }
}

void ClearEngine::clearTextureUint(
    CUstream stream,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const uint32_t clearValue[4]
)
{
    PackIntFunc packIntFunc = getFormatConversionFuncs(texture->m_desc.format).packIntFunc;
    SLANG_RHI_ASSERT(packIntFunc);
    uint32_t truncatedClearValue[4] = {};
    truncateBySintFormat(texture->m_desc.format, clearValue, truncatedClearValue);
    uint32_t packedClearValue[4] = {};
    packIntFunc(truncatedClearValue, packedClearValue);
    clearTexture(stream, texture, subresourceRange, packedClearValue);
}

void ClearEngine::clearTextureFloat(
    CUstream stream,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const float clearValue[4]
)
{
    PackFloatFunc packFloatFunc = getFormatConversionFuncs(texture->m_desc.format).packFloatFunc;
    SLANG_RHI_ASSERT(packFloatFunc);
    uint32_t packedClearValue[4] = {};
    packFloatFunc(clearValue, packedClearValue);
    clearTexture(stream, texture, subresourceRange, packedClearValue);
}

void ClearEngine::clearTexture(
    CUstream stream,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const uint32_t clearValue[4]
)
{
    Dimension dim = {};
    Size size = {};
    Layered layered = Layered::NonLayered;
    uint32_t blockDim[3] = {1, 1, 1};

    switch (texture->m_desc.type)
    {
    case TextureType::Texture1D:
        dim = Dimension::_1D;
        blockDim[0] = 256;
        break;
    case TextureType::Texture1DArray:
        dim = Dimension::_1D;
        layered = Layered::Layered;
        blockDim[0] = 256;
        break;
    case TextureType::Texture2D:
        dim = Dimension::_2D;
        blockDim[0] = 32;
        blockDim[1] = 32;
        break;
    case TextureType::Texture2DArray:
        dim = Dimension::_2D;
        layered = Layered::Layered;
        blockDim[0] = 32;
        blockDim[1] = 32;
        break;
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        return;
    case TextureType::Texture3D:
        dim = Dimension::_3D;
        blockDim[0] = 8;
        blockDim[1] = 8;
        blockDim[2] = 8;
        break;
    case TextureType::TextureCube:
        dim = Dimension::Cube;
        blockDim[0] = 32;
        blockDim[1] = 32;
        break;
    case TextureType::TextureCubeArray:
        dim = Dimension::Cube;
        layered = Layered::Layered;
        blockDim[0] = 32;
        blockDim[1] = 32;
        break;
    }

    const FormatInfo& formatInfo = getFormatInfo(texture->m_desc.format);
    switch (formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock)
    {
    case 1:
        size = Size::_8;
        break;
    case 2:
        size = Size::_16;
        break;
    case 4:
        size = Size::_32;
        break;
    case 8:
        size = Size::_64;
        break;
    case 16:
        size = Size::_128;
        break;
    default:
        return;
    }

    CUfunction function = m_clearFunction[size_t(dim)][size_t(size)][size_t(layered)];

    for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; ++mipOffset)
    {
        uint32_t mip = subresourceRange.mip + mipOffset;
        Extent3D mipSize = calcMipSize(texture->m_desc.size, mip);
        for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; ++layerOffset)
        {
            uint32_t layer = subresourceRange.layer + layerOffset;
            SubresourceRange sr = {layer, 1, mip, 1};
            CUsurfObject surface = texture->getSurfObject(sr);
            uint32_t sizeAndLayer[4] = {mipSize.width, mipSize.height, mipSize.depth, layer};
            launch(stream, function, blockDim, surface, sizeAndLayer, reinterpret_cast<const uint32_t*>(clearValue));
        }
    }
}

void ClearEngine::launch(
    CUstream stream,
    CUfunction function,
    const uint32_t blockDim[3],
    CUsurfObject surface,
    const uint32_t sizeAndLayer[4],
    const uint32_t clearValue[4]
)
{
    uint32_t gridDim[3] = {
        (sizeAndLayer[0] + blockDim[0] - 1) / blockDim[0],
        (sizeAndLayer[1] + blockDim[1] - 1) / blockDim[1],
        (sizeAndLayer[2] + blockDim[2] - 1) / blockDim[2]
    };

    struct Arguments
    {
        CUsurfObject surface;
        uint64_t padding;
        uint32_t sizeAndLayer[4];
        uint32_t value[4];
    };

    Arguments args = {};
    args.surface = surface;
    args.sizeAndLayer[0] = sizeAndLayer[0];
    args.sizeAndLayer[1] = sizeAndLayer[1];
    args.sizeAndLayer[2] = sizeAndLayer[2];
    args.sizeAndLayer[3] = sizeAndLayer[3];
    args.value[0] = clearValue[0];
    args.value[1] = clearValue[1];
    args.value[2] = clearValue[2];
    args.value[3] = clearValue[3];
    size_t argsSize = sizeof(Arguments);

    void* extra[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        &args,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        &argsSize,
        CU_LAUNCH_PARAM_END,
    };

    SLANG_CUDA_ASSERT_ON_FAIL(cuLaunchKernel(
        function,
        gridDim[0],
        gridDim[1],
        gridDim[2],
        blockDim[0],
        blockDim[1],
        blockDim[2],
        0,
        stream,
        nullptr,
        extra
    ));
}

} // namespace rhi::cuda
