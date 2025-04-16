// This file implements Metal kernels for clearing textures.

#include <metal_stdlib>
using namespace metal;

struct Params
{
    uint width;
    uint height;
    uint depth;
    uint layer;
    uint mip;
};

#define DEFINE_CLEAR_TEXTURE1D(T)                                                                                      \
    kernel void clear_texture1d_##T(                                                                                   \
        texture1d<T, access::write> texture [[texture(0)]],                                                            \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint gid [[thread_position_in_grid]]                                                                           \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid < params.width)                                                                                        \
        {                                                                                                              \
            texture.write(value, gid /*, params.mip*/);                                                                \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURE1D_ARRAY(T)                                                                                \
    kernel void clear_texture1d_array_##T(                                                                             \
        texture1d_array<T, access::write> texture [[texture(0)]],                                                      \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint gid [[thread_position_in_grid]]                                                                           \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid < params.width)                                                                                        \
        {                                                                                                              \
            texture.write(value, gid, params.layer /*, params.mip*/);                                                  \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURE2D(T)                                                                                      \
    kernel void clear_texture2d_##T(                                                                                   \
        texture2d<T, access::write> texture [[texture(0)]],                                                            \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint2 gid [[thread_position_in_grid]]                                                                          \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid.x < params.width && gid.y < params.height)                                                             \
        {                                                                                                              \
            texture.write(value, gid, params.mip);                                                                     \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURE2D_ARRAY(T)                                                                                \
    kernel void clear_texture2d_array_##T(                                                                             \
        texture2d_array<T, access::write> texture [[texture(0)]],                                                      \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint2 gid [[thread_position_in_grid]]                                                                          \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid.x < params.width && gid.y < params.height)                                                             \
        {                                                                                                              \
            texture.write(value, gid, params.layer, params.mip);                                                       \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURE3D(T)                                                                                      \
    kernel void clear_texture3d_##T(                                                                                   \
        texture3d<T, access::write> texture [[texture(0)]],                                                            \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint3 gid [[thread_position_in_grid]]                                                                          \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid.x < params.width && gid.y < params.height && gid.z < params.depth)                                     \
        {                                                                                                              \
            texture.write(value, gid, params.mip);                                                                     \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURECUBE(T)                                                                                    \
    kernel void clear_texturecube_##T(                                                                                 \
        texturecube<T, access::write> texture [[texture(0)]],                                                          \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint2 gid [[thread_position_in_grid]]                                                                          \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid.x < params.width && gid.y < params.height)                                                             \
        {                                                                                                              \
            texture.write(value, gid, params.layer, params.mip);                                                       \
        }                                                                                                              \
    }

#define DEFINE_CLEAR_TEXTURECUBE_ARRAY(T)                                                                              \
    kernel void clear_texturecube_array_##T(                                                                           \
        texturecube_array<T, access::write> texture [[texture(0)]],                                                    \
        constant Params& params [[buffer(0)]],                                                                         \
        constant T##4 & value [[buffer(1)]],                                                                           \
        uint2 gid [[thread_position_in_grid]]                                                                          \
    )                                                                                                                  \
    {                                                                                                                  \
        if (gid.x < params.width && gid.y < params.height)                                                             \
        {                                                                                                              \
            texture.write(value, gid, params.layer % 6, params.layer / 6, params.mip);                                 \
        }                                                                                                              \
    }


#define DEFINE_CLEAR(type)                                                                                             \
    DEFINE_CLEAR_TEXTURE1D(type)                                                                                       \
    DEFINE_CLEAR_TEXTURE1D_ARRAY(type)                                                                                 \
    DEFINE_CLEAR_TEXTURE2D(type)                                                                                       \
    DEFINE_CLEAR_TEXTURE2D_ARRAY(type)                                                                                 \
    DEFINE_CLEAR_TEXTURE3D(type)                                                                                       \
    DEFINE_CLEAR_TEXTURECUBE(type)                                                                                     \
    DEFINE_CLEAR_TEXTURECUBE_ARRAY(type)

DEFINE_CLEAR(float)
DEFINE_CLEAR(half)
DEFINE_CLEAR(uint)
DEFINE_CLEAR(int)
