#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

class RenderPipelineImpl : public RenderPipeline
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayout> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~RenderPipelineImpl();

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayout> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayout> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::map<std::string, Index> m_shaderGroupNameToIndex;
    Int m_shaderGroupCount;

    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
