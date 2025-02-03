#include "d3d12-descriptor-heap.h"

namespace rhi::d3d12 {

// DescriptorHeap

Result DescriptorHeap::init(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    D3D12_DESCRIPTOR_HEAP_FLAGS flags,
    uint32_t size
)
{
    m_device = device;
    m_size = size;
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(type);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = size;
    heapDesc.Flags = flags;
    heapDesc.Type = type;
    SLANG_RETURN_ON_FAIL(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_heap.writeRef())));

    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    else
        m_gpuStart.ptr = 0;

    return SLANG_OK;
}

// CPUDescriptorHeap

Result CPUDescriptorHeap::create(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t pageSize,
    CPUDescriptorHeap** outHeap
)
{
    RefPtr<CPUDescriptorHeap> heap = new CPUDescriptorHeap(device, type, pageSize);
    returnRefPtrMove(outHeap, heap);
    return SLANG_OK;
}

CPUDescriptorHeap::CPUDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t pageSize)
    : m_device(device)
    , m_type(type)
    , m_pageSize(pageSize)
{
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(type);
}

CPUDescriptorHeap::~CPUDescriptorHeap()
{
    for (Page* page : m_pages)
    {
        SLANG_RHI_ASSERT(page->allocator.storageReport().totalFreeSpace == m_pageSize);
        delete page;
    }
}

CPUDescriptorAllocation CPUDescriptorHeap::allocate()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CPUDescriptorAllocation allocation;
    if (SLANG_SUCCEEDED(allocate(1, allocation.cpuHandle, allocation.heapIndex, allocation.heapOffset)))
    {
        return allocation;
    }
    return {};
}

void CPUDescriptorHeap::free(const CPUDescriptorAllocation& allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    free(allocation.heapIndex, allocation.heapOffset);
}

CPUDescriptorRangeAllocation CPUDescriptorHeap::allocate(uint32_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CPUDescriptorRangeAllocation allocation;
    allocation.count = count;
    allocation.descriptorSize = m_descriptorSize;
    if (SLANG_SUCCEEDED(allocate(count, allocation.firstCpuHandle, allocation.heapIndex, allocation.heapOffset)))
    {
        return allocation;
    }
    return {};
}

void CPUDescriptorHeap::free(const CPUDescriptorRangeAllocation& allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    free(allocation.heapIndex, allocation.heapOffset);
}

Result CPUDescriptorHeap::allocate(
    uint32_t count,
    D3D12_CPU_DESCRIPTOR_HANDLE& outHandle,
    uint32_t& outHeapIndex,
    OffsetAllocator::Allocation& outHeapOffset
)
{
    while (true)
    {
        if (m_freePages.empty())
        {
            uint32_t size = max(m_pageSize, count);
            if (!SUCCEEDED(newPage(size)))
            {
                return {};
            }
        }
        SLANG_RHI_ASSERT(!m_freePages.empty());
        Page* page = *m_freePages.begin();
        OffsetAllocator::Allocation offset = page->allocator.allocate(count);
        if (offset)
        {
            outHandle = page->heap.getCpuHandle(offset.offset);
            outHeapIndex = page->heapIndex;
            outHeapOffset = offset;
            return SLANG_OK;
        }
        m_freePages.erase(page);
    }
}

void CPUDescriptorHeap::free(uint32_t heapIndex, OffsetAllocator::Allocation heapOffset)
{
    SLANG_RHI_ASSERT(heapIndex < m_pages.size());
    SLANG_RHI_ASSERT(heapOffset);

    Page* page = m_pages[heapIndex];

    SLANG_RHI_ASSERT(page->heap.getCpuHandle(0).ptr <= page->heap.getCpuHandle(heapOffset.offset).ptr);
    SLANG_RHI_ASSERT(page->heap.getCpuHandle(heapOffset.offset).ptr <= page->heap.getCpuHandle(m_pageSize - 1).ptr);

    page->allocator.free(heapOffset);
    m_freePages.insert(page);
}

Result CPUDescriptorHeap::newPage(uint32_t size)
{
    uint32_t heapIndex = uint32_t(m_pages.size());
    Page* page = new Page(heapIndex, size);
    SLANG_RETURN_ON_FAIL(page->heap.init(m_device, m_type, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, size));
    m_pages.push_back(page);
    m_freePages.insert(page);
    return SLANG_OK;
}

// GPUDescriptorHeap

Result GPUDescriptorHeap::create(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t size,
    uint32_t maxAllocations,
    GPUDescriptorHeap** outHeap
)
{
    RefPtr<GPUDescriptorHeap> heap = new GPUDescriptorHeap(device, type, size, maxAllocations);
    SLANG_RETURN_ON_FAIL(heap->m_heap.init(device, type, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, size));
    returnRefPtrMove(outHeap, heap);
    return SLANG_OK;
}

GPUDescriptorHeap::GPUDescriptorHeap(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t size,
    uint32_t maxAllocations
)
    : m_device(device)
    , m_type(type)
    , m_size(size)
    , m_allocator(size, maxAllocations)
{
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(type);
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
    SLANG_RHI_ASSERT(m_allocator.storageReport().totalFreeSpace == m_size);
}

GPUDescriptorRangeAllocation GPUDescriptorHeap::allocate(uint32_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    OffsetAllocator::Allocation heapOffset = m_allocator.allocate(count);
    if (heapOffset)
    {
        GPUDescriptorRangeAllocation allocation;
        allocation.firstCpuHandle = m_heap.getCpuHandle(heapOffset.offset);
        allocation.firstGpuHandle = m_heap.getGpuHandle(heapOffset.offset);
        allocation.count = count;
        allocation.descriptorSize = m_descriptorSize;
        allocation.heapOffset = heapOffset;
        return allocation;
    }
    return {};
}

void GPUDescriptorHeap::free(const GPUDescriptorRangeAllocation& allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    SLANG_RHI_ASSERT(allocation.heapOffset);
    SLANG_RHI_ASSERT(m_heap.getCpuHandle(0).ptr <= m_heap.getCpuHandle(allocation.heapOffset.offset).ptr);
    SLANG_RHI_ASSERT(m_heap.getCpuHandle(allocation.heapOffset.offset).ptr <= m_heap.getCpuHandle(m_size - 1).ptr);

    m_allocator.free(allocation.heapOffset);
}

// GPUDescriptorArena

Result GPUDescriptorArena::init(GPUDescriptorHeap* heap, uint32_t chunkSize)
{
    SLANG_RHI_ASSERT(chunkSize > 0);
    m_heap = heap;
    m_chunkSize = chunkSize;
    m_currentChunkSpace = 0;
    m_currentChunkOffset = 0;
    return SLANG_OK;
}

GPUDescriptorArena::~GPUDescriptorArena()
{
    reset();
}

void GPUDescriptorArena::reset()
{
    for (const auto& chunk : m_chunks)
    {
        m_heap->free(chunk);
    }
    m_chunks.clear();
    m_currentChunkOffset = 0;
    m_currentChunkSpace = 0;
}

GPUDescriptorRange GPUDescriptorArena::allocate(uint32_t count)
{
    if (count > m_currentChunkSpace)
    {
        uint32_t chunkSize = max(count, m_chunkSize);
        m_chunks.push_back(m_heap->allocate(chunkSize));
        m_currentChunkSpace = chunkSize;
        m_currentChunkOffset = 0;
    }
    SLANG_RHI_ASSERT(count <= m_currentChunkSpace);
    GPUDescriptorRange range = m_chunks.back();
    range.firstCpuHandle.ptr += m_currentChunkOffset * range.descriptorSize;
    range.firstGpuHandle.ptr += m_currentChunkOffset * range.descriptorSize;
    range.count = count;
    m_currentChunkOffset += count;
    m_currentChunkSpace -= count;
    return range;
}

} // namespace rhi::d3d12
