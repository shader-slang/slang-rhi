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
        if (!entry.head)
            entry.size = size;
        else
            SLANG_RHI_ASSERT(entry.size == size);
        buffer->m_nextAtSameAddr = entry.head;
        entry.head = buffer;
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
        /// Imported buffers can alias an existing address; chaining avoids allocating here.
        BufferImpl* head = nullptr;
        DeviceAddress size = 0;
    };

    BufferImpl* findByRange(DeviceAddress addr)
    {
        if (m_sortedDirty)
            rebuildSorted();

        if (m_sorted.empty())
            return nullptr;

        // upper_bound finds first entry with base > addr; step back one.
        auto it = std::upper_bound(
            m_sorted.begin(),
            m_sorted.end(),
            addr,
            [](DeviceAddress a, const std::pair<DeviceAddress, Entry>& e)
            {
                return a < e.first;
            }
        );

        if (it == m_sorted.begin())
            return nullptr;

        --it;
        if (addr < it->first + it->second.size)
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
        m_sortedDirty = false;
    }

    std::mutex m_mutex;
    std::unordered_map<DeviceAddress, Entry> m_baseAddrMap;
    std::vector<std::pair<DeviceAddress, Entry>> m_sorted;
    bool m_sortedDirty = true;
};

} // namespace rhi::metal
