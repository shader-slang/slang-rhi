#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

/// WebGPU doesn't have API for clearing textures.
/// This class provides a set of compute pipelines to clear WebGPU textures.
/// It is used by the CommandRecorder to implement the clear texture commands.
/// To support all possible texture types, separate pipelines are created for:
/// - TextureType: 1D, 1DArray, 2D, 2DArray, 3D, Cube, CubeArray
/// - Type: float, uint
class ClearEngine
{
public:
    Result initialize(Context* ctx);
    void release();

    void clearTextureUint(
        WGPUComputePassEncoder encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const uint32_t clearValue[4]
    );

    void clearTextureFloat(
        WGPUComputePassEncoder encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        const float clearValue[4]
    );

private:
    enum class Type
    {
        Float,
        Uint,
        Count,
    };

    struct Params
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t layer;
        uint32_t mipLevel;
        uint32_t format;
    };

    static constexpr size_t kTextureTypeCount = size_t(TextureType::TextureCubeArray) + 1;
    static constexpr size_t kTypeCount = size_t(Type::Count);

    Context* m_ctx = nullptr;
    WGPUShaderModule m_shaderModule = nullptr;
    WGPUComputePipeline m_clearPipelines[kTextureTypeCount][kTypeCount] = {};
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUPipelineLayout m_pipelineLayout = nullptr;

    // Thread group sizes for different texture types
    struct
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    } m_workgroupSizes[kTextureTypeCount];

    void clearTexture(
        WGPUComputePassEncoder encoder,
        TextureImpl* texture,
        SubresourceRange subresourceRange,
        Type type,
        const void* clearValue,
        size_t clearValueSize
    );

    Result createBindGroupLayout();
    Result createPipelineLayout();
    Result createShaderModule();
    Result createPipelines();
};

} // namespace rhi::wgpu
