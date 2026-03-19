#include "debug-shader-object.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

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

Result DebugShaderObject::getEntryPoint(uint32_t index, IShaderObject** entryPoint)
{
    SLANG_RHI_DEBUG_API(IShaderObject, getEntryPoint);

    if (m_entryPoints.empty())
    {
        for (uint32_t i = 0; i < getEntryPointCount(); i++)
        {
            RefPtr<DebugShaderObject> entryPointObj = new DebugShaderObject(ctx);
            SLANG_RETURN_ON_FAIL(baseObject->getEntryPoint(i, entryPointObj->baseObject.writeRef()));
            m_entryPoints.push_back(entryPointObj);
        }
    }
    if (index >= m_entryPoints.size())
    {
        RHI_VALIDATION_ERROR("'index' must not exceed 'entryPointCount'.");
        return SLANG_FAIL;
    }

    returnComPtr(entryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result DebugShaderObject::setData(const ShaderOffset& offset, const void* data, Size size)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setData);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    return baseObject->setData(offset, data, size);
}

Result DebugShaderObject::reserveData(const ShaderOffset& offset, Size size, void** outData)
{
    SLANG_RHI_DEBUG_API(IShaderObject, reserveData);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    return baseObject->reserveData(offset, size, outData);
}

Result DebugShaderObject::getObject(const ShaderOffset& offset, IShaderObject** object)
{
    SLANG_RHI_DEBUG_API(IShaderObject, getObject);

    ComPtr<IShaderObject> innerObject;
    SLANG_RETURN_ON_FAIL(baseObject->getObject(offset, innerObject.writeRef()));

    RefPtr<DebugShaderObject> debugShaderObject;
    auto it = m_objects.find(ShaderOffsetKey{offset});
    if (it != m_objects.end())
    {
        debugShaderObject = it->second;
        if (debugShaderObject->baseObject == innerObject)
        {
            returnComPtr(object, debugShaderObject);
            return SLANG_OK;
        }
    }

    debugShaderObject = new DebugShaderObject(ctx);
    debugShaderObject->baseObject = innerObject;
    debugShaderObject->m_typeName = string::from_cstr(innerObject->getElementTypeLayout()->getName());
    m_objects.emplace(ShaderOffsetKey{offset}, debugShaderObject);

    returnComPtr(object, debugShaderObject);
    return SLANG_OK;
}

Result DebugShaderObject::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setObject);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    auto objectImpl = getDebugObj(object);
    m_objects[ShaderOffsetKey{offset}] = objectImpl;
    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_initializedBindingRanges.emplace(offset.bindingRangeIndex);
    // objectImpl->checkCompleteness();

    return baseObject->setObject(offset, getInnerObj(object));
}

Result DebugShaderObject::setBinding(const ShaderOffset& offset, const Binding& binding)
{
    SLANG_RHI_DEBUG_API(IShaderObject, setBinding);

    SLANG_RETURN_ON_FAIL(checkNotFinalized());

    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_bindings[ShaderOffsetKey{offset}] = binding;
    // m_initializedBindingRanges.emplace(offset.bindingRangeIndex);

    return baseObject->setBinding(offset, binding);
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

    return baseObject->setSpecializationArgs(offset, args, count);
}

void DebugRootShaderObject::reset()
{
    m_entryPoints.clear();
    m_objects.clear();
    // TODO(shaderobject): Implement better validation for bindings but make that optional as it's expensive.
    // m_bindings.clear();
    baseObject.setNull();
}

} // namespace rhi::debug
