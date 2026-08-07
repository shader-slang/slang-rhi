#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

/// CUDA doesn't have API for clearing textures.
/// This class provides a set of kernels to clear CUDA surfaces.
/// It is used by the CommandExecutor to implement the clear texture commands.
/// To support all possible texture types, the kernels are generated for all combinations of:
/// - Dimension: 1D, 2D, 3D, Cube
/// - Size: 8, 16, 32, 64, 128 bits
/// - Layered: NonLayered, Layered
class ClearEngine
{
public:
    enum class Dimension
    {
        _1D,
        _2D,
        _3D,
        Cube,
        Count,
    };
    enum class Size
    {
        _8,
        _16,
        _32,
        _64,
        _128,
        Count,
    };
    enum class Layered
    {
        NonLayered,
        Layered,
        Count,
    };

    Result initialize(DeviceImpl* device);
    void release();

    void clearTextureUint(
        CUstream stream,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const uint32_t clearValue[4]
    );

    void clearTextureFloat(
        CUstream stream,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const float clearValue[4]
    );

private:
    CUmodule m_module = 0;
    CUfunction m_clearFunction[size_t(Dimension::Count)][size_t(Size::Count)][size_t(Layered::Count)];

    void clearTexture(
        CUstream stream,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const uint32_t clearValue[4]
    );

    void launch(
        CUstream stream,
        CUfunction function,
        const uint32_t blockDim[3],
        CUsurfObject surface,
        const uint32_t sizeAndLayer[4],
        const uint32_t clearValue[4]
    );
};

} // namespace rhi::cuda
