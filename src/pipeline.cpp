#include "pipeline.h"

#include "rhi-shared.h"

namespace rhi {

// ----------------------------------------------------------------------------
// RenderPipeline
// ----------------------------------------------------------------------------

IPipeline* RenderPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRenderPipeline::getTypeGuid())
        return static_cast<IRenderPipeline*>(this);
    return nullptr;
}

// ----------------------------------------------------------------------------
// VirtualRenderPipeline
// ----------------------------------------------------------------------------

Result VirtualRenderPipeline::init(Device* device, const RenderPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.targets, m_desc.targetCount);
    m_program = checked_cast<ShaderProgram*>(desc.program);
    m_inputLayout = checked_cast<InputLayout*>(desc.inputLayout);
    return SLANG_OK;
}

Result VirtualRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// VirtualRenderPipeline
// ----------------------------------------------------------------------------

IPipeline* ComputePipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IComputePipeline::getTypeGuid())
        return static_cast<IComputePipeline*>(this);
    return nullptr;
}

Result VirtualComputePipeline::init(Device* device, const ComputePipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IPipeline* RayTracingPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRayTracingPipeline::getTypeGuid())
        return static_cast<IRayTracingPipeline*>(this);
    return nullptr;
}

Result VirtualRayTracingPipeline::init(Device* device, const RayTracingPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.hitGroups, m_desc.hitGroupCount);
    for (uint32_t i = 0; i < m_desc.hitGroupCount; i++)
    {
        m_descHolder.holdString(m_desc.hitGroups[i].hitGroupName);
        m_descHolder.holdString(m_desc.hitGroups[i].closestHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].anyHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].intersectionEntryPoint);
    }
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi
