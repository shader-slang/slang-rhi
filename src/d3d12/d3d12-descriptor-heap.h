#pragma once

#include "core/common.h"
#include "core/short_vector.h"
#include "core/virtual-object-pool.h"

#include <slang-com-ptr.h>

#include <d3d12.h>
#include <dxgi.h>

#include <vector>

namespace rhi::d3d12 {

/*! \brief A simple class to manage an underlying Dx12 Descriptor Heap. Allocations are made linearly in order. It is
not possible to free individual allocations, but all allocations can be deallocated with 'deallocateAll'. */
class D3D12DescriptorHeap
{
public:
    typedef D3D12DescriptorHeap ThisType;

    /// Initialize
    Result init(ID3D12Device* device, int size, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
    /// Initialize with an array of handles copying over the representation
    Result init(
        ID3D12Device* device,
        const D3D12_CPU_DESCRIPTOR_HANDLE* handles,
        int numHandles,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_DESCRIPTOR_HEAP_FLAGS flags
    );

    /// Returns the number of slots that have been used
    SLANG_FORCE_INLINE int getUsedSize() const { return m_currentIndex; }

    /// Get the total amount of descriptors possible on the heap
    SLANG_FORCE_INLINE int getTotalSize() const { return m_totalSize; }
    /// Allocate a descriptor. Returns the index, or -1 if none left.
    SLANG_FORCE_INLINE int allocate();
    /// Allocate a number of descriptors. Returns the start index (or -1 if not possible)
    SLANG_FORCE_INLINE int allocate(int numDescriptors);

    ///
    SLANG_FORCE_INLINE int placeAt(int index);

    /// Deallocates all allocations, and starts allocation from the start of the underlying heap again
    SLANG_FORCE_INLINE void deallocateAll() { m_currentIndex = 0; }

    /// Get the size of each
    SLANG_FORCE_INLINE int getDescriptorSize() const { return m_descriptorSize; }

    /// Get the GPU heap start
    SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE getGpuStart() const
    {
        return m_heap->GetGPUDescriptorHandleForHeapStart();
    }

    /// Get the CPU heap start
    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuStart() const
    {
        return m_heap->GetCPUDescriptorHandleForHeapStart();
    }

    /// Get the GPU handle at the specified index
    SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(int index) const;

    /// Get the CPU handle at the specified index
    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(int index) const;

    /// Get the underlying heap
    SLANG_FORCE_INLINE ID3D12DescriptorHeap* getHeap() const { return m_heap; }

    /// Ctor
    D3D12DescriptorHeap();

protected:
    ComPtr<ID3D12Device> m_device;
    /// The underlying heap being allocated from
    ComPtr<ID3D12DescriptorHeap> m_heap;
    /// Total amount of allocations available on the heap
    int m_totalSize;
    /// The current descriptor
    int m_currentIndex;
    /// The size of each descriptor
    int m_descriptorSize;
    /// The flags of the heap
    D3D12_DESCRIPTOR_HEAP_FLAGS m_heapFlags;
};

/// A d3d12 descriptor, used as "backing storage" for a view.
///
/// This type is intended to be used to represent descriptors that
/// are allocated and freed through a `D3D12GeneralDescriptorHeap`.
struct D3D12Descriptor
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    operator bool() const { return cpuHandle.ptr != 0; }
};

/// An allocator for host-visible descriptors.
///
/// Unlike the `D3D12DescriptorHeap` type, this class allows for both
/// allocation and freeing of descriptors, by maintaining a free list.
///
class D3D12GeneralDescriptorHeap : public RefObject
{
    ID3D12Device* m_device;
    int m_chunkSize;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type;

    D3D12DescriptorHeap m_heap;
    VirtualObjectPool m_allocator;

public:
    int getSize() { return m_chunkSize; }

    Result init(ID3D12Device* device, int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_device = device;
        m_chunkSize = chunkSize;
        m_type = type;

        SLANG_RETURN_ON_FAIL(m_heap.init(m_device, m_chunkSize, m_type, flag));
        m_allocator.initPool(m_chunkSize);
        return SLANG_OK;
    }

    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(int index) const { return m_heap.getCpuHandle(index); }

    SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(int index) const { return m_heap.getGpuHandle(index); }

    int allocate(int count) { return m_allocator.alloc(count); }

    Result allocate(D3D12Descriptor* outDescriptor)
    {
        // TODO: this allocator would take some work to make thread-safe

        int index = m_allocator.alloc(1);
        if (index < 0)
        {
            SLANG_RHI_ASSERT_FAILURE("Descriptor allocation failed");
            return SLANG_FAIL;
        }

        D3D12Descriptor descriptor;
        descriptor.cpuHandle = m_heap.getCpuHandle(index);

        *outDescriptor = descriptor;
        return SLANG_OK;
    }

    void free(int index, int count) { m_allocator.free(index, count); }

    void free(D3D12Descriptor descriptor)
    {
        auto index = (int)(descriptor.cpuHandle.ptr - m_heap.getCpuStart().ptr) / m_heap.getDescriptorSize();
        free(index, 1);
    }
};

class D3D12GeneralExpandingDescriptorHeap : public RefObject
{
    ID3D12Device* m_device;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type;
    D3D12_DESCRIPTOR_HEAP_FLAGS m_flag;
    int m_chunkSize;
    std::vector<RefPtr<D3D12GeneralDescriptorHeap>> m_subHeaps;
    std::vector<int> m_subHeapStartingIndex;

public:
    Result newSubHeap()
    {
        RefPtr<D3D12GeneralDescriptorHeap> subHeap = new D3D12GeneralDescriptorHeap();
        SLANG_RETURN_ON_FAIL(subHeap->init(m_device, m_chunkSize, m_type, m_flag));
        m_subHeaps.push_back(subHeap);
        if (m_subHeapStartingIndex.size())
        {
            m_subHeapStartingIndex.push_back(m_subHeapStartingIndex.back() + m_subHeaps.back()->getSize());
        }
        else
        {
            m_subHeapStartingIndex.push_back(0);
        }
        return SLANG_OK;
    }

    int getSubHeapIndex(int descriptorIndex) const
    {
        Index l = 0;
        Index r = m_subHeapStartingIndex.size();
        while (l < r - 1)
        {
            Index m = l + (r - l) / 2;
            if (m_subHeapStartingIndex[m] < descriptorIndex)
                l = m;
            else if (m_subHeapStartingIndex[m] > descriptorIndex)
                r = m;
            else
                return (int)m;
        }
        SLANG_RHI_ASSERT(
            m_subHeapStartingIndex[l] <= descriptorIndex &&
            m_subHeapStartingIndex[l] + m_subHeaps[l]->getSize() > descriptorIndex
        );
        return (int)l;
    }

    Result init(ID3D12Device* device, int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_device = device;
        m_chunkSize = chunkSize;
        m_type = type;
        m_flag = flag;

        return newSubHeap();
    }

    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(int index) const
    {
        auto subHeapIndex = getSubHeapIndex(index);
        return m_subHeaps[subHeapIndex]->getCpuHandle(index - m_subHeapStartingIndex[subHeapIndex]);
    }

    SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(int index) const
    {
        auto subHeapIndex = getSubHeapIndex(index);
        return m_subHeaps[subHeapIndex]->getGpuHandle(index - m_subHeapStartingIndex[subHeapIndex]);
    }

    int allocate(int count)
    {
        auto result = m_subHeaps.back()->allocate(count);
        if (result == -1)
        {
            newSubHeap();
            return allocate(count);
        }
        return result + m_subHeapStartingIndex.back();
    }

    Result allocate(D3D12Descriptor* outDescriptor)
    {
        int index = allocate(1);
        if (index < 0)
        {
            SLANG_RHI_ASSERT_FAILURE("Descriptor allocation failed");
            return SLANG_FAIL;
        }

        D3D12Descriptor descriptor;
        descriptor.cpuHandle = getCpuHandle(index);

        *outDescriptor = descriptor;
        return SLANG_OK;
    }

    void free(int index, int count)
    {
        auto subHeapIndex = getSubHeapIndex(index);
        m_subHeaps[subHeapIndex]->free(index - m_subHeapStartingIndex[subHeapIndex], count);
    }

    void free(D3D12Descriptor descriptor)
    {
        for (auto& subHeap : m_subHeaps)
        {
            if (descriptor.cpuHandle.ptr >= subHeap->getCpuHandle(0).ptr)
            {
                auto subIndex = descriptor.cpuHandle.ptr - subHeap->getCpuHandle(0).ptr;
                if (subIndex < (SIZE_T)subHeap->getSize())
                {
                    subHeap->free(descriptor);
                    break;
                }
            }
        }
    }
};

class D3D12LinearExpandingDescriptorHeap : public RefObject
{
    ID3D12Device* m_device;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type;
    D3D12_DESCRIPTOR_HEAP_FLAGS m_flag;
    int m_chunkSize;
    short_vector<D3D12DescriptorHeap, 4> m_subHeaps;
    int32_t m_subHeapIndex;

public:
    Result newSubHeap()
    {
        m_subHeapIndex++;
        if (m_subHeapIndex <= m_subHeaps.size())
        {
            D3D12DescriptorHeap subHeap;
            SLANG_RETURN_ON_FAIL(subHeap.init(m_device, m_chunkSize, m_type, m_flag));
            m_subHeaps.push_back(_Move(subHeap));
        }
        return SLANG_OK;
    }

    Result init(ID3D12Device* device, int chunkSize, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
    {
        m_device = device;
        m_chunkSize = chunkSize;
        m_type = type;
        m_flag = flag;
        m_subHeapIndex = -1;
        return newSubHeap();
    }

    int allocate(int count)
    {
        auto result = m_subHeaps[m_subHeapIndex].allocate(count);
        if (result == -1)
        {
            newSubHeap();
            return allocate(count);
        }
        SLANG_RHI_ASSERT(result <= 0xFFFFFF);
        SLANG_RHI_ASSERT(m_subHeapIndex <= 255);
        return (m_subHeapIndex << 24) + result;
    }

    SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(int index) const
    {
        auto subHeapIndex = ((uint32_t)(index >> 24) & 0xFF);
        return m_subHeaps[subHeapIndex].getCpuHandle(index & 0xFFFFFF);
    }

    void free(int index, int count) { SLANG_RHI_ASSERT_FAILURE("Not supported"); }

    void free(D3D12Descriptor descriptor) { SLANG_RHI_ASSERT_FAILURE("Not supported"); }

    void freeAll()
    {
        for (auto& subHeap : m_subHeaps)
            subHeap.deallocateAll();
        m_subHeapIndex = 0;
    }
};

struct DescriptorHeapReference
{
    enum class Type
    {
        Linear,
        General,
        ExpandingGeneral,
        ExpandingLinear
    };
    union Ptr
    {
        D3D12DescriptorHeap* linearHeap;
        D3D12GeneralDescriptorHeap* generalHeap;
        D3D12GeneralExpandingDescriptorHeap* generalExpandingHeap;
        D3D12LinearExpandingDescriptorHeap* linearExpandingHeap;
    };
    Type type;
    Ptr ptr;
    DescriptorHeapReference() = default;
    DescriptorHeapReference(D3D12DescriptorHeap* heap)
    {
        type = Type::Linear;
        ptr.linearHeap = heap;
    }
    DescriptorHeapReference(D3D12GeneralDescriptorHeap* heap)
    {
        type = Type::General;
        ptr.generalHeap = heap;
    }
    DescriptorHeapReference(D3D12GeneralExpandingDescriptorHeap* heap)
    {
        type = Type::ExpandingGeneral;
        ptr.generalExpandingHeap = heap;
    }
    DescriptorHeapReference(D3D12LinearExpandingDescriptorHeap* heap)
    {
        type = Type::ExpandingLinear;
        ptr.linearExpandingHeap = heap;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(int index) const
    {
        switch (type)
        {
        case Type::Linear:
            return ptr.linearHeap->getCpuHandle(index);
        case Type::General:
            return ptr.generalHeap->getCpuHandle(index);
        case Type::ExpandingGeneral:
            return ptr.generalExpandingHeap->getCpuHandle(index);
        case Type::ExpandingLinear:
            return ptr.linearExpandingHeap->getCpuHandle(index);
        default:
            return D3D12_CPU_DESCRIPTOR_HANDLE();
        }
    }
    D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(int index) const
    {
        switch (type)
        {
        case Type::Linear:
            return ptr.linearHeap->getGpuHandle(index);
        case Type::General:
            return ptr.generalHeap->getGpuHandle(index);
        case Type::ExpandingGeneral:
            return ptr.generalExpandingHeap->getGpuHandle(index);
        default:
            return D3D12_GPU_DESCRIPTOR_HANDLE();
        }
    }
    int allocate(int numDescriptors)
    {
        switch (type)
        {
        case Type::Linear:
            return ptr.linearHeap->allocate(numDescriptors);
        case Type::General:
            return ptr.generalHeap->allocate(numDescriptors);
        case Type::ExpandingGeneral:
            return ptr.generalExpandingHeap->allocate(numDescriptors);
        default:
            return ptr.linearExpandingHeap->allocate(numDescriptors);
        }
    }
    void free(int index, int count)
    {
        switch (type)
        {
        default:
        case Type::Linear:
            SLANG_RHI_ASSERT_FAILURE("Linear heap does not support free().");
            break;
        case Type::General:
            return ptr.generalHeap->free(index, count);
        case Type::ExpandingGeneral:
            return ptr.generalExpandingHeap->free(index, count);
        }
    }
    void freeIfSupported(int index, int count)
    {
        switch (type)
        {
        case Type::Linear:
            return;
        case Type::General:
            return ptr.generalHeap->free(index, count);
        case Type::ExpandingGeneral:
            return ptr.generalExpandingHeap->free(index, count);
        default:
            break;
        }
    }
};

// ---------------------------------------------------------------------------
int D3D12DescriptorHeap::allocate()
{
    return allocate(1);
}
// ---------------------------------------------------------------------------
int D3D12DescriptorHeap::allocate(int numDescriptors)
{
    if (m_currentIndex + numDescriptors <= m_totalSize)
    {
        const int index = m_currentIndex;
        m_currentIndex += numDescriptors;
        return index;
    }
    if (m_heapFlags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        // No automatic resizing for GPU visible heaps.
        return -1;
    }
    // We don't have enough heap size, resize the heap.
    auto oldHeap = m_heap;
    auto oldSize = m_totalSize;
    auto currentIndex = m_currentIndex;
    auto desc = m_heap->GetDesc();
    this->init(m_device, (int)desc.NumDescriptors * 2, desc.Type, desc.Flags);
    m_device->CopyDescriptorsSimple(
        (UINT)currentIndex,
        m_heap->GetCPUDescriptorHandleForHeapStart(),
        oldHeap->GetCPUDescriptorHandleForHeapStart(),
        desc.Type
    );
    m_currentIndex = currentIndex;
    // Now allocate again.
    const int index = m_currentIndex;
    m_currentIndex += numDescriptors;
    return index;
}
// ---------------------------------------------------------------------------
SLANG_FORCE_INLINE int D3D12DescriptorHeap::placeAt(int index)
{
    SLANG_RHI_ASSERT(index >= 0 && index < m_totalSize);
    m_currentIndex = index + 1;
    return index;
}

// ---------------------------------------------------------------------------
SLANG_FORCE_INLINE D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::getCpuHandle(int index) const
{
    SLANG_RHI_ASSERT(index >= 0 && index < m_totalSize);
    D3D12_CPU_DESCRIPTOR_HANDLE start = m_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dst;
    dst.ptr = start.ptr + m_descriptorSize * index;
    return dst;
}
// ---------------------------------------------------------------------------
SLANG_FORCE_INLINE D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::getGpuHandle(int index) const
{
    SLANG_RHI_ASSERT(index >= 0 && index < m_totalSize);
    D3D12_GPU_DESCRIPTOR_HANDLE start = m_heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE dst;
    dst.ptr = start.ptr + m_descriptorSize * index;
    return dst;
}

} // namespace rhi::d3d12
