#include "cpu-shader-object.h"
#include "cpu-device.h"
#include "cpu-buffer.h"
#include "cpu-texture.h"
#include "cpu-shader-object-layout.h"

namespace rhi::cpu {

Index CPUShaderObjectData::getCount()
{
    return m_ordinaryData.size();
}

void CPUShaderObjectData::setCount(Index count)
{
    m_ordinaryData.resize(count);
}

uint8_t* CPUShaderObjectData::getBuffer()
{
    return m_ordinaryData.data();
}

CPUShaderObjectData::~CPUShaderObjectData()
{
    // m_buffer's data is managed by m_ordinaryData so we
    // set it to null to prevent m_buffer from freeing it.
    if (m_buffer)
        m_buffer->m_data = nullptr;
}

/// Returns a StructuredBuffer resource view for GPU access into the buffer content.
/// Creates a StructuredBuffer resource if it has not been created.
Buffer* CPUShaderObjectData::getBufferResource(
    Device* device,
    slang::TypeLayoutReflection* elementLayout,
    slang::BindingType bindingType
)
{
    SLANG_UNUSED(device);
    if (!m_buffer)
    {
        BufferDesc desc = {};
        desc.elementSize = (int)elementLayout->getSize();
        m_buffer = new BufferImpl(desc);
    }
    m_buffer->m_desc.size = m_ordinaryData.size();
    m_buffer->m_data = m_ordinaryData.data();
    return m_buffer.get();
}

Result ShaderObjectImpl::init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout)
{
    m_device = device;
    m_device.breakStrongReference();
    m_layout = typeLayout;

    // If the layout tells us that there is any uniform data,
    // then we need to allocate a constant buffer to hold that data.
    //
    // TODO: Do we need to allocate a shadow copy for use from
    // the CPU?
    //
    // TODO: When/where do we bind this constant buffer into
    // a descriptor set for later use?
    //
    auto slangLayout = getLayout()->getElementTypeLayout();
    size_t uniformSize = slangLayout->getSize();
    m_data.setCount(uniformSize);

    // If the layout specifies that we have any resources or sub-objects,
    // then we need to size the appropriate arrays to account for them.
    //
    // Note: the counts here are the *total* number of resources/sub-objects
    // and not just the number of resource/sub-object ranges.
    //
    m_resources.resize(typeLayout->getResourceCount());
    m_objects.resize(typeLayout->getSubObjectCount());

    for (auto subObjectRange : getLayout()->subObjectRanges)
    {
        RefPtr<ShaderObjectLayoutImpl> subObjectLayout = subObjectRange.layout;

        // In the case where the sub-object range represents an
        // existential-type leaf field (e.g., an `IBar`), we
        // cannot pre-allocate the object(s) to go into that
        // range, since we can't possibly know what to allocate
        // at this point.
        //
        if (!subObjectLayout)
            continue;

        //
        // Otherwise, we will allocate a sub-object to fill
        // in each entry in this range, based on the layout
        // information we already have.

        auto& bindingRangeInfo = getLayout()->m_bindingRanges[subObjectRange.bindingRangeIndex];
        for (Index i = 0; i < bindingRangeInfo.count; ++i)
        {
            RefPtr<ShaderObjectImpl> subObject = new ShaderObjectImpl();
            SLANG_RETURN_ON_FAIL(subObject->init(device, subObjectLayout));

            ShaderOffset offset;
            offset.uniformOffset = bindingRangeInfo.uniformOffset + sizeof(void*) * i;
            offset.bindingRangeIndex = (GfxIndex)subObjectRange.bindingRangeIndex;
            offset.bindingArrayIndex = (GfxIndex)i;

            SLANG_RETURN_ON_FAIL(setObject(offset, subObject));
        }
    }

    m_state = State::Initialized;

    return SLANG_OK;
}

GfxCount ShaderObjectImpl::getEntryPointCount()
{
    return 0;
}

Result ShaderObjectImpl::getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint)
{
    *outEntryPoint = nullptr;
    return SLANG_OK;
}

const void* ShaderObjectImpl::getRawData()
{
    return m_data.getBuffer();
}

size_t ShaderObjectImpl::getSize()
{
    return (size_t)m_data.getCount();
}

Result ShaderObjectImpl::setData(const ShaderOffset& offset, const void* data, size_t size)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    size = min(size, size_t(m_data.getCount() - offset.uniformOffset));
    memcpy((char*)m_data.getBuffer() + offset.uniformOffset, data, size);
    return SLANG_OK;
}

Result ShaderObjectImpl::setBinding(const ShaderOffset& offset, Binding binding)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    auto layout = getLayout();

    auto bindingRangeIndex = offset.bindingRangeIndex;
    if (bindingRangeIndex < 0 || bindingRangeIndex >= layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    const auto& bindingRange = layout->getBindingRange(bindingRangeIndex);
    auto bindingIndex = bindingRange.baseIndex + offset.bindingArrayIndex;

    switch (binding.type)
    {
    case BindingType::Buffer:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(binding.resource);
        const BufferDesc& desc = buffer->m_desc;
        BufferRange range = buffer->resolveBufferRange(binding.bufferRange);
        m_resources[bindingIndex] = buffer;

        void* dataPtr = (uint8_t*)buffer->m_data + range.offset;
        size_t size = range.size;
        if (desc.elementSize > 1)
            size /= desc.elementSize;

        auto ptrOffset = offset;
        SLANG_RETURN_ON_FAIL(setData(ptrOffset, &dataPtr, sizeof(dataPtr)));

        auto sizeOffset = offset;
        sizeOffset.uniformOffset += sizeof(dataPtr);
        SLANG_RETURN_ON_FAIL(setData(sizeOffset, &size, sizeof(size)));
        break;
    }
    case BindingType::Texture:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource);
        return setBinding(offset, m_device->createTextureView(texture, {}));
    }
    case BindingType::TextureView:
    {
        auto textureView = checked_cast<TextureViewImpl*>(binding.resource);
        m_resources[bindingIndex] = textureView;
        slang_prelude::IRWTexture* textureObj = textureView;
        SLANG_RETURN_ON_FAIL(setData(offset, &textureObj, sizeof(textureObj)));
        break;
    }
    case BindingType::Sampler:
    {
        break;
    }
    case BindingType::CombinedTextureSampler:
    {
        break;
    }
    case BindingType::CombinedTextureViewSampler:
    {
        break;
    }
    case BindingType::AccelerationStructure:
    {
        break;
    }
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());
    SLANG_RETURN_ON_FAIL(Super::setObject(offset, object));

    auto bindingRangeIndex = offset.bindingRangeIndex;
    auto& bindingRange = getLayout()->m_bindingRanges[bindingRangeIndex];

    ShaderObjectImpl* subObject = checked_cast<ShaderObjectImpl*>(object);

    switch (bindingRange.bindingType)
    {
    default:
    {
        void* bufferPtr = subObject->m_data.getBuffer();
        SLANG_RETURN_ON_FAIL(setData(offset, &bufferPtr, sizeof(void*)));
    }
    break;
    case slang::BindingType::ExistentialValue:
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        break;
    }
    return SLANG_OK;
}

uint8_t* ShaderObjectImpl::getDataBuffer()
{
    return m_data.getBuffer();
}

EntryPointLayoutImpl* EntryPointShaderObjectImpl::getLayout()
{
    return checked_cast<EntryPointLayoutImpl*>(m_layout.Ptr());
}

Result RootShaderObjectImpl::init(DeviceImpl* device, RootShaderObjectLayoutImpl* programLayout)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::init(device, programLayout));
    for (auto& entryPoint : programLayout->m_entryPointLayouts)
    {
        RefPtr<EntryPointShaderObjectImpl> object = new EntryPointShaderObjectImpl();
        SLANG_RETURN_ON_FAIL(object->init(device, entryPoint));
        m_entryPoints.push_back(object);
    }
    return SLANG_OK;
}

RootShaderObjectLayoutImpl* RootShaderObjectImpl::getLayout()
{
    return checked_cast<RootShaderObjectLayoutImpl*>(m_layout.Ptr());
}

EntryPointShaderObjectImpl* RootShaderObjectImpl::getEntryPoint(Index index)
{
    return m_entryPoints[index];
}

GfxCount RootShaderObjectImpl::getEntryPointCount()
{
    return (GfxCount)m_entryPoints.size();
}

Result RootShaderObjectImpl::getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint)
{
    returnComPtr(outEntryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result RootShaderObjectImpl::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::collectSpecializationArgs(args));
    for (auto& entryPoint : m_entryPoints)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
    }
    return SLANG_OK;
}

} // namespace rhi::cpu
