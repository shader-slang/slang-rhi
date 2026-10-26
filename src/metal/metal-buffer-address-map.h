#pragma once

#include <algorithm>
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
/// A lazy-sorted vector is only built when a range lookup is actually needed.
///
/// Used by the residency fallback path (!m_hasResidencySet) to resolve device
/// pointers found in shader uniform data for useResources calls.
class BufferAddressMap
{
public:
    void insert(DeviceAddress baseAddr, DeviceAddress size, BufferImpl* buffer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Entry& entry = m_baseAddrMap[baseAddr];
        // Placed aliases can share a base address but have different lengths. Keep
        // the largest buffer first so every lookup returns a buffer covering the address.
        buffer->m_addressMapSize = size;
        BufferImpl** link = &entry.head;
        while (*link && (*link)->m_addressMapSize > size)
            link = &(*link)->m_nextAtSameAddr;
        buffer->m_nextAtSameAddr = *link;
        *link = buffer;
        m_sortedDirty = true;
    }

    void erase(DeviceAddress baseAddr, BufferImpl* buffer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto entry = m_baseAddrMap.find(baseAddr);
        if (entry == m_baseAddrMap.end())
            return;
        for (BufferImpl** link = &entry->second.head; *link; link = &(*link)->m_nextAtSameAddr)
        {
            if (*link == buffer)
            {
                *link = buffer->m_nextAtSameAddr;
                buffer->m_nextAtSameAddr = nullptr;
                break;
            }
        }
        if (!entry->second.head)
            m_baseAddrMap.erase(entry);
        m_sortedDirty = true;
    }

    /// Look up the buffer owning a given GPU address.
    /// Returns nullptr if no buffer contains the address.
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
        /// Sorted by decreasing size; imported and placed buffers can alias an existing address.
        BufferImpl* head = nullptr;
    };

    BufferImpl* findByRange(DeviceAddress addr)
    {
        if (m_sortedDirty)
            rebuildSorted();

        if (m_sorted.empty())
            return nullptr;

        // The range index has strictly increasing ends, even when buffers overlap.
        // Find the first range ending after addr, then check that it starts before addr.
        auto it = std::upper_bound(
            m_sorted.begin(),
            m_sorted.end(),
            addr,
            [](DeviceAddress a, const std::pair<DeviceAddress, Entry>& e)
            {
                return a < e.first + e.second.head->m_addressMapSize;
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
            DeviceAddress end = entry.first + entry.second.head->m_addressMapSize;
            if (end > maxEnd)
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
