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

VirtualRenderPipeline::VirtualRenderPipeline(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device)
    , m_desc(desc)
{
    m_descHolder.holdList(m_desc.targets, m_desc.targetCount);
    m_program = checked_cast<ShaderProgram*>(desc.program);
    m_inputLayout = checked_cast<InputLayout*>(desc.inputLayout);
}

Result VirtualRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// ComputePipeline
// ----------------------------------------------------------------------------

IPipeline* ComputePipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IComputePipeline::getTypeGuid())
        return static_cast<IComputePipeline*>(this);
    return nullptr;
}

// ----------------------------------------------------------------------------
// VirtualComputePipeline
// ----------------------------------------------------------------------------

VirtualComputePipeline::VirtualComputePipeline(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device)
    , m_desc(desc)
{
    m_program = checked_cast<ShaderProgram*>(desc.program);
}

Result VirtualComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// RayTracingPipeline
// ----------------------------------------------------------------------------

IPipeline* RayTracingPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRayTracingPipeline::getTypeGuid())
        return static_cast<IRayTracingPipeline*>(this);
    return nullptr;
}

// ----------------------------------------------------------------------------
// VirtualRayTracingPipeline
// ----------------------------------------------------------------------------

VirtualRayTracingPipeline::VirtualRayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device)
{
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
}

Result VirtualRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi
