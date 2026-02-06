#pragma once

#include "assert.h"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace rhi {

/**
 * \brief A ring buffer queue that grows when full but never shrinks.
 *
 * Designed for efficient FIFO operations with amortized O(1) push/pop.
 * When the buffer is full, it grows by a factor of 2 and compacts
 * existing entries to the beginning. After reaching steady-state,
 * no further allocations occur.
 *
 * Elements are stored in insertion order - the oldest element is at the
 * front and will be popped first. This property enables early termination
 * when iterating through elements that are ordered by some monotonic key.
 *
 * \tparam T Element type
 */
template<typename T>
class RingQueue
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = value_type&;
    using const_reference = const value_type&;

    /// Default constructor with optional initial capacity.
    explicit RingQueue(size_type initial_capacity = 64)
        : m_head(0)
        , m_tail(0)
        , m_size(0)
    {
        m_buffer.resize(initial_capacity > 0 ? initial_capacity : 1);
    }

    /// Copy constructor.
    RingQueue(const RingQueue& other)
        : m_head(0)
        , m_tail(other.m_size)
        , m_size(other.m_size)
    {
        m_buffer.resize(other.m_buffer.size());
        // Copy elements in order, compacting to the beginning.
        for (size_type i = 0; i < other.m_size; ++i)
        {
            m_buffer[i] = other.m_buffer[(other.m_head + i) % other.m_buffer.size()];
        }
    }

    /// Move constructor.
    RingQueue(RingQueue&& other) noexcept
        : m_buffer(std::move(other.m_buffer))
        , m_head(other.m_head)
        , m_tail(other.m_tail)
        , m_size(other.m_size)
    {
        other.m_head = 0;
        other.m_tail = 0;
        other.m_size = 0;
    }

    /// Destructor.
    ~RingQueue() = default;

    /// Copy assignment.
    RingQueue& operator=(const RingQueue& other)
    {
        if (this != &other)
        {
            m_buffer.resize(other.m_buffer.size());
            m_head = 0;
            m_tail = other.m_size;
            m_size = other.m_size;
            // Copy elements in order, compacting to the beginning.
            for (size_type i = 0; i < other.m_size; ++i)
            {
                m_buffer[i] = other.m_buffer[(other.m_head + i) % other.m_buffer.size()];
            }
        }
        return *this;
    }

    /// Move assignment.
    RingQueue& operator=(RingQueue&& other) noexcept
    {
        if (this != &other)
        {
            m_buffer = std::move(other.m_buffer);
            m_head = other.m_head;
            m_tail = other.m_tail;
            m_size = other.m_size;
            other.m_head = 0;
            other.m_tail = 0;
            other.m_size = 0;
        }
        return *this;
    }

    /// Push an element to the back of the queue (copy).
    void push(const value_type& value)
    {
        ensure_capacity();
        m_buffer[m_tail] = value;
        m_tail = (m_tail + 1) % m_buffer.size();
        ++m_size;
    }

    /// Push an element to the back of the queue (move).
    void push(value_type&& value)
    {
        ensure_capacity();
        m_buffer[m_tail] = std::move(value);
        m_tail = (m_tail + 1) % m_buffer.size();
        ++m_size;
    }

    /// Construct an element in-place at the back of the queue.
    template<typename... Args>
    reference emplace(Args&&... args)
    {
        ensure_capacity();
        m_buffer[m_tail] = value_type(std::forward<Args>(args)...);
        size_type index = m_tail;
        m_tail = (m_tail + 1) % m_buffer.size();
        ++m_size;
        return m_buffer[index];
    }

    /// Remove the front element from the queue.
    void pop()
    {
        SLANG_RHI_ASSERT(m_size > 0);
        // For trivial types, we don't need to do anything special.
        // For non-trivial types, we could reset to default, but the
        // element will be overwritten on next push anyway.
        m_head = (m_head + 1) % m_buffer.size();
        --m_size;

        // Reset indices when empty to avoid unnecessary wraparound.
        if (m_size == 0)
        {
            m_head = 0;
            m_tail = 0;
        }
    }

    /// Access the front element.
    [[nodiscard]] reference front() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_buffer[m_head];
    }

    /// Access the front element (const).
    [[nodiscard]] const_reference front() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_buffer[m_head];
    }

    /// Access the back element.
    [[nodiscard]] reference back() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        size_type index = (m_tail + m_buffer.size() - 1) % m_buffer.size();
        return m_buffer[index];
    }

    /// Access the back element (const).
    [[nodiscard]] const_reference back() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        size_type index = (m_tail + m_buffer.size() - 1) % m_buffer.size();
        return m_buffer[index];
    }

    /// Check if the queue is empty.
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    /// Get the number of elements in the queue.
    [[nodiscard]] size_type size() const noexcept { return m_size; }

    /// Get the current capacity of the queue.
    [[nodiscard]] size_type capacity() const noexcept { return m_buffer.size(); }

    /// Clear all elements from the queue.
    void clear() noexcept
    {
        m_head = 0;
        m_tail = 0;
        m_size = 0;
    }

    /// Reserve capacity for at least the specified number of elements.
    /// If new_capacity is greater than the current capacity, the buffer
    /// is grown and existing elements are compacted to the beginning.
    void reserve(size_type new_capacity)
    {
        if (new_capacity > m_buffer.size())
        {
            grow(new_capacity);
        }
    }

    /**
     * \brief Iterator for traversing the queue in FIFO order.
     *
     * Note: This is a simple forward iterator for read-only access
     * and inspection.
     */
    class iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator(RingQueue* queue, size_type index)
            : m_queue(queue)
            , m_index(index)
        {
        }

        reference operator*() const
        {
            size_type actual_index = (m_queue->m_head + m_index) % m_queue->m_buffer.size();
            return m_queue->m_buffer[actual_index];
        }

        pointer operator->() const { return &(**this); }

        iterator& operator++()
        {
            ++m_index;
            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const { return m_index == other.m_index; }
        bool operator!=(const iterator& other) const { return m_index != other.m_index; }

    private:
        RingQueue* m_queue;
        size_type m_index;
    };

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator(const RingQueue* queue, size_type index)
            : m_queue(queue)
            , m_index(index)
        {
        }

        reference operator*() const
        {
            size_type actual_index = (m_queue->m_head + m_index) % m_queue->m_buffer.size();
            return m_queue->m_buffer[actual_index];
        }

        pointer operator->() const { return &(**this); }

        const_iterator& operator++()
        {
            ++m_index;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const { return m_index == other.m_index; }
        bool operator!=(const const_iterator& other) const { return m_index != other.m_index; }

    private:
        const RingQueue* m_queue;
        size_type m_index;
    };

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, m_size); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, m_size); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, m_size); }

private:
    /// Ensure there is capacity for at least one more element.
    void ensure_capacity()
    {
        if (m_size >= m_buffer.size())
        {
            grow(m_buffer.size() * 2);
        }
    }

    /// Grow the buffer to at least the specified capacity and compact elements.
    void grow(size_type new_capacity)
    {
        std::vector<value_type> new_buffer(new_capacity);

        // Compact elements to the beginning of the new buffer.
        for (size_type i = 0; i < m_size; ++i)
        {
            new_buffer[i] = std::move(m_buffer[(m_head + i) % m_buffer.size()]);
        }

        m_buffer = std::move(new_buffer);
        m_head = 0;
        m_tail = m_size;
    }

    std::vector<value_type> m_buffer;
    size_type m_head; ///< Index of the front element.
    size_type m_tail; ///< Index where the next element will be inserted.
    size_type m_size; ///< Number of elements in the queue.
};

} // namespace rhi
