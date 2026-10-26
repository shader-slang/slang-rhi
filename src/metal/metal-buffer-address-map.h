#pragma once

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metal-buffer.h"

#include "slang-rhi.h"

namespace rhi::metal {

/// Thread-safe map from GPU virtual addresses to their owning BufferImpl.
///
/// Provides O(1) average lookup for exact base-address matches (the common case)
/// and O(log N) fallback for pointers that land inside a buffer's address range.
/// The first range lookup after a representative changes rebuilds an O(N log N)
/// sorted index, where N is the number of distinct base addresses.
///
/// Used by the residency fallback path (!m_hasResidencySet) to resolve device
/// pointers found in shader uniform data for useResources calls.
class BufferAddressMap
{
public:
    void insert(DeviceAddress baseAddr, DeviceAddress size, BufferImpl* buffer)
    {
        // Native buffer ranges are nonempty and cannot wrap the address space.
        SLANG_RHI_ASSERT(size > 0 && size - 1 <= std::numeric_limits<DeviceAddress>::max() - baseAddr);
        std::lock_guard<std::mutex> lock(m_mutex);
        // A buffer has only one intrusive link, so registering it twice would create a cycle.
        SLANG_RHI_ASSERT(buffer->m_addressMapSize == 0);
        Entry& entry = m_baseAddrMap[baseAddr];
        // Placed aliases can share a base address but have different lengths. Keep
        // the largest buffer first so every lookup returns a buffer covering the address.
        buffer->m_addressMapSize = size;
        // Only the head must be largest. Inserting smaller aliases immediately
        // after it avoids scanning the chain and leaves the range index valid.
        BufferImpl** link = &entry.head;
        if (entry.head && size <= entry.head->m_addressMapSize)
            link = &entry.head->m_nextAtSameAddr;
        else
            m_sortedDirty = true;
        buffer->m_nextAtSameAddr = *link;
        *link = buffer;
    }

    void erase(DeviceAddress baseAddr, BufferImpl* buffer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto entry = m_baseAddrMap.find(baseAddr);
        if (entry == m_baseAddrMap.end())
            return;
        BufferImpl*& head = entry->second.head;
        for (BufferImpl** link = &head; *link; link = &(*link)->m_nextAtSameAddr)
        {
            if (*link == buffer)
            {
                *link = buffer->m_nextAtSameAddr;
                buffer->m_nextAtSameAddr = nullptr;
                buffer->m_addressMapSize = 0;
                if (link == &head)
                {
                    // The remaining aliases are unordered. Promote the largest
                    // in one scan; only removing the representative needs this.
                    BufferImpl** largest = &head;
                    for (BufferImpl** candidate = &head; *candidate; candidate = &(*candidate)->m_nextAtSameAddr)
                    {
                        if ((*candidate)->m_addressMapSize > (*largest)->m_addressMapSize)
                            largest = candidate;
                    }
                    if (largest != &head)
                    {
                        BufferImpl* replacement = *largest;
                        *largest = replacement->m_nextAtSameAddr;
                        replacement->m_nextAtSameAddr = head;
                        head = replacement;
                    }
                    if (!head)
                        m_baseAddrMap.erase(entry);
                    m_sortedDirty = true;
                }
                return;
            }
        }
    }

    /// Look up the buffer owning a given GPU address.
    /// Returns nullptr if no buffer contains the address. This is a borrowed pointer;
    /// the map lock does not protect its lifetime after this function returns.
    BufferImpl* find(DeviceAddress addr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Fast path: exact base-address match.
        auto it = m_baseAddrMap.find(addr);
        if (it != m_baseAddrMap.end())
            return it->second.head;

        // Slow path: address may point into the middle of a buffer.
        return findByRange(addr);
    }

private:
    struct Entry
    {
        /// Buffers sharing this base address, linked through BufferImpl::m_nextAtSameAddr.
        /// The head has the largest size; the remaining aliases are unordered.
        BufferImpl* head = nullptr;
    };

    BufferImpl* findByRange(DeviceAddress addr)
    {
        if (m_sortedDirty)
            rebuildSorted();

        if (m_sorted.empty())
            return nullptr;

        // The range index has strictly increasing last addresses, even when buffers
        // overlap. Inclusive ends allow a range to include the maximum device address.
        auto it = std::lower_bound(
            m_sorted.begin(),
            m_sorted.end(),
            addr,
            [](const std::pair<DeviceAddress, Entry>& e, DeviceAddress a)
            {
                return e.first + (e.second.head->m_addressMapSize - 1) < a;
            }
        );

        if (it != m_sorted.end() && it->first <= addr)
            return it->second.head;

        return nullptr;
    }

    void rebuildSorted()
    {
        m_sorted.clear();
        m_sorted.reserve(m_baseAddrMap.size());
        for (auto& [addr, entry] : m_baseAddrMap)
            m_sorted.push_back({addr, entry});
        std::sort(
            m_sorted.begin(),
            m_sorted.end(),
            [](const auto& a, const auto& b)
            {
                return a.first < b.first;
            }
        );
        // A range contained in an earlier range adds no coverage. Dropping it
        // keeps both starts and ends sorted and preserves logarithmic lookup.
        size_t count = 0;
        DeviceAddress maxEnd = 0;
        for (const auto& entry : m_sorted)
        {
            DeviceAddress end = entry.first + (entry.second.head->m_addressMapSize - 1);
            if (count == 0 || end > maxEnd)
            {
                m_sorted[count++] = entry;
                maxEnd = end;
            }
        }
        m_sorted.resize(count);
        m_sortedDirty = false;
    }

    std::mutex m_mutex;
    std::unordered_map<DeviceAddress, Entry> m_baseAddrMap;
    std::vector<std::pair<DeviceAddress, Entry>> m_sorted;
    bool m_sortedDirty = true;
};

} // namespace rhi::metal
