#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

class RenderPipelineImpl : public RenderPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc);
    ~RenderPipelineImpl();

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);
    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::map<std::string, uint32_t> m_shaderGroupNameToIndex;
    uint32_t m_shaderGroupCount;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);
    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
