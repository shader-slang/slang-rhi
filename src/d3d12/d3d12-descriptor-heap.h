#pragma once

#include "d3d12-base.h"

#include "core/offset-allocator.h"

namespace rhi::d3d12 {

/// A plain D3D12 descriptor heap.
/// This is a simple wrapper around ID3D12DescriptorHeap that provides a more convenient interface.
class DescriptorHeap
{
public:
    Result init(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_DESCRIPTOR_HEAP_FLAGS flags,
        uint32_t size
    );

    /// Return the underlying D3D12 descriptor heap.
    ID3D12DescriptorHeap* getHeap() const { return m_heap; }
    /// Return the number of descriptors in the heap.
    size_t getSize() const { return m_size; }
    /// Return the size of each descriptor.
    size_t getDescriptorSize() const { return m_descriptorSize; }

    /// Return the CPU descriptor handle at the specified index.
    inline D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(uint32_t index) const
    {
        SLANG_RHI_ASSERT(index < m_size);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
        handle.ptr += index * m_descriptorSize;
        return handle;
    }

    /// Return the GPU descriptor handle at the specified index.
    inline D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(uint32_t index) const
    {
        SLANG_RHI_ASSERT(m_gpuStart.ptr != 0);
        SLANG_RHI_ASSERT(index < m_size);
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
        handle.ptr += index * m_descriptorSize;
        return handle;
    }

private:
    ID3D12Device* m_device;
    ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t m_size;
    uint32_t m_descriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart;
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart;
};

/// Represents a single allocated CPU descriptor.
struct CPUDescriptorAllocation
{
    /// The CPU descriptor handle.
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {0};

    /// Returns true if the allocation is valid.
    bool isValid() const { return cpuHandle.ptr != 0; }
    explicit operator bool() const { return isValid(); }

private:
    uint32_t heapIndex;
    OffsetAllocator::Allocation heapOffset;

    friend class CPUDescriptorHeap;
};

/// Represents a range of allocated CPU descriptors.
struct CPUDescriptorRangeAllocation
{
    /// The first CPU descriptor handle in the range.
    D3D12_CPU_DESCRIPTOR_HANDLE firstCpuHandle = {0};
    /// The number of descriptors in the range.
    uint32_t count;


    /// Return the CPU descriptor handle at the specified index.
    D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(uint32_t index) const
    {
        SLANG_RHI_ASSERT(index < count);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = firstCpuHandle;
        handle.ptr += index * descriptorSize;
        return handle;
    }

    /// Returns true if the allocation is valid.
    bool isValid() const { return firstCpuHandle.ptr != 0; }
    explicit operator bool() const { return isValid(); }

private:
    uint16_t descriptorSize;
    uint32_t heapIndex;
    OffsetAllocator::Allocation heapOffset;

    friend class CPUDescriptorHeap;
};

/// A CPU (non-shader-visible) descriptor heap.
/// Manages a set of pages, each of which is a separate descriptor heap.
class CPUDescriptorHeap : public RefObject
{
public:
    static Result create(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t pageSize,
        CPUDescriptorHeap** outHeap
    );

    CPUDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t pageSize);
    ~CPUDescriptorHeap();

    /// Allocate a single descriptor.
    CPUDescriptorAllocation allocate();
    /// Free a single descriptor.
    void free(const CPUDescriptorAllocation& allocation);

    /// Allocate a range of descriptors.
    CPUDescriptorRangeAllocation allocate(uint32_t count);
    /// Free a range of descriptors.
    void free(const CPUDescriptorRangeAllocation& allocation);

private:
    Result allocate(
        uint32_t count,
        D3D12_CPU_DESCRIPTOR_HANDLE& outHandle,
        uint32_t& outHeapIndex,
        OffsetAllocator::Allocation& outHeapOffset
    );
    void free(uint32_t heapIndex, OffsetAllocator::Allocation heapOffset);
    Result newPage(uint32_t size);

    struct Page
    {
        uint32_t heapIndex;
        DescriptorHeap heap;
        OffsetAllocator allocator;

        Page(uint32_t heapIndex, uint32_t size)
            : heapIndex(heapIndex)
            , allocator(size, size)
        {
        }
    };

    ID3D12Device* m_device;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type;
    uint32_t m_pageSize;
    uint32_t m_descriptorSize;
    std::mutex m_mutex;
    std::vector<Page*> m_pages;
    std::set<Page*> m_freePages;
};

/// Represents a range of descriptors in a GPU (shader-visible) descriptor heap.
struct GPUDescriptorRange
{
    /// The first CPU descriptor handle in the range.
    D3D12_CPU_DESCRIPTOR_HANDLE firstCpuHandle = {0};
    /// The first GPU descriptor handle in the range.
    D3D12_GPU_DESCRIPTOR_HANDLE firstGpuHandle = {0};
    /// The number of descriptors in the range.
    uint32_t count;
    /// The size of each descriptor.
    uint16_t descriptorSize;

    /// Return the CPU descriptor handle at the specified index.
    D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(uint32_t index) const
    {
        SLANG_RHI_ASSERT(index < count);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = firstCpuHandle;
        handle.ptr += index * descriptorSize;
        return handle;
    }

    /// Return the GPU descriptor handle at the specified index.
    D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(uint32_t index) const
    {
        SLANG_RHI_ASSERT(index < count);
        D3D12_GPU_DESCRIPTOR_HANDLE handle = firstGpuHandle;
        handle.ptr += index * descriptorSize;
        return handle;
    }

    /// Returns true if the range is valid.
    bool isValid() const { return firstGpuHandle.ptr != 0; }
    explicit operator bool() const { return isValid(); }
};

/// Represents a range of allocated GPU descriptors.
struct GPUDescriptorRangeAllocation : GPUDescriptorRange
{
    /// Returns the descriptor heap offset of the first descriptor in the range.
    uint32_t getHeapOffset() const { return heapOffset.offset; }

private:
    OffsetAllocator::Allocation heapOffset;

    friend class GPUDescriptorHeap;
};

/// A GPU (shader-visible) descriptor heap.
/// Manages a single descriptor heap.
class GPUDescriptorHeap : public RefObject
{
public:
    static Result create(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t size,
        uint32_t maxAllocations,
        GPUDescriptorHeap** outHeap
    );

    GPUDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t size, uint32_t maxAllocations);
    ~GPUDescriptorHeap();

    /// Return the underlying D3D12 descriptor heap.
    ID3D12DescriptorHeap* getHeap() const { return m_heap.getHeap(); }

    /// Allocate a range of descriptors.
    GPUDescriptorRangeAllocation allocate(uint32_t count);
    /// Free a range of descriptors.
    void free(const GPUDescriptorRangeAllocation& allocation);

private:
    ID3D12Device* m_device;
    uint32_t m_size;
    uint32_t m_descriptorSize;
    std::mutex m_mutex;
    DescriptorHeap m_heap;
    OffsetAllocator m_allocator;
};

/// Manages an arena of GPU descriptors.
/// Allocates chunks from a GPU descriptor heap and then sub-allocates from those chunks.
class GPUDescriptorArena : public RefObject
{
public:
    Result init(GPUDescriptorHeap* heap, uint32_t chunkSize);

    ~GPUDescriptorArena();

    void reset();

    GPUDescriptorRange allocate(uint32_t count);

private:
    GPUDescriptorHeap* m_heap;
    uint32_t m_chunkSize;
    std::vector<GPUDescriptorRangeAllocation> m_chunks;
    uint32_t m_currentChunkSpace;
    uint32_t m_currentChunkOffset;
};

} // namespace rhi::d3d12
