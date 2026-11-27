#include "pipeline.h"

#include "rhi-shared.h"
#include "device.h"
#include "shader-object.h"

#include <atomic>
#include <mutex>

namespace rhi {

namespace {
std::atomic<uint64_t> s_nextPipelineCacheId{1};
}

Pipeline::Pipeline(Device* device)
    : DeviceChild(device)
    , m_specializationCacheId(s_nextPipelineCacheId.fetch_add(1, std::memory_order_relaxed))
{
    SLANG_RHI_ASSERT(m_specializationCacheId != 0);
}

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

Result VirtualComputePipeline::getMaxCooperativeThreadGroupCount(IShaderObject* rootObject, uint32_t* outCount)
{
    if (!outCount)
        return SLANG_E_INVALID_ARG;
    *outCount = 0;
    if (!is_set(m_desc.flags, ComputePipelineFlags::CooperativeLaunch))
        return SLANG_E_NOT_AVAILABLE;

    RefPtr<ExtendedShaderObjectTypeListObject> specializationArgs;
    if (m_program->isSpecializable())
    {
        if (!rootObject)
            return SLANG_E_INVALID_ARG;
        specializationArgs = new ExtendedShaderObjectTypeListObject();
        SLANG_RETURN_ON_FAIL(
            checked_cast<RootShaderObject*>(rootObject)->collectSpecializationArgs(*specializationArgs)
        );
    }

    // Resolve the concrete pipeline the same way a dispatch does. The lock also orders this with
    // pipeline resolution on other threads, which writes m_concretePipeline.
    Pipeline* concretePipeline = nullptr;
    {
        std::lock_guard<std::mutex> lock(getDevice()->m_pipelineResolutionMutex);
        SLANG_RETURN_ON_FAIL(getDevice()->getConcretePipeline(this, specializationArgs.get(), concretePipeline));
    }
    return checked_cast<ComputePipeline*>(concretePipeline)->getMaxCooperativeThreadGroupCount(rootObject, outCount);
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
    // A deferred or specializable pipeline may compile after the caller's descriptor chain
    // has gone out of scope. Own the recognized extension just like the hit groups below.
    // Unrecognized extensions are ignored; never retain a borrowed tail in the owned copy.
    auto optixDesc = findStructInChain<OptixRayTracingPipelineDesc>(desc.next);
    m_desc.next = nullptr;
    if (optixDesc)
    {
        m_descHolder.holdList(optixDesc, 1);
        const_cast<OptixRayTracingPipelineDesc*>(optixDesc)->next = nullptr;
        m_desc.next = optixDesc;
    }
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
