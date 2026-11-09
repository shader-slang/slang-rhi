#include "pipeline.h"

#include "rhi-shared.h"
#include "shader-object.h"

#include <atomic>

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

// ----------------------------------------------------------------------------
// RayTracingPipeline
// ----------------------------------------------------------------------------

Result RayTracingPipeline::getShaderRecordLayouts(const RayTracingPipelineDesc& desc, ShaderRecordLayoutMap& layouts)
{
    if (!desc.program->getDesc().useRayTracingShaderRecordParameters)
        return SLANG_OK;
    auto program = checked_cast<ShaderProgram*>(desc.program);
    auto programLayout = program->linkedProgram->getLayout();
    const bool vulkan = program->getDevice()->getDeviceType() == DeviceType::Vulkan;
    for (uint32_t i = 0; i < programLayout->getEntryPointCount(); ++i)
    {
        auto entryPoint = programLayout->getEntryPointByIndex(i);
        ShaderObjectContainerType containerType;
        auto typeLayout = ShaderObjectLayout::_unwrapParameterGroups(entryPoint->getTypeLayout(), containerType);
        size_t size = typeLayout->getSize();
        if (size > UINT32_MAX - 3)
            return SLANG_E_INVALID_ARG;
        uint32_t offset =
            vulkan && entryPoint->getShaderRecordOffset() >= 0 ? uint32_t(entryPoint->getShaderRecordOffset()) : 0;
        if (size > UINT32_MAX - offset)
            return SLANG_E_INVALID_ARG;
        ShaderRecordLayout layout;
        layout.size = size ? offset + uint32_t(size) : 0;
        layout.entryPointCount = 1;
        layout.entryPoints[0] = {i, offset, uint32_t(size)};
        layouts[entryPoint->getNameOverride()] = layout;
    }
    for (uint32_t i = 0; i < desc.hitGroupCount; ++i)
    {
        const auto& group = desc.hitGroups[i];
        if (!group.hitGroupName)
            return SLANG_E_INVALID_ARG;
        const char* stages[] = {group.closestHitEntryPoint, group.anyHitEntryPoint, group.intersectionEntryPoint};
        ShaderRecordLayout groupLayout;
        for (const char* stage : stages)
        {
            if (!stage)
                continue;
            auto entry = layouts.find(stage);
            if (entry == layouts.end())
                return SLANG_E_INVALID_ARG;
            auto region = entry->second.entryPoints[0];
            // DXIL reads separate CBV bindings, mapped to consecutive local-root constants.
            // SPIR-V reads offsets assigned by the compiler in the one shared shader record.
            if (!vulkan)
                region.offset = groupLayout.size;
            uint32_t storageSize = (region.size + 3) & ~3u;
            if (storageSize > UINT32_MAX - region.offset)
                return SLANG_E_INVALID_ARG;
            for (uint32_t previous = 0; previous < groupLayout.entryPointCount; ++previous)
            {
                const auto& other = groupLayout.entryPoints[previous];
                if (region.size && other.size && region.offset < other.offset + other.size &&
                    other.offset < region.offset + region.size)
                {
                    program->getDevice()->printError(
                        "Hit-group shader-record regions overlap; compile the stages with uniform layout constraints."
                    );
                    return SLANG_E_INVALID_ARG;
                }
            }
            groupLayout.entryPoints[groupLayout.entryPointCount++] = region;
            if (storageSize)
                groupLayout.size = max(groupLayout.size, region.offset + storageSize);
        }
        if (!layouts.emplace(group.hitGroupName, groupLayout).second)
            return SLANG_E_INVALID_ARG;
    }
    return SLANG_OK;
}

Result RayTracingPipeline::getShaderRecordLayout(const char* exportName, RayTracingShaderRecordLayout* outLayout)
{
    if (!exportName || !outLayout)
        return SLANG_E_INVALID_ARG;
    if (isVirtual())
    {
        auto concrete = static_cast<RayTracingPipeline*>(getConcretePipeline());
        return concrete ? concrete->getShaderRecordLayout(exportName, outLayout) : SLANG_E_NOT_AVAILABLE;
    }
    auto found = m_shaderRecordLayouts.find(exportName);
    if (found == m_shaderRecordLayouts.end())
        return SLANG_E_NOT_AVAILABLE;
    *outLayout = found->second;
    return SLANG_OK;
}

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
