#include "renderer-shared.h"
#include "mutable-shader-object.h"

#include "core/common.h"

#include <slang.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

const Guid GUID::IID_ISlangUnknown = SLANG_UUID_ISlangUnknown;
const Guid GUID::IID_IShaderProgram = IShaderProgram::getTypeGuid();
const Guid GUID::IID_IInputLayout = IInputLayout::getTypeGuid();
const Guid GUID::IID_IPipeline = IPipeline::getTypeGuid();
const Guid GUID::IID_ITransientResourceHeap = ITransientResourceHeap::getTypeGuid();

const Guid GUID::IID_ISwapchain = ISwapchain::getTypeGuid();
const Guid GUID::IID_ISampler = ISampler::getTypeGuid();
const Guid GUID::IID_IResource = IResource::getTypeGuid();
const Guid GUID::IID_IBuffer = IBuffer::getTypeGuid();
const Guid GUID::IID_ITexture = ITexture::getTypeGuid();
const Guid GUID::IID_ITextureView = ITextureView::getTypeGuid();
const Guid GUID::IID_IDevice = IDevice::getTypeGuid();
const Guid GUID::IID_IPersistentShaderCache = IPersistentShaderCache::getTypeGuid();
const Guid GUID::IID_IShaderObject = IShaderObject::getTypeGuid();

const Guid GUID::IID_ICommandEncoder = ICommandEncoder::getTypeGuid();
const Guid GUID::IID_IResourceCommandEncoder = IResourceCommandEncoder::getTypeGuid();
const Guid GUID::IID_IRenderCommandEncoder = IRenderCommandEncoder::getTypeGuid();
const Guid GUID::IID_IComputeCommandEncoder = IComputeCommandEncoder::getTypeGuid();
const Guid GUID::IID_IRayTracingCommandEncoder = IRayTracingCommandEncoder::getTypeGuid();

const Guid GUID::IID_ICommandBuffer = ICommandBuffer::getTypeGuid();
const Guid GUID::IID_ICommandBufferD3D12 = ICommandBufferD3D12::getTypeGuid();

const Guid GUID::IID_ICommandQueue = ICommandQueue::getTypeGuid();
const Guid GUID::IID_IQueryPool = IQueryPool::getTypeGuid();
const Guid GUID::IID_IAccelerationStructure = IAccelerationStructure::getTypeGuid();
const Guid GUID::IID_IFence = IFence::getTypeGuid();
const Guid GUID::IID_IShaderTable = IShaderTable::getTypeGuid();
const Guid GUID::IID_IPipelineCreationAPIDispatcher = IPipelineCreationAPIDispatcher::getTypeGuid();
const Guid GUID::IID_ITransientResourceHeapD3D12 = ITransientResourceHeapD3D12::getTypeGuid();

IFence* FenceBase::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IFence)
        return static_cast<IFence*>(this);
    return nullptr;
}

IResource* Buffer::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IResource || guid == GUID::IID_IBuffer)
        return static_cast<IBuffer*>(this);
    return nullptr;
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

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = std::min(resolved.offset, m_desc.size);
    resolved.size = std::min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IResource || guid == GUID::IID_ITexture)
        return static_cast<ITexture*>(this);
    return nullptr;
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

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.mipLevel = std::min(resolved.mipLevel, m_desc.numMipLevels);
    resolved.mipLevelCount = std::min(resolved.mipLevelCount, m_desc.numMipLevels - resolved.mipLevel);
    resolved.baseArrayLayer = std::min(resolved.baseArrayLayer, m_desc.arraySize);
    resolved.layerCount = std::min(resolved.layerCount, m_desc.arraySize - resolved.baseArrayLayer);
    return resolved;
}

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IResource || guid == GUID::IID_ITextureView)
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
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IResource || guid == GUID::IID_ISampler)
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
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IResource || guid == GUID::IID_IAccelerationStructure)
        return static_cast<IAccelerationStructure*>(this);
    return nullptr;
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
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IShaderProgram)
        return static_cast<IShaderProgram*>(this);
    return nullptr;
}

IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IInputLayout)
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IQueryPool)
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

IPipeline* Pipeline::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IPipeline)
        return static_cast<IPipeline*>(this);
    return nullptr;
}

Result Pipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_IMPLEMENTED;
}

void Pipeline::initializeBase(const PipelineStateDesc& inDesc)
{
    desc = inDesc;

    descHolder.holdList(desc.graphics.targets, desc.graphics.targetCount);
    descHolder.holdList(desc.rayTracing.hitGroups, desc.rayTracing.hitGroupCount);
    for (Index i = 0; i < desc.rayTracing.hitGroupCount; i++)
    {
        descHolder.holdString(desc.rayTracing.hitGroups[i].hitGroupName);
        descHolder.holdString(desc.rayTracing.hitGroups[i].closestHitEntryPoint);
        descHolder.holdString(desc.rayTracing.hitGroups[i].anyHitEntryPoint);
        descHolder.holdString(desc.rayTracing.hitGroups[i].intersectionEntryPoint);
    }

    auto program = desc.getProgram();
    m_program = program;
    isSpecializable = false;
    if (program->slangGlobalScope && program->slangGlobalScope->getSpecializationParamCount() != 0)
        isSpecializable = true;
    for (auto& entryPoint : program->slangEntryPoints)
    {
        if (entryPoint->getSpecializationParamCount() != 0)
        {
            isSpecializable = true;
            break;
        }
    }
    // Hold a strong reference to inputLayout object to prevent it from destruction.
    if (inDesc.type == PipelineType::Graphics)
    {
        inputLayout = static_cast<InputLayout*>(inDesc.graphics.inputLayout);
    }
}

Result RendererBase::getEntryPointCodeFromShaderCache(
    slang::IComponentType* program,
    SlangInt entryPointIndex,
    SlangInt targetIndex,
    slang::IBlob** outCode,
    slang::IBlob** outDiagnostics
)
{
    // Immediately call getEntryPointCode if shader cache is not available.
    if (!persistentShaderCache)
    {
        return program->getEntryPointCode(entryPointIndex, targetIndex, outCode, outDiagnostics);
    }

    // Hash all relevant state for generating the entry point shader code to use as a key
    // for the shader cache.
    ComPtr<ISlangBlob> hashBlob;
    program->getEntryPointHash(entryPointIndex, targetIndex, hashBlob.writeRef());

    // Query the shader cache.
    ComPtr<ISlangBlob> codeBlob;
    if (persistentShaderCache->queryCache(hashBlob, codeBlob.writeRef()) != SLANG_OK)
    {
        // No cached entry found. Generate the code and add it to the cache.
        SLANG_RETURN_ON_FAIL(
            program->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), outDiagnostics)
        );
        persistentShaderCache->writeCache(hashBlob, codeBlob);
    }

    *outCode = codeBlob.detach();
    return SLANG_OK;
}

Result RendererBase::queryInterface(SlangUUID const& uuid, void** outObject)
{
    *outObject = getInterface(uuid);
    return SLANG_OK;
}

IDevice* RendererBase::getInterface(const Guid& guid)
{
    return (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_IDevice) ? static_cast<IDevice*>(this) : nullptr;
}

Result RendererBase::initialize(const Desc& desc)
{
    persistentShaderCache = desc.persistentShaderCache;

    if (desc.apiCommandDispatcher)
    {
        desc.apiCommandDispatcher->queryInterface(
            GUID::IID_IPipelineCreationAPIDispatcher,
            (void**)m_pipelineCreationAPIDispatcher.writeRef()
        );
    }
    return SLANG_OK;
}

Result RendererBase::getNativeDeviceHandles(NativeHandles* outHandles)
{
    return SLANG_OK;
}

Result RendererBase::getFeatures(const char** outFeatures, Size bufferSize, GfxCount* outFeatureCount)
{
    if (bufferSize >= (UInt)m_features.size())
    {
        for (Index i = 0; i < m_features.size(); i++)
        {
            outFeatures[i] = m_features[i].data();
        }
    }
    if (outFeatureCount)
        *outFeatureCount = (GfxCount)m_features.size();
    return SLANG_OK;
}

bool RendererBase::hasFeature(const char* featureName)
{
    return std::any_of(
        m_features.begin(),
        m_features.end(),
        [&](const std::string& feature) { return feature == featureName; }
    );
}

Result RendererBase::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_UNUSED(format);
    FormatSupport support = FormatSupport::None;
    support |= FormatSupport::Buffer;
    support |= FormatSupport::IndexBuffer;
    support |= FormatSupport::VertexBuffer;
    support |= FormatSupport::Texture;
    support |= FormatSupport::DepthStencil;
    support |= FormatSupport::RenderTarget;
    support |= FormatSupport::Blendable;
    support |= FormatSupport::ShaderLoad;
    support |= FormatSupport::ShaderSample;
    support |= FormatSupport::ShaderUavLoad;
    support |= FormatSupport::ShaderUavStore;
    support |= FormatSupport::ShaderAtomic;
    *outFormatSupport = support;
    return SLANG_OK;
}

Result RendererBase::getSlangSession(slang::ISession** outSlangSession)
{
    *outSlangSession = slangContext.session.get();
    slangContext.session->addRef();
    return SLANG_OK;
}

Result RendererBase::createTextureFromNativeHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    ITexture** outTexture
)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    const Size size,
    ITexture** outTexture
)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(size);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createShaderObject(
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    IShaderObject** outObject
)
{
    return createShaderObject2(slangContext.session, type, container, outObject);
}

Result RendererBase::createShaderObject2(
    slang::ISession* slangSession,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    IShaderObject** outObject
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, container, shaderObjectLayout.writeRef()));
    return createShaderObject(shaderObjectLayout, outObject);
}

Result RendererBase::createMutableShaderObject(
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outObject
)
{
    return createMutableShaderObject2(slangContext.session, type, containerType, outObject);
}

Result RendererBase::createMutableShaderObject2(
    slang::ISession* slangSession,
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outObject
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, containerType, shaderObjectLayout.writeRef()));
    return createMutableShaderObject(shaderObjectLayout, outObject);
}

Result RendererBase::createShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outObject
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    return createShaderObject(shaderObjectLayout, outObject);
}

Result RendererBase::createMutableShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outObject
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    return createMutableShaderObject(shaderObjectLayout, outObject);
}

Result RendererBase::getAccelerationStructurePrebuildInfo(
    const IAccelerationStructure::BuildInputs& buildInputs,
    IAccelerationStructure::PrebuildInfo* outPrebuildInfo
)
{
    SLANG_UNUSED(buildInputs);
    SLANG_UNUSED(outPrebuildInfo);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createAccelerationStructure(
    const IAccelerationStructure::CreateDesc& desc,
    IAccelerationStructure** outView
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outView);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outTable)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outTable);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    SLANG_UNUSED(program);
    SLANG_UNUSED(outObject);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_UNUSED(desc);
    *outFence = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::waitForFences(
    GfxCount fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    SLANG_UNUSED(fenceCount);
    SLANG_UNUSED(fences);
    SLANG_UNUSED(fenceValues);
    SLANG_UNUSED(waitForAll);
    SLANG_UNUSED(timeout);
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    SLANG_UNUSED(desc);
    *outSize = 0;
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::getTextureRowAlignment(Size* outAlignment)
{
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result RendererBase::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    ShaderObjectLayout** outLayout
)
{
    switch (container)
    {
    case ShaderObjectContainerType::StructuredBuffer:
        type = session->getContainerType(type, slang::ContainerType::StructuredBuffer);
        break;
    case ShaderObjectContainerType::Array:
        type = session->getContainerType(type, slang::ContainerType::UnsizedArray);
        break;
    default:
        break;
    }

    auto typeLayout = session->getTypeLayout(type);
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(session, typeLayout, outLayout));
    (*outLayout)->m_slangSession = session;
    return SLANG_OK;
}

Result RendererBase::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    auto it = m_shaderObjectLayoutCache.find(typeLayout);
    if (it != m_shaderObjectLayoutCache.end())
    {
        shaderObjectLayout = it->second;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(createShaderObjectLayout(session, typeLayout, shaderObjectLayout.writeRef()));
        m_shaderObjectLayoutCache.emplace(typeLayout, shaderObjectLayout);
    }
    *outLayout = shaderObjectLayout.detach();
    return SLANG_OK;
}

ShaderComponentID ShaderCache::getComponentId(slang::TypeReflection* type)
{
    ComponentKey key;
    key.typeName = string::from_cstr(type->getName());
    switch (type->getKind())
    {
    case slang::TypeReflection::Kind::Specialized:
    {
        auto baseType = type->getElementType();

        std::string str;
        str += string::from_cstr(baseType->getName());

        auto rawType = (SlangReflectionType*)type;

        str += '<';
        SlangInt argCount = spReflectionType_getSpecializedTypeArgCount(rawType);
        for (SlangInt a = 0; a < argCount; ++a)
        {
            if (a != 0)
                str += ',';
            if (auto rawArgType = spReflectionType_getSpecializedTypeArgType(rawType, a))
            {
                auto argType = (slang::TypeReflection*)rawArgType;
                str += string::from_cstr(argType->getName());
            }
        }
        str += '>';
        key.typeName = std::move(str);
        key.updateHash();
        return getComponentId(key);
    }
        // TODO: collect specialization arguments and append them to `key`.
        SLANG_RHI_UNIMPLEMENTED("specialized type");
    default:
        break;
    }
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(std::string_view name)
{
    ComponentKey key;
    key.typeName = name;
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(ComponentKey key)
{
    auto it = componentIds.find(key);
    if (it != componentIds.end())
        return it->second;
    ShaderComponentID resultId = static_cast<ShaderComponentID>(componentIds.size());
    componentIds.emplace(key, resultId);
    return resultId;
}

void ShaderCache::addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline)
{
    specializedPipelines[key] = specializedPipeline;
}

void ShaderObjectLayout::initBase(
    RendererBase* renderer,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementTypeLayout
)
{
    m_renderer = renderer;
    m_slangSession = session;
    m_elementTypeLayout = elementTypeLayout;
    m_componentID = m_renderer->shaderCache.getComponentId(m_elementTypeLayout->getType());
}

// Get the final type this shader object represents. If the shader object's type has existential fields,
// this function will return a specialized type using the bound sub-objects' type as specialization argument.
Result ShaderObjectBase::getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    return _getSpecializedShaderObjectType(outType);
}

Result ShaderObjectBase::_getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    if (shaderObjectType.slangType)
        *outType = shaderObjectType;
    ExtendedShaderObjectTypeList specializationArgs;
    SLANG_RETURN_ON_FAIL(collectSpecializationArgs(specializationArgs));
    if (specializationArgs.getCount() == 0)
    {
        shaderObjectType.componentID = getLayoutBase()->getComponentID();
        shaderObjectType.slangType = getLayoutBase()->getElementTypeLayout()->getType();
    }
    else
    {
        shaderObjectType.slangType = getRenderer()->slangContext.session->specializeType(
            _getElementTypeLayout()->getType(),
            specializationArgs.components.data(),
            specializationArgs.getCount()
        );
        shaderObjectType.componentID = getRenderer()->shaderCache.getComponentId(shaderObjectType.slangType);
    }
    *outType = shaderObjectType;
    return SLANG_OK;
}

Result ShaderObjectBase::setExistentialHeader(
    slang::TypeReflection* existentialType,
    slang::TypeReflection* concreteType,
    ShaderOffset offset
)
{
    // The first field of the tuple (offset zero) is the run-time type information
    // (RTTI) ID for the concrete type being stored into the field.
    //
    // TODO: We need to be able to gather the RTTI type ID from `object` and then
    // use `setData(offset, &TypeID, sizeof(TypeID))`.

    // The second field of the tuple (offset 8) is the ID of the "witness" for the
    // conformance of the concrete type to the interface used by this field.
    //
    auto witnessTableOffset = offset;
    witnessTableOffset.uniformOffset += 8;
    //
    // Conformances of a type to an interface are computed and then stored by the
    // Slang runtime, so we can look up the ID for this particular conformance (which
    // will create it on demand).
    //
    // Note: If the type doesn't actually conform to the required interface for
    // this sub-object range, then this is the point where we will detect that
    // fact and error out.
    //
    uint32_t conformanceID = 0xFFFFFFFF;
    SLANG_RETURN_ON_FAIL(getLayoutBase()->m_slangSession->getTypeConformanceWitnessSequentialID(
        concreteType,
        existentialType,
        &conformanceID
    ));
    //
    // Once we have the conformance ID, then we can write it into the object
    // at the required offset.
    //
    SLANG_RETURN_ON_FAIL(setData(witnessTableOffset, &conformanceID, sizeof(conformanceID)));

    return SLANG_OK;
}

Buffer* SimpleShaderObjectData::getBufferResource(
    RendererBase* device,
    slang::TypeLayoutReflection* elementLayout,
    slang::BindingType bindingType
)
{
    if (!m_structuredBuffer)
    {
        // Create structured buffer resource if it has not been created.
        BufferDesc desc = {};
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
        desc.defaultState = ResourceState::ShaderResource;
        desc.elementSize = (int)elementLayout->getSize();
        desc.format = Format::Unknown;
        desc.size = (Size)m_ordinaryData.size();
        ComPtr<IBuffer> buffer;
        SLANG_RETURN_NULL_ON_FAIL(device->createBuffer(desc, m_ordinaryData.data(), buffer.writeRef()));
        m_structuredBuffer = static_cast<Buffer*>(buffer.get());
    }

    return m_structuredBuffer;
}

void ShaderProgram::init(const ShaderProgramDesc& inDesc)
{
    desc = inDesc;

    slangGlobalScope = desc.slangGlobalScope;
    for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
        for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
        for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
}

Result ShaderProgram::compileShaders(RendererBase* device)
{
    // For a fully specialized program, read and store its kernel code in `shaderProgram`.
    auto compileShader = [&](slang::EntryPointReflection* entryPointInfo,
                             slang::IComponentType* entryPointComponent,
                             SlangInt entryPointIndex)
    {
        auto stage = entryPointInfo->getStage();
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
            getDebugCallback()
                ->handleMessage(msgType, DebugMessageSource::Slang, (char*)diagnostics->getBufferPointer());
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

Result RendererBase::maybeSpecializePipeline(
    Pipeline* currentPipeline,
    ShaderObjectBase* rootObject,
    RefPtr<Pipeline>& outNewPipeline
)
{
    outNewPipeline = static_cast<Pipeline*>(currentPipeline);

    auto pipelineType = currentPipeline->desc.type;
    if (currentPipeline->unspecializedPipeline)
        currentPipeline = currentPipeline->unspecializedPipeline;
    // If the currently bound pipeline is specializable, we need to specialize it based on bound shader objects.
    if (currentPipeline->isSpecializable)
    {
        specializationArgs.clear();
        SLANG_RETURN_ON_FAIL(rootObject->collectSpecializationArgs(specializationArgs));

        // Construct a shader cache key that represents the specialized shader kernels.
        PipelineKey pipelineKey;
        pipelineKey.pipeline = currentPipeline;
        for (const auto& componentID : specializationArgs.componentIDs)
        {
            pipelineKey.specializationArgs.push_back(componentID);
        }
        pipelineKey.updateHash();

        RefPtr<Pipeline> specializedPipeline = shaderCache.getSpecializedPipeline(pipelineKey);
        // Try to find specialized pipeline from shader cache.
        if (!specializedPipeline)
        {
            auto unspecializedProgram = static_cast<ShaderProgram*>(
                pipelineType == PipelineType::Compute ? currentPipeline->desc.compute.program
                                                      : currentPipeline->desc.graphics.program
            );
            auto unspecializedProgramLayout = unspecializedProgram->linkedProgram->getLayout();

            ComPtr<slang::IComponentType> specializedComponentType;
            ComPtr<slang::IBlob> diagnosticBlob;
            auto compileRs = unspecializedProgram->linkedProgram->specialize(
                specializationArgs.components.data(),
                specializationArgs.getCount(),
                specializedComponentType.writeRef(),
                diagnosticBlob.writeRef()
            );
            if (diagnosticBlob)
            {
                getDebugCallback()->handleMessage(
                    compileRs == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
                    DebugMessageSource::Slang,
                    (char*)diagnosticBlob->getBufferPointer()
                );
            }
            SLANG_RETURN_ON_FAIL(compileRs);

            // Now create the specialized shader program using compiled binaries.
            ComPtr<IShaderProgram> specializedProgram;
            ShaderProgramDesc specializedProgramDesc = unspecializedProgram->desc;
            specializedProgramDesc.slangGlobalScope = specializedComponentType;

            if (specializedProgramDesc.linkingStyle == LinkingStyle::SingleProgram)
            {
                // When linking style is GraphicsCompute, the specialized global scope already contains
                // entry-points, so we do not need to supply them again when creating the specialized
                // pipeline.
                specializedProgramDesc.slangEntryPointCount = 0;
            }
            SLANG_RETURN_ON_FAIL(createShaderProgram(specializedProgramDesc, specializedProgram.writeRef()));

            // Create specialized pipeline state.
            ComPtr<IPipeline> specializedPipelineComPtr;
            switch (pipelineType)
            {
            case PipelineType::Compute:
            {
                auto pipelineDesc = currentPipeline->desc.compute;
                pipelineDesc.program = specializedProgram;
                SLANG_RETURN_ON_FAIL(createComputePipeline(pipelineDesc, specializedPipelineComPtr.writeRef()));
                break;
            }
            case PipelineType::Graphics:
            {
                auto pipelineDesc = currentPipeline->desc.graphics;
                pipelineDesc.program = static_cast<ShaderProgram*>(specializedProgram.get());
                SLANG_RETURN_ON_FAIL(createRenderPipeline(pipelineDesc, specializedPipelineComPtr.writeRef()));
                break;
            }
            case PipelineType::RayTracing:
            {
                auto pipelineDesc = currentPipeline->desc.rayTracing;
                pipelineDesc.program = static_cast<ShaderProgram*>(specializedProgram.get());
                SLANG_RETURN_ON_FAIL(createRayTracingPipeline(pipelineDesc, specializedPipelineComPtr.writeRef()));
                break;
            }
            default:
                break;
            }
            specializedPipeline = static_cast<Pipeline*>(specializedPipelineComPtr.get());
            specializedPipeline->unspecializedPipeline = currentPipeline;
            shaderCache.addSpecializedPipeline(pipelineKey, specializedPipeline);
        }
        auto specializedPipelineBase = static_cast<Pipeline*>(specializedPipeline.Ptr());
        outNewPipeline = specializedPipelineBase;
    }
    return SLANG_OK;
}

IDebugCallback*& _getDebugCallback()
{
    static IDebugCallback* callback = nullptr;
    return callback;
}

class NullDebugCallback : public IDebugCallback
{
public:
    virtual void handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        SLANG_UNUSED(type);
        SLANG_UNUSED(source);
        SLANG_UNUSED(message);
    }
};
IDebugCallback* _getNullDebugCallback()
{
    static NullDebugCallback result = {};
    return &result;
}

Result ShaderObjectBase::copyFrom(IShaderObject* object, ITransientResourceHeap* transientHeap)
{
    if (auto srcObj = dynamic_cast<MutableRootShaderObject*>(object))
    {
        setData(ShaderOffset(), srcObj->m_data.data(), (size_t)srcObj->m_data.size()); // TODO: Change size_t to Count?
        for (auto it : srcObj->m_objects)
        {
            ComPtr<IShaderObject> subObject;
            SLANG_RETURN_ON_FAIL(it.second->getCurrentVersion(transientHeap, subObject.writeRef()));
            setObject(it.first, subObject);
        }
        for (auto it : srcObj->m_bindings)
        {
            setBinding(it.first, it.second);
        }
        for (auto it : srcObj->m_specializationArgs)
        {
            setSpecializationArgs(it.first, it.second.data(), (uint32_t)it.second.size());
        }
        return SLANG_OK;
    }
    return SLANG_FAIL;
}

Result ShaderTable::init(const IShaderTable::Desc& desc)
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
    for (GfxIndex i = 0; i < desc.rayGenShaderCount; i++)
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
    for (GfxIndex i = 0; i < desc.missShaderCount; i++)
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
    for (GfxIndex i = 0; i < desc.hitGroupCount; i++)
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
    for (GfxIndex i = 0; i < desc.callableShaderCount; i++)
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
