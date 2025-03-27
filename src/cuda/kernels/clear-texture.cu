// This file implements CUDA kernels for clearing textures.
// The kernels are named clearTexture{dim}_{bits}_{layered}, where:
// - {dim} is the dimension of the texture (1D, 2D, 3D, Cube)
// - {bits} is the number of bits per element (8, 16, 32, 64, 128)
// - {layered} is 0 if the texture is not layered, 1 if it is layered
// The kernels are called with the following arguments:
// - surface: the surface object of the texture to clear
// - sizeAndLayer: a uint4 containing the size of the texture (width, height, depth) and the layer index
// - value: a uint4 containing the raw value to write to the texture
//
// To recompile this file into the corresponding PTX code, run:
// nvcc -ptx -o clear-texture.ptx clear-texture.cu

#include <cuda_runtime.h>

template<typename T, bool Layered>
__device__ void clearTexture1D(cudaSurfaceObject_t surface, uint4 sizeAndLayer, uint4 value)
{
    unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;

    if (x < sizeAndLayer.x)
    {
        T tmp = *((T*)&value);
        if (Layered)
            surf1DLayeredwrite(tmp, surface, x * sizeof(T), sizeAndLayer.w);
        else
            surf1Dwrite(tmp, surface, x * sizeof(T));
    }
}

template<typename T, bool Layered>
__device__ void clearTexture2D(cudaSurfaceObject_t surface, uint4 sizeAndLayer, uint4 value)
{
    unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < sizeAndLayer.x && y < sizeAndLayer.y)
    {
        T tmp = *((T*)&value);
        if (Layered)
            surf2DLayeredwrite(tmp, surface, x * sizeof(T), y, sizeAndLayer.w);
        else
            surf2Dwrite(tmp, surface, x * sizeof(T), y);
    }
}

template<typename T, bool Layered>
__device__ void clearTexture3D(cudaSurfaceObject_t surface, uint4 sizeAndLayer, uint4 value)
{
    unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned int z = blockIdx.z * blockDim.z + threadIdx.z;

    if (x < sizeAndLayer.x && y < sizeAndLayer.y && z < sizeAndLayer.z)
    {
        T tmp = *((T*)&value);
        surf3Dwrite(tmp, surface, x * sizeof(T), y, z);
    }
}

template<typename T, bool Layered>
__device__ void clearTextureCube(cudaSurfaceObject_t surface, uint4 sizeAndLayer, uint4 value)
{
    unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < sizeAndLayer.x && y < sizeAndLayer.y)
    {
        T tmp = *((T*)&value);
        if (Layered)
        {
            surfCubemapLayeredwrite(tmp, surface, x * sizeof(T), y, sizeAndLayer.w);
        }
        else
        {
            surfCubemapwrite(tmp, surface, x * sizeof(T), y, sizeAndLayer.w);
        }
    }
}

#define DEFINE_SINGLE(dim, type, bits, layered)                                                                        \
    extern "C" __global__ void clearTexture##dim##_##bits##_##layered(                                                 \
        cudaSurfaceObject_t surface,                                                                                   \
        uint4 sizeAndLayer,                                                                                            \
        uint4 value                                                                                                    \
    )                                                                                                                  \
    {                                                                                                                  \
        clearTexture##dim<type, layered>(surface, sizeAndLayer, value);                                                \
    }

#define DEFINE_ALL(type, bits)                                                                                         \
    DEFINE_SINGLE(1D, type, bits, 0)                                                                                   \
    DEFINE_SINGLE(1D, type, bits, 1)                                                                                   \
    DEFINE_SINGLE(2D, type, bits, 0)                                                                                   \
    DEFINE_SINGLE(2D, type, bits, 1)                                                                                   \
    DEFINE_SINGLE(3D, type, bits, 0)                                                                                   \
    DEFINE_SINGLE(Cube, type, bits, 0)                                                                                 \
    DEFINE_SINGLE(Cube, type, bits, 1)

DEFINE_ALL(char1, 8)
DEFINE_ALL(short1, 16)
DEFINE_ALL(int1, 32)
DEFINE_ALL(int2, 64)
DEFINE_ALL(int4, 128)
