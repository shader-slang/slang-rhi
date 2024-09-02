#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

class RenderPipelineImpl : public RenderPipelineBase
{
public:
    RenderPipelineImpl(DeviceImpl* device);
    virtual ~RenderPipelineImpl() override;
    Result init(const RenderPipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    ComputePipelineImpl(DeviceImpl* device);
    virtual ~ComputePipelineImpl() override;
    Result init(const ComputePipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

class RayTracingPipelineImpl : public RayTracingPipelineBase
{
public:
    RayTracingPipelineImpl(DeviceImpl* device);
    virtual ~RayTracingPipelineImpl() override;
    Result init(const RayTracingPipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::map<std::string, Index> m_shaderGroupNameToIndex;
    Int m_shaderGroupCount;
};

} // namespace rhi::vk
