#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

class RenderPipelineImpl : public RenderPipeline
{
public:
    BreakableReference<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~RenderPipelineImpl();

    virtual void comFree() override { m_device.breakStrongReference(); }

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    BreakableReference<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~ComputePipelineImpl();

    virtual void comFree() override { m_device.breakStrongReference(); }

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    BreakableReference<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::map<std::string, uint32_t> m_shaderGroupNameToIndex;
    uint32_t m_shaderGroupCount;

    ~RayTracingPipelineImpl();

    virtual void comFree() override { m_device.breakStrongReference(); }

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
