#pragma once

#include "vk-base.h"

#include <map>
#include <string>

namespace rhi::vk {

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

} // namespace rhi::vk
