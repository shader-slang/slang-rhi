#include "cuda-shader-object.h"
#include "cuda-helper-functions.h"
#include "cuda-resource-views.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

Result ShaderObjectData::setCount(Index count)
{
    if (isHostOnly)
    {
        m_cpuBuffer.resize(count);
        if (!m_bufferView)
        {
            IResourceView::Desc viewDesc = {};
            viewDesc.type = IResourceView::Type::UnorderedAccess;
            m_bufferView = new ResourceViewImpl();
            m_bufferView->proxyBuffer = m_cpuBuffer.data();
            m_bufferView->m_desc = viewDesc;
        }
        return SLANG_OK;
    }

    if (!m_buffer)
    {
        BufferDesc desc;
        desc.size = count;
        m_buffer = new BufferImpl(desc);
        if (count)
        {
            SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc((CUdeviceptr*)&m_buffer->m_cudaMemory, (size_t)count));
        }
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::UnorderedAccess;
        m_bufferView = new ResourceViewImpl();
        m_bufferView->buffer = m_buffer;
        m_bufferView->m_desc = viewDesc;
    }
    auto oldSize = m_buffer->getDesc()->size;
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
                cuMemcpy((CUdeviceptr)newMemory, (CUdeviceptr)m_buffer->m_cudaMemory, std::min((size_t)count, oldSize))
            );
        }
        cuMemFree((CUdeviceptr)m_buffer->m_cudaMemory);
        m_buffer->m_cudaMemory = newMemory;
        m_buffer->getDesc()->size = count;
    }
    return SLANG_OK;
}

Index ShaderObjectData::getCount()
{
    if (isHostOnly)
        return m_cpuBuffer.size();
    if (m_buffer)
        return (Index)(m_buffer->getDesc()->size);
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
ResourceViewBase* ShaderObjectData::getResourceView(
    RendererBase* device,
    slang::TypeLayoutReflection* elementLayout,
    slang::BindingType bindingType
)
{
    SLANG_UNUSED(device);
    m_buffer->getDesc()->elementSize = (int)elementLayout->getSize();
    return m_bufferView.Ptr();
}

Result ShaderObjectImpl::init(IDevice* device, ShaderObjectLayoutImpl* typeLayout)
{
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
    resources.resize(typeLayout->getResourceCount());
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
    return SLANG_OK;
}

SLANG_NO_THROW GfxCount SLANG_MCALL ShaderObjectImpl::getEntryPointCount()
{
    return 0;
}

SLANG_NO_THROW Result SLANG_MCALL ShaderObjectImpl::getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint)
{
    *outEntryPoint = nullptr;
    return SLANG_OK;
}

SLANG_NO_THROW const void* SLANG_MCALL ShaderObjectImpl::getRawData()
{
    return m_data.getBuffer();
}

SLANG_NO_THROW Size SLANG_MCALL ShaderObjectImpl::getSize()
{
    return (Size)m_data.getCount();
}

SLANG_NO_THROW Result SLANG_MCALL ShaderObjectImpl::setData(ShaderOffset const& offset, void const* data, Size size)
{
    Size temp = m_data.getCount() - (Size)offset.uniformOffset;
    size = std::min(size, temp);
    SLANG_CUDA_RETURN_ON_FAIL(
        cuMemcpy((CUdeviceptr)((uint8_t*)m_data.getBuffer() + offset.uniformOffset), (CUdeviceptr)data, size)
    );
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL ShaderObjectImpl::setResource(ShaderOffset const& offset, IResourceView* resourceView)
{
    if (!resourceView)
        return SLANG_OK;

    auto layout = getLayout();

    auto bindingRangeIndex = offset.bindingRangeIndex;
    SLANG_RHI_ASSERT(bindingRangeIndex >= 0);
    SLANG_RHI_ASSERT(bindingRangeIndex < layout->m_bindingRanges.size());

    auto& bindingRange = layout->m_bindingRanges[bindingRangeIndex];

    auto viewIndex = bindingRange.baseIndex + offset.bindingArrayIndex;
    auto cudaView = static_cast<ResourceViewImpl*>(resourceView);

    resources[viewIndex] = cudaView;

    if (cudaView->texture)
    {
        if (cudaView->m_desc.type == IResourceView::Type::UnorderedAccess)
        {
            auto handle = cudaView->texture->m_cudaSurfObj;
            setData(offset, &handle, sizeof(uint64_t));
        }
        else
        {
            auto handle = cudaView->texture->getBindlessHandle();
            setData(offset, &handle, sizeof(uint64_t));
        }
    }
    else if (cudaView->buffer)
    {
        auto handle = cudaView->buffer->getBindlessHandle();
        setData(offset, &handle, sizeof(handle));
        auto sizeOffset = offset;
        sizeOffset.uniformOffset += sizeof(handle);
        auto& desc = *cudaView->buffer->getDesc();
        size_t size = desc.size;
        if (desc.elementSize > 1)
            size /= desc.elementSize;
        setData(sizeOffset, &size, sizeof(size));
    }
    else if (cudaView->proxyBuffer)
    {
        auto handle = cudaView->proxyBuffer;
        setData(offset, &handle, sizeof(handle));
        auto sizeOffset = offset;
        sizeOffset.uniformOffset += sizeof(handle);
        auto& desc = *cudaView->buffer->getDesc();
        size_t size = desc.size;
        if (desc.elementSize > 1)
            size /= desc.elementSize;
        setData(sizeOffset, &size, sizeof(size));
    }
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL ShaderObjectImpl::setObject(ShaderOffset const& offset, IShaderObject* object)
{
    SLANG_RETURN_ON_FAIL(Super::setObject(offset, object));

    auto bindingRangeIndex = offset.bindingRangeIndex;
    auto& bindingRange = getLayout()->m_bindingRanges[bindingRangeIndex];

    ShaderObjectImpl* subObject = static_cast<ShaderObjectImpl*>(object);
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

SLANG_NO_THROW Result SLANG_MCALL ShaderObjectImpl::setSampler(ShaderOffset const& offset, ISampler* sampler)
{
    SLANG_UNUSED(sampler);
    SLANG_UNUSED(offset);
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL
ShaderObjectImpl::setCombinedTextureSampler(ShaderOffset const& offset, IResourceView* textureView, ISampler* sampler)
{
    SLANG_UNUSED(sampler);
    setResource(offset, textureView);
    return SLANG_OK;
}

EntryPointShaderObjectImpl::EntryPointShaderObjectImpl()
{
    m_data.isHostOnly = true;
}

SLANG_NO_THROW uint32_t SLANG_MCALL RootShaderObjectImpl::addRef()
{
    return 1;
}

SLANG_NO_THROW uint32_t SLANG_MCALL RootShaderObjectImpl::release()
{
    return 1;
}

Result RootShaderObjectImpl::init(IDevice* device, ShaderObjectLayoutImpl* typeLayout)
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

SLANG_NO_THROW GfxCount SLANG_MCALL RootShaderObjectImpl::getEntryPointCount()
{
    return (GfxCount)entryPointObjects.size();
}

SLANG_NO_THROW Result SLANG_MCALL RootShaderObjectImpl::getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint)
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
