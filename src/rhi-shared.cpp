#include "rhi-shared.h"
#include "command-list.h"

#include "core/common.h"

#include <slang.h>

#include <atomic>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
}

IResource* Buffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == IBuffer::getTypeGuid())
        return static_cast<IBuffer*>(this);
    return nullptr;
}

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = min(resolved.offset, m_desc.size);
    resolved.size = min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

BufferDesc& Buffer::getDesc()
{
    return m_desc;
}

Result Buffer::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITexture::getTypeGuid())
        return static_cast<ITexture*>(this);
    return nullptr;
}

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.mipLevel = min(resolved.mipLevel, m_desc.mipLevelCount);
    resolved.mipLevelCount = min(resolved.mipLevelCount, m_desc.mipLevelCount - resolved.mipLevel);
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    resolved.baseArrayLayer = min(resolved.baseArrayLayer, arrayLayerCount);
    resolved.layerCount = min(resolved.layerCount, arrayLayerCount - resolved.baseArrayLayer);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.mipLevel > 0 || range.mipLevelCount < m_desc.mipLevelCount)
    {
        return false;
    }
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    if (range.baseArrayLayer > 0 || range.layerCount < arrayLayerCount)
    {
        return false;
    }
    return true;
}

TextureDesc& Texture::getDesc()
{
    return m_desc;
}

Result Texture::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Texture::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITextureView::getTypeGuid())
        return static_cast<ITextureView*>(this);
    return nullptr;
}

Result TextureView::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

ISampler* Sampler::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ISampler::getTypeGuid())
        return static_cast<ISampler*>(this);
    return nullptr;
}

const SamplerDesc& Sampler::getDesc()
{
    return m_desc;
}

Result Sampler::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_IMPLEMENTED;
}

IAccelerationStructure* AccelerationStructure::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() ||
        guid == IAccelerationStructure::getTypeGuid())
        return static_cast<IAccelerationStructure*>(this);
    return nullptr;
}

AccelerationStructureHandle AccelerationStructure::getHandle()
{
    return {};
}

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialTypeLayout
)
{
    // Our task here is to figure out if a value of `concreteTypeLayout`
    // can fit into an existential value using `existentialTypelayout`.

    // We can start by asking how many bytes the concrete type of the object consumes.
    //
    auto concreteValueSize = concreteTypeLayout->getSize();

    // We can also compute how many bytes the existential-type value provides,
    // but we need to remember that the *payload* part of that value comes after
    // the header with RTTI and witness-table IDs, so the payload is 16 bytes
    // smaller than the entire value.
    //
    auto existentialValueSize = existentialTypeLayout->getSize();
    auto existentialPayloadSize = existentialValueSize - 16;

    // If the concrete type consumes more ordinary bytes than we have in the payload,
    // it cannot possibly fit.
    //
    if (concreteValueSize > existentialPayloadSize)
        return false;

    // It is possible that the ordinary bytes of `concreteTypeLayout` can fit
    // in the payload, but that type might also use storage other than ordinary
    // bytes. In that case, the value would *not* fit, because all the non-ordinary
    // data can't fit in the payload at all.
    //
    auto categoryCount = concreteTypeLayout->getCategoryCount();
    for (unsigned int i = 0; i < categoryCount; ++i)
    {
        auto category = concreteTypeLayout->getCategoryByIndex(i);
        switch (category)
        {
        // We want to ignore any ordinary/uniform data usage, since that
        // was already checked above.
        //
        case slang::ParameterCategory::Uniform:
            break;

        // Any other kind of data consumed means the value cannot possibly fit.
        default:
            return false;

            // TODO: Are there any cases of resource usage that need to be ignored here?
            // E.g., if the sub-object contains its own existential-type fields (which
            // get reflected as consuming "existential value" storage) should that be
            // ignored?
        }
    }

    // If we didn't reject the concrete type above for either its ordinary
    // data or some use of non-ordinary data, then it seems like it must fit.
    //
    return true;
}

IShaderProgram* ShaderProgram::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderProgram::getTypeGuid())
        return static_cast<IShaderProgram*>(this);
    return nullptr;
}

IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IInputLayout::getTypeGuid())
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

IPipeline* RenderPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRenderPipeline::getTypeGuid())
        return static_cast<IRenderPipeline*>(this);
    return nullptr;
}

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






void ShaderProgram::init(const ShaderProgramDesc& desc)
{
    m_desc = desc;

    slangGlobalScope = desc.slangGlobalScope;
    for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
    {
        slangEntryPoints.push_back(ComPtr<slang::IComponentType>(desc.slangEntryPoints[i]));
    }

    auto session = desc.slangGlobalScope ? desc.slangGlobalScope->getSession() : nullptr;
    if (desc.linkingStyle == LinkingStyle::SingleProgram)
    {
        std::vector<slang::IComponentType*> components;
        if (desc.slangGlobalScope)
        {
            components.push_back(desc.slangGlobalScope);
        }
        for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
        {
            if (!session)
            {
                session = desc.slangEntryPoints[i]->getSession();
            }
            components.push_back(desc.slangEntryPoints[i]);
        }
        session->createCompositeComponentType(components.data(), components.size(), linkedProgram.writeRef());
    }
    else
    {
        for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
        {
            if (desc.slangGlobalScope)
            {
                slang::IComponentType* entryPointComponents[2] = {desc.slangGlobalScope, desc.slangEntryPoints[i]};
                ComPtr<slang::IComponentType> linkedEntryPoint;
                session->createCompositeComponentType(entryPointComponents, 2, linkedEntryPoint.writeRef());
                linkedEntryPoints.push_back(linkedEntryPoint);
            }
            else
            {
                linkedEntryPoints.push_back(ComPtr<slang::IComponentType>(desc.slangEntryPoints[i]));
            }
        }
        linkedProgram = desc.slangGlobalScope;
    }

    m_isSpecializable = _isSpecializable();
}

Result ShaderProgram::compileShaders(Device* device)
{
    if (m_compiledShaders)
        return SLANG_OK;

    if (device->getDeviceInfo().deviceType == DeviceType::CPU)
    {
        // CPU device does not need to compile shaders.
        m_compiledShaders = true;
        return SLANG_OK;
    }

    // For a fully specialized program, read and store its kernel code in `shaderProgram`.
    auto compileShader = [&](slang::EntryPointReflection* entryPointInfo,
                             slang::IComponentType* entryPointComponent,
                             SlangInt entryPointIndex)
    {
        ComPtr<ISlangBlob> kernelCode;
        ComPtr<ISlangBlob> diagnostics;
        auto compileResult = device->getEntryPointCodeFromShaderCache(
            entryPointComponent,
            entryPointIndex,
            0,
            kernelCode.writeRef(),
            diagnostics.writeRef()
        );
        if (diagnostics)
        {
            DebugMessageType msgType = DebugMessageType::Warning;
            if (compileResult != SLANG_OK)
                msgType = DebugMessageType::Error;
            device->handleMessage(msgType, DebugMessageSource::Slang, (char*)diagnostics->getBufferPointer());
        }
        SLANG_RETURN_ON_FAIL(compileResult);
        SLANG_RETURN_ON_FAIL(createShaderModule(entryPointInfo, kernelCode));
        return SLANG_OK;
    };

    if (linkedEntryPoints.size() == 0)
    {
        // If the user does not explicitly specify entry point components, find them from
        // `linkedEntryPoints`.
        auto programReflection = linkedProgram->getLayout();
        for (SlangUInt i = 0; i < programReflection->getEntryPointCount(); i++)
        {
            SLANG_RETURN_ON_FAIL(compileShader(programReflection->getEntryPointByIndex(i), linkedProgram, (SlangInt)i));
        }
    }
    else
    {
        // If the user specifies entry point components via the separated entry point array,
        // compile code from there.
        for (auto& entryPoint : linkedEntryPoints)
        {
            SLANG_RETURN_ON_FAIL(compileShader(entryPoint->getLayout()->getEntryPointByIndex(0), entryPoint, 0));
        }
    }

    m_compiledShaders = true;

    return SLANG_OK;
}

Result ShaderProgram::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    SLANG_UNUSED(entryPointInfo);
    SLANG_UNUSED(kernelCode);
    return SLANG_OK;
}

bool ShaderProgram::isMeshShaderProgram() const
{
    // Similar to above, interrogate either explicity specified entry point
    // componenets or the ones in the linked program entry point array
    if (linkedEntryPoints.size())
    {
        for (const auto& e : linkedEntryPoints)
            if (e->getLayout()->getEntryPointByIndex(0)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    else
    {
        const auto programReflection = linkedProgram->getLayout();
        for (SlangUInt i = 0; i < programReflection->getEntryPointCount(); ++i)
            if (programReflection->getEntryPointByIndex(i)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    return false;
}

Result Device::createShaderObject(ShaderObjectLayout* layout, ShaderObject** outObject)
{
    return ShaderObject::create(this, layout, outObject);
}

Result Device::createRootShaderObject(ShaderProgram* program, RootShaderObject** outObject)
{
    return RootShaderObject::create(this, program, outObject);
}

Result Device::getSpecializedProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    // TODO make thread-safe
    SpecializationKey key(specializationArgs);
    auto it = program->m_specializedPrograms.find(key);
    if (it != program->m_specializedPrograms.end())
    {
        returnRefPtr(outSpecializedProgram, it->second);
        return SLANG_OK;
    }
    else
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(specializeProgram(program, specializationArgs, specializedProgram.writeRef()));
        program->m_specializedPrograms[key] = specializedProgram;
        // Program is owned by the cache
        specializedProgram->comFree();
        returnRefPtr(outSpecializedProgram, specializedProgram);
        return SLANG_OK;
    }
}


Result Device::specializeProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    ComPtr<slang::IComponentType> specializedComponentType;
    ComPtr<slang::IBlob> diagnosticBlob;
    Result result = program->linkedProgram->specialize(
        specializationArgs.components.data(),
        specializationArgs.getCount(),
        specializedComponentType.writeRef(),
        diagnosticBlob.writeRef()
    );
    if (diagnosticBlob)
    {
        handleMessage(
            result == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
            DebugMessageSource::Slang,
            (char*)diagnosticBlob->getBufferPointer()
        );
    }
    SLANG_RETURN_ON_FAIL(result);

    // Now create the specialized shader program using compiled binaries.
    RefPtr<ShaderProgram> specializedProgram;
    ShaderProgramDesc programDesc = program->m_desc;
    programDesc.slangGlobalScope = specializedComponentType;

    if (programDesc.linkingStyle == LinkingStyle::SingleProgram)
    {
        // When linking style is SingleProgram, the specialized global scope already contains
        // entry-points, so we do not need to supply them again when creating the specialized
        // pipeline.
        programDesc.slangEntryPointCount = 0;
    }
    SLANG_RETURN_ON_FAIL(createShaderProgram(programDesc, (IShaderProgram**)specializedProgram.writeRef()));
    returnRefPtr(outSpecializedProgram, specializedProgram);
    return SLANG_OK;
}

Result Device::getConcretePipeline(
    Pipeline* pipeline,
    ExtendedShaderObjectTypeList* specializationArgs,
    Pipeline*& outPipeline
)
{
    // If this is already a concrete pipeline, then we are done.
    if (!pipeline->isVirtual())
    {
        outPipeline = pipeline;
        return SLANG_OK;
    }

    // Create key for looking up cached pipelines.
    PipelineKey pipelineKey;
    pipelineKey.pipeline = pipeline;

    // If the pipeline is specializable, collect specialization arguments from bound shader objects.
    if (pipeline->m_program->isSpecializable())
    {
        if (!specializationArgs)
            return SLANG_FAIL;
        for (const auto& componentID : specializationArgs->componentIDs)
        {
            pipelineKey.specializationArgs.push_back(componentID);
        }
    }

    // Look up pipeline in cache.
    pipelineKey.updateHash();
    RefPtr<Pipeline> concretePipeline = m_shaderCache.getSpecializedPipeline(pipelineKey);
    if (!concretePipeline)
    {
        // Specialize program if needed.
        RefPtr<ShaderProgram> program = pipeline->m_program;
        if (program->isSpecializable())
        {
            RefPtr<ShaderProgram> specializedProgram;
            SLANG_RETURN_ON_FAIL(specializeProgram(program, *specializationArgs, specializedProgram.writeRef()));
            program = specializedProgram;
            // Program is owned by the specialized pipeline.
            program->comFree();
        }

        // Ensure sure shaders are compiled.
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));

        switch (pipeline->getType())
        {
        case PipelineType::Render:
        {
            RenderPipelineDesc desc = checked_cast<VirtualRenderPipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IRenderPipeline> renderPipeline;
            SLANG_RETURN_ON_FAIL(createRenderPipeline2(desc, renderPipeline.writeRef()));
            concretePipeline = checked_cast<RenderPipeline*>(renderPipeline.get());
            break;
        }
        case PipelineType::Compute:
        {
            ComputePipelineDesc desc = checked_cast<VirtualComputePipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IComputePipeline> computePipeline;
            SLANG_RETURN_ON_FAIL(createComputePipeline2(desc, computePipeline.writeRef()));
            concretePipeline = checked_cast<ComputePipeline*>(computePipeline.get());
            break;
        }
        case PipelineType::RayTracing:
        {
            RayTracingPipelineDesc desc = checked_cast<VirtualRayTracingPipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IRayTracingPipeline> rayTracingPipeline;
            SLANG_RETURN_ON_FAIL(createRayTracingPipeline2(desc, rayTracingPipeline.writeRef()));
            concretePipeline = checked_cast<RayTracingPipeline*>(rayTracingPipeline.get());
            break;
        }
        }
        m_shaderCache.addSpecializedPipeline(pipelineKey, concretePipeline);
        // Pipeline is owned by the cache.
        concretePipeline->comFree();
    }

    outPipeline = concretePipeline;
    return SLANG_OK;
}

Result Device::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result ShaderTable::init(const ShaderTableDesc& desc)
{
    m_rayGenShaderCount = desc.rayGenShaderCount;
    m_missShaderCount = desc.missShaderCount;
    m_hitGroupCount = desc.hitGroupCount;
    m_callableShaderCount = desc.callableShaderCount;
    m_shaderGroupNames.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    m_recordOverwrites.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    for (uint32_t i = 0; i < desc.rayGenShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.rayGenShaderEntryPointNames[i]);
        if (desc.rayGenShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.rayGenShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.missShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.missShaderEntryPointNames[i]);
        if (desc.missShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.missShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.hitGroupCount; i++)
    {
        m_shaderGroupNames.push_back(desc.hitGroupNames[i]);
        if (desc.hitGroupRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.hitGroupRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.callableShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.callableShaderEntryPointNames[i]);
        if (desc.callableShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.callableShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    return SLANG_OK;
}

ISurface* Surface::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ISurface::getTypeGuid())
        return static_cast<ISurface*>(this);
    return nullptr;
}

void Surface::setInfo(const SurfaceInfo& info)
{
    m_info = info;
    m_infoHolder.reset();
    m_infoHolder.holdList(m_info.formats, m_info.formatCount);
}

void Surface::setConfig(const SurfaceConfig& config)
{
    m_config = config;
}

bool isDepthFormat(Format format)
{
    switch (format)
    {
    case Format::D16_UNORM:
    case Format::D32_FLOAT:
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(Format format)
{
    switch (format)
    {
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

} // namespace rhi
