#include "debug-transient-heap.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugTransientResourceHeap::queryInterface(SlangUUID const& uuid, void** outObject)
{
    if (uuid == GUID::IID_ISlangUnknown || uuid == GUID::IID_ITransientResourceHeap)
        *outObject = static_cast<ITransientResourceHeap*>(this);
    if (uuid == GUID::IID_ITransientResourceHeapD3D12)
    {
        RefPtr<DebugTransientResourceHeapD3D12> result = new DebugTransientResourceHeapD3D12(ctx);
        baseObject->queryInterface(uuid, (void**)result->baseObject.writeRef());
        returnComPtr((ITransientResourceHeapD3D12**)outObject, result);
        return SLANG_OK;
    }
    else
    {
        return baseObject->queryInterface(uuid, outObject);
    }
}

Result DebugTransientResourceHeap::synchronizeAndReset()
{
    SLANG_RHI_API_FUNC;
    return baseObject->synchronizeAndReset();
}

Result DebugTransientResourceHeap::finish()
{
    SLANG_RHI_API_FUNC;
    return baseObject->finish();
}

Result DebugTransientResourceHeap::createCommandBuffer(ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugCommandBuffer> outObject = new DebugCommandBuffer(ctx);
    outObject->m_transientHeap = this;
    auto result = baseObject->createCommandBuffer(outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    outObject->queryInterface(ICommandBuffer::getTypeGuid(), (void**)outCommandBuffer);
    return result;
}

Result DebugTransientResourceHeapD3D12::queryInterface(SlangUUID const& uuid, void** outObject)
{
    if (uuid == GUID::IID_ISlangUnknown || uuid == GUID::IID_ITransientResourceHeapD3D12)
        *outObject = static_cast<ITransientResourceHeapD3D12*>(this);
    if (uuid == GUID::IID_ITransientResourceHeap)
    {
        RefPtr<DebugTransientResourceHeap> result = new DebugTransientResourceHeap(ctx);
        baseObject->queryInterface(uuid, (void**)result->baseObject.writeRef());
        returnComPtr((ITransientResourceHeap**)outObject, result);
        return SLANG_OK;
    }
    else
    {
        return baseObject->queryInterface(uuid, outObject);
    }
}

Result DebugTransientResourceHeapD3D12::allocateTransientDescriptorTable(
    DescriptorType type,
    GfxCount count,
    Offset& outDescriptorOffset,
    void** outD3DDescriptorHeapHandle
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->allocateTransientDescriptorTable(type, count, outDescriptorOffset, outD3DDescriptorHeapHandle);
}

} // namespace rhi::debug
