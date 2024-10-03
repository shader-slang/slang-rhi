#pragma once

#include <cstring>
#include <cstdlib>

namespace rhi {

class StructHolder
{
public:
    ~StructHolder() { freeAll(); }

    void reset() { freeAll(); }

    void holdString(const char*& str)
    {
        if (str)
        {
            char* newStr = (char*)allocate(std::strlen(str) + 1);
            std::memcpy(newStr, str, std::strlen(str) + 1);
            str = newStr;
        }
    }

    template<typename T>
    void holdList(T*& items, size_t count)
    {
        if (items && count > 0)
        {
            T* newItems = (T*)allocate(sizeof(T) * count);
            std::memcpy((void*)newItems, items, sizeof(T) * count);
            items = newItems;
        }
    }

private:
    struct Allocation
    {
        Allocation* next = nullptr;
    };

    Allocation* m_allocations = nullptr;

    void* allocate(size_t size)
    {
        Allocation* allocation = (Allocation*)std::malloc(sizeof(Allocation) + size);
        allocation->next = m_allocations;
        m_allocations = allocation;
        return allocation + 1;
    }

    void freeAll()
    {
        Allocation* allocation = m_allocations;
        while (allocation)
        {
            Allocation* next = allocation->next;
            std::free(allocation);
            allocation = next;
        }
        m_allocations = nullptr;
    }
};

} // namespace rhi
