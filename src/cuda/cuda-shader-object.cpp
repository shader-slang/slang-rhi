#include "cuda-shader-object.h"
#include "cuda-helper-functions.h"
#include "cuda-shader-object-layout.h"
#include "cuda-device.h"

namespace rhi::cuda {

Result ShaderObjectData::setCount(Index count)
{
    if (isHostOnly)
    {
        m_cpuBuffer.resize(count);
        if (!m_buffer)
        {
            BufferDesc desc;
            desc.size = count;
            m_buffer = new BufferImpl(desc);
        }
        m_buffer->m_cpuBuffer = m_cpuBuffer.data();
        m_buffer->m_desc.size = count;
    }
    else
    {
        if (!m_buffer)
        {
            BufferDesc desc;
            desc.size = count;
            m_buffer = new BufferImpl(desc);
            if (count)
            {
                SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc((CUdeviceptr*)&m_buffer->m_cudaMemory, (size_t)count));
            }
        }
        auto oldSize = m_buffer->m_desc.size;
        if ((size_t)count != oldSize)
        {
            void* newMemory = nullptr;
            if (count)
            {
                SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc((CUdeviceptr*)&newMemory, (size_t)count));
            }
            if (oldSize)
            {
                SLANG_CUDA_RETURN_ON_FAIL(
                    cuMemcpy((CUdeviceptr)newMemory, (CUdeviceptr)m_buffer->m_cudaMemory, min((size_t)count, oldSize))
                );
            }
            cuMemFree((CUdeviceptr)m_buffer->m_cudaMemory);
            m_buffer->m_cudaMemory = newMemory;
            m_buffer->m_desc.size = count;
        }
    }

    return SLANG_OK;
}

Index ShaderObjectData::getCount()
{
    if (isHostOnly)
        return m_cpuBuffer.size();
    if (m_buffer)
        return (Index)(m_buffer->m_desc.size);
    else
        return 0;
}

void* ShaderObjectData::getBuffer()
{
    if (isHostOnly)
        return m_cpuBuffer.data();

    if (m_buffer)
        return m_buffer->m_cudaMemory;
    return nullptr;
}

/// Returns a resource view for GPU access into the buffer content.
Buffer* ShaderObjectData::getBufferResource(
    Device* device,
    slang::TypeLayoutReflection* elementLayout,
    slang::BindingType bindingType
)
{
    SLANG_UNUSED(device);
    m_buffer->m_desc.elementSize = elementLayout->getSize();
    return m_buffer.get();
}

Result ShaderObjectImpl::init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout)
{
    m_data.device = device;

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
    if (uniformSize)
    {
        m_data.setCount((Index)uniformSize);
    }

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

Size ShaderObjectImpl::getSize()
{
    return (Size)m_data.getCount();
}

Result ShaderObjectImpl::setData(ShaderOffset const& offset, void const* data, Size size)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    Size temp = m_data.getCount() - (Size)offset.uniformOffset;
    size = min(size, temp);
    SLANG_CUDA_RETURN_ON_FAIL(
        cuMemcpy((CUdeviceptr)((uint8_t*)m_data.getBuffer() + offset.uniformOffset), (CUdeviceptr)data, size)
    );
    return SLANG_OK;
}

Result ShaderObjectImpl::setBinding(ShaderOffset const& offset, Binding binding)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    auto layout = getLayout();

    auto bindingRangeIndex = offset.bindingRangeIndex;
    SLANG_RHI_ASSERT(bindingRangeIndex >= 0);
    SLANG_RHI_ASSERT(bindingRangeIndex < layout->m_bindingRanges.size());

    auto& bindingRange = layout->m_bindingRanges[bindingRangeIndex];

    auto viewIndex = bindingRange.baseIndex + offset.bindingArrayIndex;

    switch (binding.type)
    {
    case BindingType::Buffer:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(binding.resource.get());
        const BufferDesc& desc = buffer->m_desc;
        BufferRange range = buffer->resolveBufferRange(binding.bufferRange);
        m_resources[viewIndex] = buffer;
        uint64_t handle = buffer->m_cpuBuffer ? (uint64_t)buffer->m_cpuBuffer : (uint64_t)buffer->m_cudaMemory;
        handle += range.offset;
        setData(offset, &handle, sizeof(handle));
        ShaderOffset sizeOffset = offset;
        sizeOffset.uniformOffset += sizeof(handle);
        size_t size = desc.size;
        if (desc.elementSize > 1)
            size /= desc.elementSize;
        setData(sizeOffset, &size, sizeof(size));
        break;
    }
    case BindingType::Texture:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource.get());
        m_resources[viewIndex] = texture;
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::Texture:
            setData(offset, &texture->m_cudaTexObj, sizeof(texture->m_cudaTexObj));
            break;
        case slang::BindingType::MutableTexture:
            setData(offset, &texture->m_cudaSurfObj, sizeof(texture->m_cudaSurfObj));
            break;
        }
        break;
    }
    case BindingType::TextureView:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(binding.resource.get());
        m_resources[viewIndex] = textureView;
        TextureImpl* texture = textureView->m_texture;
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::Texture:
            setData(offset, &texture->m_cudaTexObj, sizeof(texture->m_cudaTexObj));
            break;
        case slang::BindingType::MutableTexture:
            setData(offset, &texture->m_cudaSurfObj, sizeof(texture->m_cudaSurfObj));
            break;
        }
        break;
    }
    }
    return SLANG_OK;
}

Result ShaderObjectImpl::setObject(ShaderOffset const& offset, IShaderObject* object)
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
        void* subObjectDataBuffer = subObject->getBuffer();
        SLANG_RETURN_ON_FAIL(setData(offset, &subObjectDataBuffer, sizeof(void*)));
    }
    break;
    case slang::BindingType::ExistentialValue:
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        break;
    }
    return SLANG_OK;
}

EntryPointShaderObjectImpl::EntryPointShaderObjectImpl()
{
    m_data.isHostOnly = true;
}

Result RootShaderObjectImpl::init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::init(device, typeLayout));
    auto programLayout = dynamic_cast<RootShaderObjectLayoutImpl*>(typeLayout);
    for (auto& entryPoint : programLayout->entryPointLayouts)
    {
        RefPtr<EntryPointShaderObjectImpl> object = new EntryPointShaderObjectImpl();
        SLANG_RETURN_ON_FAIL(object->init(device, entryPoint));
        entryPointObjects.push_back(object);
    }
    return SLANG_OK;
}

GfxCount RootShaderObjectImpl::getEntryPointCount()
{
    return (GfxCount)entryPointObjects.size();
}

Result RootShaderObjectImpl::getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint)
{
    returnComPtr(outEntryPoint, entryPointObjects[index]);
    return SLANG_OK;
}

Result RootShaderObjectImpl::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::collectSpecializationArgs(args));
    for (auto& entryPoint : entryPointObjects)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
    }
    return SLANG_OK;
}

} // namespace rhi::cuda
