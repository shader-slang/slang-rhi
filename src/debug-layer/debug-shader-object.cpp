#include "debug-shader-object.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

namespace {

// Returns the shared resource to track for one binding operand, or null if the operand is neither a
// Shared buffer nor a view of a Shared texture. A texture binding arrives as an ITextureView, which
// exposes no shared handle of its own, so we resolve it to its owning ITexture (getTexture), which
// does. Only the Shared usage flag is checked here - no handle is exported at bind time.
IResource* sharedResourceOfBinding(IResource* resource)
{
    if (!resource)
        return nullptr;
    ComPtr<IBuffer> buffer;
    if (SLANG_SUCCEEDED(resource->queryInterface(IBuffer::getTypeGuid(), (void**)buffer.writeRef())))
        return is_set(buffer->getDesc().usage, BufferUsage::Shared) ? buffer.get() : nullptr;
    ComPtr<ITextureView> view;
    if (SLANG_SUCCEEDED(resource->queryInterface(ITextureView::getTypeGuid(), (void**)view.writeRef())))
    {
        ITexture* texture = view->getTexture();
        return (texture && is_set(texture->getDesc().usage, TextureUsage::Shared)) ? texture : nullptr;
    }
    ComPtr<ITexture> texture;
    if (SLANG_SUCCEEDED(resource->queryInterface(ITexture::getTypeGuid(), (void**)texture.writeRef())))
        return is_set(texture->getDesc().usage, TextureUsage::Shared) ? texture.get() : nullptr;
    return nullptr;
}

} // namespace

// ----------------------------------------------------------------------------
// DebugShaderObject
// ----------------------------------------------------------------------------

slang::TypeLayoutReflection* DebugShaderObject::getElementTypeLayout()
{
    SLANG_RHI_DEBUG_API(IShaderObject, getElementTypeLayout);

    return baseObject->getElementTypeLayout();
}

ShaderObjectContainerType DebugShaderObject::getContainerType()
{
    SLANG_RHI_DEBUG_API(IShaderObject, getContainerType);

    return baseObject->getContainerType();
}

uint32_t DebugShaderObject::getEntryPointCount()
{
    SLANG_RHI_DEBUG_API(IShaderObject, getEntryPointCount);

    return baseObject->getEntryPointCount();
}

Result DebugShaderObject::getEntryPoint(uint32_t index, IShaderObject** outEntryPoint)
{
    SLANG_RHI_DEBUG_API(IShaderObject, getEntryPoint);

    if (!outEntryPoint)
    {
        RHI_VALIDATION_ERROR("'outEntryPoint' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    if (m_entryPoints.empty())
    {
        for (uint32_t i = 0; i < getEntryPointCount(); i++)
        {
            RefPtr<DebugShaderObject> entryPoint = new DebugShaderObject(ctx);
            SLANG_RETURN_ON_FAIL(baseObject->getEntryPoint(i, entryPoint->baseObject.writeRef()));
            m_entryPoints.push_back(entryPoint);
        }
    }
    if (index >= m_entryPoints.size())
    {
        RHI_VALIDATION_ERROR("'index' must not exceed 'entryPointCount'.");
        return SLANG_FAIL;
    }

    returnComPtr(outEntryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result DebugShaderObject::setData(const ShaderOffset& offset, const void* data, Size size)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setData);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    if (!data && size > 0)
    {
        RHI_VALIDATION_ERROR("'data' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->setData(offset, data, size);
}

Result DebugShaderObject::reserveData(const ShaderOffset& offset, Size size, void** outData)
{
    SLANG_RHI_DEBUG_API(IShaderObject, reserveData);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    if (!outData)
    {
        RHI_VALIDATION_ERROR("'outData' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->reserveData(offset, size, outData);
}

Result DebugShaderObject::getObject(const ShaderOffset& offset, IShaderObject** outObject)
{
    SLANG_RHI_DEBUG_API(IShaderObject, getObject);

    if (!outObject)
    {
        RHI_VALIDATION_ERROR("'outObject' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    ComPtr<IShaderObject> innerObject;
    SLANG_RETURN_ON_FAIL(baseObject->getObject(offset, innerObject.writeRef()));

    RefPtr<DebugShaderObject> debugShaderObject;
    auto it = m_objects.find(ShaderOffsetKey{offset});
    if (it != m_objects.end())
    {
        debugShaderObject = it->second;
        if (debugShaderObject->baseObject == innerObject)
        {
            returnComPtr(outObject, debugShaderObject);
            return SLANG_OK;
        }
    }

    debugShaderObject = new DebugShaderObject(ctx);
    debugShaderObject->baseObject = innerObject;
    if (innerObject)
    {
        debugShaderObject->m_typeName = string::from_cstr(innerObject->getElementTypeLayout()->getName());
    }
    m_objects.emplace(ShaderOffsetKey{offset}, debugShaderObject);

    returnComPtr(outObject, debugShaderObject);
    return SLANG_OK;
}

Result DebugShaderObject::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setObject);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    if (!object)
    {
        RHI_VALIDATION_ERROR("'object' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    auto objectImpl = getDebugObj(object);
    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_initializedBindingRanges.emplace(offset.bindingRangeIndex);
    // objectImpl->checkCompleteness();

    Result result = baseObject->setObject(offset, getInnerObj(object));
    // Track the child only once the base accepts it, so a rejected setObject does not leave a child
    // whose Shared bindings would be validated at draw/dispatch though it was never bound.
    if (SLANG_SUCCEEDED(result))
        m_objects[ShaderOffsetKey{offset}] = objectImpl;
    return result;
}

Result DebugShaderObject::setBinding(const ShaderOffset& offset, const Binding& binding)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setBinding);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_bindings[ShaderOffsetKey{offset}] = binding;
    // m_initializedBindingRanges.emplace(offset.bindingRangeIndex);

    // A Shared resource's ownership is not validated here: the submitting queue is unknown at bind
    // time, and a resource may be legitimately bound while handed off as long as a takeOverShared is
    // recorded before the draw/dispatch that uses it. Instead we record which bound resources are
    // Shared and validate them at draw/dispatch, where the submitting queue is known (and only when a
    // Shared resource is actually bound). Recording happens only after the base binding succeeds, so a
    // rejected binding is not tracked; rebinding a slot with no Shared operand clears its entry.
    Result result = baseObject->setBinding(offset, binding);
    if (SLANG_SUCCEEDED(result))
    {
        ShaderOffsetKey key{offset};
        std::vector<ComPtr<IResource>> shared;
        if (IResource* r = sharedResourceOfBinding(binding.resource.get()))
            shared.push_back(ComPtr<IResource>(r));
        if (IResource* r = sharedResourceOfBinding(binding.resource2.get()))
            shared.push_back(ComPtr<IResource>(r));
        if (shared.empty())
            m_sharedBindings.erase(key);
        else
            m_sharedBindings[key] = std::move(shared);
    }
    return result;
}

void DebugShaderObject::collectSharedBindings(std::vector<IResource*>& out)
{
    for (const auto& kv : m_sharedBindings)
        for (const auto& resource : kv.second)
            if (resource)
                out.push_back(resource.get());
    for (const auto& kv : m_objects)
        if (kv.second)
            kv.second->collectSharedBindings(out);
    for (const auto& entryPoint : m_entryPoints)
        if (entryPoint)
            entryPoint->collectSharedBindings(out);
}

Result DebugShaderObject::setDescriptorHandle(const ShaderOffset& offset, const DescriptorHandle& handle)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setDescriptorHandle);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    return baseObject->setDescriptorHandle(offset, handle);
}

Result DebugShaderObject::setSpecializationArgs(
    const ShaderOffset& offset,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setSpecializationArgs);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    if (count > 0 && !args)
    {
        RHI_VALIDATION_ERROR("'args' must not be null when 'count' > 0.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->setSpecializationArgs(offset, args, count);
}

const void* DebugShaderObject::getRawData()
{
    SLANG_RHI_DEBUG_API(IShaderObject, getRawData);

    return baseObject->getRawData();
}

size_t DebugShaderObject::getSize()
{
    SLANG_RHI_DEBUG_API(IShaderObject, getSize);

    return baseObject->getSize();
}

Result DebugShaderObject::setConstantBufferOverride(IBuffer* constantBuffer)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setConstantBufferOverride);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    return baseObject->setConstantBufferOverride(constantBuffer);
}

Result DebugShaderObject::finalize()
{
    SLANG_RHI_DEBUG_API(IShaderObject, finalize);

    if (baseObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The shader object is already finalized.");
    }

    return baseObject->finalize();
}

bool DebugShaderObject::isFinalized()
{
    SLANG_RHI_DEBUG_API(IShaderObject, isFinalized);

    return baseObject->isFinalized();
}

void DebugShaderObject::checkCompleteness()
{
    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // auto layout = baseObject->getElementTypeLayout();
    // for (SlangInt i = 0; i < layout->getBindingRangeCount(); i++)
    // {
    //     if (layout->getBindingRangeBindingCount(i) != 0)
    //     {
    //         if (!m_initializedBindingRanges.count(i))
    //         {
    //             auto var = layout->getBindingRangeLeafVariable(i);
    //             RHI_VALIDATION_ERROR_FORMAT(
    //                 "shader parameter '%s' is not initialized in the shader object of type '%s'.",
    //                 var->getName(),
    //                 m_slangType->getName()
    //             );
    //         }
    //     }
    // }
}

Result DebugShaderObject::checkNotFinalized()
{
    if (baseObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The shader object is finalized and must not be modified.");
        return SLANG_E_INVALID_ARG;
    }
    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// DebugRootShaderObject
// ----------------------------------------------------------------------------

Result DebugRootShaderObject::setSpecializationArgs(
    const ShaderOffset& offset,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setSpecializationArgs);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    if (count > 0 && !args)
    {
        RHI_VALIDATION_ERROR("'args' must not be null when 'count' > 0.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->setSpecializationArgs(offset, args, count);
}

void DebugRootShaderObject::reset()
{
    m_entryPoints.clear();
    m_objects.clear();
    m_sharedBindings.clear();
    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_bindings.clear();
    baseObject.setNull();
}

} // namespace rhi::debug
