#include "d3d12-constant-buffer-pool.h"
#include "d3d12-buffer.h"

namespace rhi::d3d12 {

void ConstantBufferPool::init(StagingHeap* heap)
{
    m_heap = heap;
}

void ConstantBufferPool::finish() {}

void ConstantBufferPool::reset()
{
    m_allocations.clear();
}

Result ConstantBufferPool::allocate(size_t size, Allocation& outAllocation)
{
    outAllocation = {};

    if (size > m_heap->getMaxPageSize())
    {
        return SLANG_FAIL;
    }

    RefPtr<StagingHeap::Handle> handle;
    SLANG_RETURN_ON_FAIL(m_heap->allocHandle(size, {}, handle.writeRef()));

    void* mappedData = nullptr;
    SLANG_RETURN_ON_FAIL(handle->map(&mappedData));
    if (!mappedData)
        return SLANG_FAIL;

    m_allocations.push_back(handle);
    outAllocation.buffer = checked_cast<BufferImpl*>(handle->getBuffer());
    outAllocation.offset = handle->getOffset();
    outAllocation.mappedData = mappedData;
    return SLANG_OK;
}

} // namespace rhi::d3d12
