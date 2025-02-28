#include "device.h"

#include "rhi-shared.h"
#include "shader-object.h"

#include <algorithm>

namespace rhi {

// ----------------------------------------------------------------------------
// ShaderCache
// ----------------------------------------------------------------------------


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

// ----------------------------------------------------------------------------
// Device
// ----------------------------------------------------------------------------


Result Device::getEntryPointCodeFromShaderCache(
    slang::IComponentType* program,
    SlangInt entryPointIndex,
    SlangInt targetIndex,
    slang::IBlob** outCode,
    slang::IBlob** outDiagnostics
)
{
    // Immediately call getEntryPointCode if shader cache is not available.
    if (!m_persistentShaderCache)
    {
        return program->getEntryPointCode(entryPointIndex, targetIndex, outCode, outDiagnostics);
    }

    // Hash all relevant state for generating the entry point shader code to use as a key
    // for the shader cache.
    ComPtr<ISlangBlob> hashBlob;
    program->getEntryPointHash(entryPointIndex, targetIndex, hashBlob.writeRef());

    // Query the shader cache.
    ComPtr<ISlangBlob> codeBlob;
    if (m_persistentShaderCache->queryCache(hashBlob, codeBlob.writeRef()) != SLANG_OK)
    {
        // No cached entry found. Generate the code and add it to the cache.
        SLANG_RETURN_ON_FAIL(
            program->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), outDiagnostics)
        );
        m_persistentShaderCache->writeCache(hashBlob, codeBlob);
    }

    *outCode = codeBlob.detach();
    return SLANG_OK;
}

Result Device::queryInterface(const SlangUUID& uuid, void** outObject)
{
    *outObject = getInterface(uuid);
    return SLANG_OK;
}

IDevice* Device::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IDevice::getTypeGuid())
        return static_cast<IDevice*>(this);
    return nullptr;
}

Result Device::initialize(const DeviceDesc& desc)
{
    m_debugCallback = desc.debugCallback ? desc.debugCallback : NullDebugCallback::getInstance();

    m_persistentShaderCache = desc.persistentShaderCache;

    if (desc.apiCommandDispatcher)
    {
        desc.apiCommandDispatcher->queryInterface(
            IPipelineCreationAPIDispatcher::getTypeGuid(),
            (void**)m_pipelineCreationAPIDispatcher.writeRef()
        );
    }
    return SLANG_OK;
}

Result Device::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    return SLANG_OK;
}

Result Device::getFeatures(const char** outFeatures, size_t bufferSize, uint32_t* outFeatureCount)
{
    if (bufferSize >= m_features.size())
    {
        for (size_t i = 0; i < m_features.size(); i++)
        {
            outFeatures[i] = m_features[i].data();
        }
    }
    if (outFeatureCount)
        *outFeatureCount = (uint32_t)m_features.size();
    return SLANG_OK;
}

bool Device::hasFeature(const char* featureName)
{
    return std::any_of(
        m_features.begin(),
        m_features.end(),
        [&](const std::string& feature) { return feature == featureName; }
    );
}

Result Device::getFormatSupport(Format format, FormatSupport* outFormatSupport)
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

Result Device::getSlangSession(slang::ISession** outSlangSession)
{
    *outSlangSession = m_slangContext.session.get();
    m_slangContext.session->addRef();
    return SLANG_OK;
}

Result Device::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createTextureFromSharedHandle(
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

Result Device::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outLayout);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRenderPipeline> pipeline = new VirtualRenderPipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRenderPipeline2(desc, outPipeline);
    }
}

Result Device::createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualComputePipeline> pipeline = new VirtualComputePipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createComputePipeline2(desc, outPipeline);
    }
}

Result Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRayTracingPipeline> pipeline = new VirtualRayTracingPipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRayTracingPipeline2(desc, outPipeline);
    }
}

Result Device::createShaderObject(
    slang::ISession* slangSession,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    IShaderObject** outObject
)
{
    if (slangSession == nullptr)
        slangSession = m_slangContext.session.get();
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, container, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(m_slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    ShaderProgram* shaderProgram = checked_cast<ShaderProgram*>(program);
    RefPtr<RootShaderObject> rootShaderObject;
    SLANG_RETURN_ON_FAIL(createRootShaderObject(shaderProgram, rootShaderObject.writeRef()));
    returnComPtr(outObject, rootShaderObject);
    return SLANG_OK;
}

Result Device::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outSizes);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outAccelerationStructure);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outTable);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_UNUSED(desc);
    *outFence = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::waitForFences(
    uint32_t fenceCount,
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

Result Device::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    SLANG_UNUSED(desc);
    *outSize = 0;
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getTextureRowAlignment(Size* outAlignment)
{
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_UNUSED(windowHandle);
    *outSurface = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount)
{
    if (m_cooperativeVectorProperties.empty())
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    if (*propertyCount == 0)
    {
        *propertyCount = uint32_t(m_cooperativeVectorProperties.size());
        return SLANG_OK;
    }
    else
    {
        uint32_t count = min(*propertyCount, uint32_t(m_cooperativeVectorProperties.size()));
        ::memcpy(properties, m_cooperativeVectorProperties.data(), count * sizeof(CooperativeVectorProperties));
        Result result = count == *propertyCount ? SLANG_OK : SLANG_E_BUFFER_TOO_SMALL;
        *propertyCount = count;
        return result;
    }
}

Result Device::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    SLANG_UNUSED(descs);
    SLANG_UNUSED(descCount);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getShaderObjectLayout(
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

Result Device::getShaderObjectLayout(
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

}
