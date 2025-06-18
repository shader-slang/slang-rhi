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

RenderPipeline::RenderPipeline(Device* device, const RenderPipelineDesc& desc)
    : Pipeline(device)
    , m_desc(desc)
{
    m_descHolder.holdList(m_desc.targets, m_desc.targetCount);
    m_descHolder.holdString(m_desc.label);
    m_program = checked_cast<ShaderProgram*>(desc.program);
    m_inputLayout = checked_cast<InputLayout*>(desc.inputLayout);
}

// ----------------------------------------------------------------------------
// VirtualRenderPipeline
// ----------------------------------------------------------------------------

VirtualRenderPipeline::VirtualRenderPipeline(Device* device, const RenderPipelineDesc& desc)
    : RenderPipeline(device, desc)
{
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

ComputePipeline::ComputePipeline(Device* device, const ComputePipelineDesc& desc)
    : Pipeline(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
    m_program = checked_cast<ShaderProgram*>(desc.program);
}


// ----------------------------------------------------------------------------
// VirtualComputePipeline
// ----------------------------------------------------------------------------

VirtualComputePipeline::VirtualComputePipeline(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
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

RayTracingPipeline::RayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc)
    : Pipeline(device)
    , m_desc(desc)
{
    m_descHolder.holdList(m_desc.hitGroups, m_desc.hitGroupCount);
    m_descHolder.holdString(m_desc.label);
    for (uint32_t i = 0; i < m_desc.hitGroupCount; i++)
    {
        HitGroupDesc& hitGroup = const_cast<HitGroupDesc&>(m_desc.hitGroups[i]);
        m_descHolder.holdString(hitGroup.hitGroupName);
        m_descHolder.holdString(hitGroup.closestHitEntryPoint);
        m_descHolder.holdString(hitGroup.anyHitEntryPoint);
        m_descHolder.holdString(hitGroup.intersectionEntryPoint);
    }
    m_program = checked_cast<ShaderProgram*>(desc.program);
}

// ----------------------------------------------------------------------------
// VirtualRayTracingPipeline
// ----------------------------------------------------------------------------

VirtualRayTracingPipeline::VirtualRayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device, desc)
{
}

Result VirtualRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi
