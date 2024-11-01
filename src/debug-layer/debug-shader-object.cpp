#include "debug-shader-object.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

ShaderObjectContainerType DebugShaderObject::getContainerType()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getContainerType();
}

void DebugShaderObject::checkCompleteness()
{
    auto layout = baseObject->getElementTypeLayout();
    for (Index i = 0; i < layout->getBindingRangeCount(); i++)
    {
        if (layout->getBindingRangeBindingCount(i) != 0)
        {
            if (!m_initializedBindingRanges.count(i))
            {
                auto var = layout->getBindingRangeLeafVariable(i);
                RHI_VALIDATION_ERROR_FORMAT(
                    "shader parameter '%s' is not initialized in the shader object of type '%s'.",
                    var->getName(),
                    m_slangType->getName()
                );
            }
        }
    }
}

void DebugShaderObject::checkFinalized()
{
    if (!baseObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The shader object must be finalized.");
    }
}

void DebugShaderObject::checkNotFinalized()
{
    if (baseObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The shader object must not be finalized.");
    }
}

slang::TypeLayoutReflection* DebugShaderObject::getElementTypeLayout()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getElementTypeLayout();
}

GfxCount DebugShaderObject::getEntryPointCount()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getEntryPointCount();
}

Result DebugShaderObject::getEntryPoint(GfxIndex index, IShaderObject** entryPoint)
{
    SLANG_RHI_API_FUNC;
    if (m_entryPoints.empty())
    {
        for (GfxIndex i = 0; i < getEntryPointCount(); i++)
        {
            RefPtr<DebugShaderObject> entryPointObj = new DebugShaderObject(ctx);
            SLANG_RETURN_ON_FAIL(baseObject->getEntryPoint(i, entryPointObj->baseObject.writeRef()));
            m_entryPoints.push_back(entryPointObj);
        }
    }
    if (index > (GfxCount)m_entryPoints.size())
    {
        RHI_VALIDATION_ERROR("`index` must not exceed `entryPointCount`.");
        return SLANG_FAIL;
    }
    returnComPtr(entryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result DebugShaderObject::setData(ShaderOffset const& offset, void const* data, Size size)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    return baseObject->setData(offset, data, size);
}

Result DebugShaderObject::getObject(ShaderOffset const& offset, IShaderObject** object)
{
    SLANG_RHI_API_FUNC;

    ComPtr<IShaderObject> innerObject;
    auto resultCode = baseObject->getObject(offset, innerObject.writeRef());
    SLANG_RETURN_ON_FAIL(resultCode);
    RefPtr<DebugShaderObject> debugShaderObject;
    auto it = m_objects.find(ShaderOffsetKey{offset});
    if (it != m_objects.end())
    {
        debugShaderObject = it->second;
        if (debugShaderObject->baseObject == innerObject)
        {
            returnComPtr(object, debugShaderObject);
            return resultCode;
        }
    }
    debugShaderObject = new DebugShaderObject(ctx);
    debugShaderObject->baseObject = innerObject;
    debugShaderObject->m_typeName = string::from_cstr(innerObject->getElementTypeLayout()->getName());
    m_objects.emplace(ShaderOffsetKey{offset}, debugShaderObject);
    returnComPtr(object, debugShaderObject);
    return resultCode;
}

Result DebugShaderObject::setObject(ShaderOffset const& offset, IShaderObject* object)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    if (!object->isFinalized())
    {
        RHI_VALIDATION_ERROR("The assigned sub-object must be finalized.");
    }
    auto objectImpl = getDebugObj(object);
    m_objects[ShaderOffsetKey{offset}] = objectImpl;
    m_initializedBindingRanges.emplace(offset.bindingRangeIndex);
    objectImpl->checkCompleteness();
    return baseObject->setObject(offset, getInnerObj(object));
}

Result DebugShaderObject::setBinding(ShaderOffset const& offset, Binding binding)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    m_bindings[ShaderOffsetKey{offset}] = binding;
    m_initializedBindingRanges.emplace(offset.bindingRangeIndex);
    return baseObject->setBinding(offset, binding);
}

Result DebugShaderObject::setSpecializationArgs(
    ShaderOffset const& offset,
    const slang::SpecializationArg* args,
    GfxCount count
)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    return baseObject->setSpecializationArgs(offset, args, count);
}

const void* DebugShaderObject::getRawData()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getRawData();
}

size_t DebugShaderObject::getSize()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSize();
}

Result DebugShaderObject::setConstantBufferOverride(IBuffer* constantBuffer)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    return baseObject->setConstantBufferOverride(constantBuffer);
}

Result DebugShaderObject::finalize()
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    return baseObject->finalize();
}

bool DebugShaderObject::isFinalized()
{
    SLANG_RHI_API_FUNC;
    return baseObject->isFinalized();
}

Result DebugRootShaderObject::setSpecializationArgs(
    ShaderOffset const& offset,
    const slang::SpecializationArg* args,
    GfxCount count
)
{
    SLANG_RHI_API_FUNC;
    checkNotFinalized();
    return baseObject->setSpecializationArgs(offset, args, count);
}

void DebugRootShaderObject::reset()
{
    m_entryPoints.clear();
    m_objects.clear();
    m_bindings.clear();
    baseObject.setNull();
}

} // namespace rhi::debug
