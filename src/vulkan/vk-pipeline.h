#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

class RenderPipelineImpl : public RenderPipeline
{
public:
    DeviceImpl* m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~RenderPipelineImpl();

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    DeviceImpl* m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    DeviceImpl* m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::map<std::string, Index> m_shaderGroupNameToIndex;
    Int m_shaderGroupCount;

    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#if 0
class PipelineImpl : public Pipeline
{
public:
    PipelineImpl(DeviceImpl* device);
    ~PipelineImpl();

    // Turns `m_device` into a strong reference.
    // This method should be called before returning the pipeline state object to
    // external users (i.e. via an `IPipeline` pointer).
    void establishStrongDeviceReference();

    virtual void comFree() override;

    void init(const RenderPipelineDesc& inDesc);
    void init(const ComputePipelineDesc& inDesc);
    void init(const RayTracingPipelineDesc& inDesc);

    Result createVKGraphicsPipeline();

    Result createVKComputePipeline();

    virtual Result ensureAPIPipelineCreated() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    BreakableReference<DeviceImpl> m_device;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
};


class RayTracingPipelineImpl : public PipelineImpl
{
public:
    std::map<std::string, Index> shaderGroupNameToIndex;
    Int shaderGroupCount;

    RayTracingPipelineImpl(DeviceImpl* device);

    uint32_t findEntryPointIndexByName(const std::map<std::string, Index>& entryPointNameToIndex, const char* name);

    Result createVKRayTracingPipeline();

    virtual Result ensureAPIPipelineCreated() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};
#endif
} // namespace rhi::vk
