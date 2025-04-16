#pragma once

#include "metal-base.h"

namespace rhi::metal {

/// Metal doesn't have API for clearing textures.
/// This class provides a set of compute pipelines to clear Metal surfaces.
/// It is used by the CommandRecorder to implement the clear texture commands.
/// To support all possible texture types, the kernels are generated for all combinations of:
/// - TextureType: 1D, 1DArray, 2D, 2DArray, 3D, Cube, CubeArray
/// - Type: float, half, uint, int
class ClearEngine
{
public:
    Result initialize(MTL::Device* device);
    void release();

    void clearTextureUint(
        MTL::ComputeCommandEncoder* encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const uint32_t clearValue[4]
    );

    void clearTextureFloat(
        MTL::ComputeCommandEncoder* encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const float clearValue[4]
    );

private:
    enum class Type
    {
        Float,
        Half,
        Uint,
        Int,
        Count,
    };

    struct Params
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t layer;
        uint32_t mip;
    };

    static constexpr size_t kTextureTypeCount = size_t(TextureType::TextureCubeArray) + 1;
    static constexpr size_t kTypeCount = size_t(Type::Count);

    NS::SharedPtr<MTL::Library> m_library;
    NS::SharedPtr<MTL::ComputePipelineState> m_clearPipelines[kTextureTypeCount][kTypeCount];
    MTL::Size m_threadGroupSizes[kTextureTypeCount];

    void clearTexture(
        MTL::ComputeCommandEncoder* encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        Type type,
        const void* clearValue,
        size_t clearValueSize
    );
};

} // namespace rhi::metal
