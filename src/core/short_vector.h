#pragma once

#include "assert.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace rhi {

/**
 * \brief A vector that stores a small number of elements on the stack.
 *
 * Uses small buffer optimization (SBO) to avoid heap allocation for small sizes.
 * Falls back to heap allocation when capacity exceeds the inline buffer size.
 *
 * \tparam T Element type
 * \tparam N Size of the inline buffer (default 16)
 */
template<typename T, std::size_t N = 16>
class short_vector
{
public:
    static_assert(N > 0, "short_vector must have a size greater than zero.");

    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = value_type*;
    using const_iterator = const value_type*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /// Default constructor.
    short_vector() noexcept
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
    }

    /// Size constructor with default value.
    explicit short_vector(size_type count)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        resize(count);
    }

    /// Size constructor with fill value.
    short_vector(size_type count, const value_type& value)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        reserve(count);
        for (size_type i = 0; i < count; ++i)
            unchecked_push_back(value);
    }

    /// Initializer list constructor.
    short_vector(std::initializer_list<value_type> list)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        reserve(list.size());
        for (const auto& value : list)
            unchecked_push_back(value);
    }

    /// Iterator range constructor.
    template<typename InputIt, typename = std::enable_if_t<!std::is_integral<InputIt>::value>>
    short_vector(InputIt first, InputIt last)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        for (; first != last; ++first)
            push_back(*first);
    }

    /// Copy constructor.
    short_vector(const short_vector& other)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        reserve(other.m_size);
        copy_construct_range(other.m_data, other.m_data + other.m_size, m_data);
        m_size = other.m_size;
    }

    /// Move constructor.
    short_vector(short_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
        : m_size(0)
        , m_capacity(N)
    {
        m_data = inline_buffer();
        if (other.is_inline())
        {
            move_construct_range(other.m_data, other.m_data + other.m_size, m_data);
            m_size = other.m_size;
            other.destroy_all();
        }
        else
        {
            // Steal the heap allocation.
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = other.inline_buffer();
            other.m_size = 0;
            other.m_capacity = N;
        }
    }

    /// Destructor.
    ~short_vector()
    {
        destroy_range(m_data, m_data + m_size);
        free_heap();
    }

    /// Copy assignment.
    short_vector& operator=(const short_vector& other)
    {
        if (this != &other)
        {
            clear();
            reserve(other.m_size);
            copy_construct_range(other.m_data, other.m_data + other.m_size, m_data);
            m_size = other.m_size;
        }
        return *this;
    }

    /// Move assignment.
    short_vector& operator=(short_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
    {
        if (this != &other)
        {
            clear();
            free_heap();

            if (other.is_inline())
            {
                m_data = inline_buffer();
                m_capacity = N;
                move_construct_range(other.m_data, other.m_data + other.m_size, m_data);
                m_size = other.m_size;
                other.destroy_all();
            }
            else
            {
                // Steal the heap allocation.
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_data = other.inline_buffer();
                other.m_size = 0;
                other.m_capacity = N;
            }
        }
        return *this;
    }

    /// Initializer list assignment.
    short_vector& operator=(std::initializer_list<value_type> list)
    {
        assign(list);
        return *this;
    }

    [[nodiscard]] reference operator[](size_type index) noexcept
    {
        SLANG_RHI_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] const_reference operator[](size_type index) const noexcept
    {
        SLANG_RHI_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] reference front() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] const_reference front() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] reference back() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] const_reference back() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] iterator begin() noexcept { return m_data; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_data; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return m_data; }

    [[nodiscard]] iterator end() noexcept { return m_data + m_size; }
    [[nodiscard]] const_iterator end() const noexcept { return m_data + m_size; }
    [[nodiscard]] const_iterator cend() const noexcept { return m_data + m_size; }

    [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

    [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] pointer data() noexcept { return m_data; }
    [[nodiscard]] const_pointer data() const noexcept { return m_data; }

    [[nodiscard]] size_type size() const noexcept { return m_size; }
    [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }

    /// Returns true if using the inline buffer (no heap allocation).
    [[nodiscard]] bool is_inline() const noexcept { return m_data == inline_buffer(); }

    void clear() noexcept
    {
        destroy_range(m_data, m_data + m_size);
        m_size = 0;
    }

    void resize(size_type count)
    {
        if (count > m_capacity)
            grow(count);
        if (count > m_size)
            construct_range(m_data + m_size, m_data + count);
        else
            destroy_range(m_data + count, m_data + m_size);
        m_size = count;
    }

    void resize(size_type count, const value_type& value)
    {
        if (count > m_capacity)
            grow(count);
        if (count > m_size)
        {
            for (size_type i = m_size; i < count; ++i)
                construct_at(m_data + i, value);
        }
        else
        {
            destroy_range(m_data + count, m_data + m_size);
        }
        m_size = count;
    }

    void reserve(size_type new_capacity)
    {
        if (new_capacity > m_capacity)
            grow(new_capacity);
    }

    void push_back(const value_type& value)
    {
        if (m_size == m_capacity)
        {
            // Copy value before grow in case it references an element in this vector.
            value_type copy = value;
            grow(m_capacity * 2);
            construct_at(m_data + m_size, std::move(copy));
        }
        else
        {
            construct_at(m_data + m_size, value);
        }
        ++m_size;
    }

    void push_back(value_type&& value)
    {
        if (m_size == m_capacity)
        {
            // Move value before grow in case it references an element in this vector.
            value_type copy = std::move(value);
            grow(m_capacity * 2);
            construct_at(m_data + m_size, std::move(copy));
        }
        else
        {
            construct_at(m_data + m_size, std::move(value));
        }
        ++m_size;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args)
    {
        if (m_size == m_capacity)
        {
            // Construct temporary before grow in case args reference elements in this vector.
            value_type tmp(std::forward<Args>(args)...);
            grow(m_capacity * 2);
            construct_at(m_data + m_size, std::move(tmp));
        }
        else
        {
            construct_at(m_data + m_size, std::forward<Args>(args)...);
        }
        ++m_size;
        return m_data[m_size - 1];
    }

    void pop_back()
    {
        SLANG_RHI_ASSERT(m_size > 0);
        --m_size;
        destroy_at(m_data + m_size);
    }

    iterator erase(const_iterator pos)
    {
        SLANG_RHI_ASSERT(pos >= begin() && pos < end());
        iterator it = begin() + (pos - begin());
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memmove(it, it + 1, (end() - it - 1) * sizeof(T));
            --m_size;
        }
        else
        {
            for (iterator src = it + 1, dst = it; src != end(); ++src, ++dst)
                *dst = std::move(*src);
            pop_back();
        }
        return it;
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        SLANG_RHI_ASSERT(first >= begin() && first <= end());
        SLANG_RHI_ASSERT(last >= first && last <= end());
        iterator it_first = begin() + (first - begin());
        iterator it_last = begin() + (last - begin());
        if (it_first != it_last)
        {
            size_type num_erased = it_last - it_first;
            size_type num_tail = end() - it_last;
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(it_first, it_last, num_tail * sizeof(T));
            }
            else
            {
                iterator dst = it_first;
                for (iterator src = it_last; src != end(); ++src, ++dst)
                    *dst = std::move(*src);
                destroy_range(dst, m_data + m_size);
            }
            m_size -= num_erased;
        }
        return it_first;
    }

    iterator insert(const_iterator pos, const value_type& value)
    {
        SLANG_RHI_ASSERT(pos >= begin() && pos <= end());
        size_type index = pos - begin();

        // Copy value before any modifications in case it references an element in this vector.
        value_type copy = value;

        if (m_size == m_capacity)
            grow(m_capacity * 2);

        iterator it = begin() + index;
        if (it == end())
        {
            construct_at(m_data + m_size, std::move(copy));
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memmove(it + 1, it, (end() - it) * sizeof(T));
            *it = std::move(copy);
        }
        else
        {
            construct_at(m_data + m_size, std::move(back()));
            for (iterator dst = end() - 1, src = dst - 1; dst != it; --dst, --src)
                *dst = std::move(*src);
            *it = std::move(copy);
        }
        ++m_size;
        return it;
    }

    iterator insert(const_iterator pos, value_type&& value)
    {
        SLANG_RHI_ASSERT(pos >= begin() && pos <= end());
        size_type index = pos - begin();

        // Move value before any modifications in case it references an element in this vector.
        value_type copy = std::move(value);

        if (m_size == m_capacity)
            grow(m_capacity * 2);

        iterator it = begin() + index;
        if (it == end())
        {
            construct_at(m_data + m_size, std::move(copy));
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memmove(it + 1, it, (end() - it) * sizeof(T));
            *it = std::move(copy);
        }
        else
        {
            construct_at(m_data + m_size, std::move(back()));
            for (iterator dst = end() - 1, src = dst - 1; dst != it; --dst, --src)
                *dst = std::move(*src);
            *it = std::move(copy);
        }
        ++m_size;
        return it;
    }

    void assign(size_type count, const value_type& value)
    {
        clear();
        reserve(count);
        for (size_type i = 0; i < count; ++i)
            unchecked_push_back(value);
    }

    template<typename InputIt, typename = std::enable_if_t<!std::is_integral<InputIt>::value>>
    void assign(InputIt first, InputIt last)
    {
        clear();
        for (; first != last; ++first)
            push_back(*first);
    }

    void assign(std::initializer_list<value_type> list)
    {
        clear();
        reserve(list.size());
        for (const auto& value : list)
            unchecked_push_back(value);
    }

    void swap(short_vector& other) noexcept(
        std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value
    )
    {
        if (this == &other)
            return;

        // Both inline: swap elements directly.
        if (is_inline() && other.is_inline())
        {
            size_type common = (m_size < other.m_size) ? m_size : other.m_size;
            for (size_type i = 0; i < common; ++i)
            {
                std::swap(m_data[i], other.m_data[i]);
            }

            if (m_size < other.m_size)
            {
                for (size_type i = m_size; i < other.m_size; ++i)
                {
                    construct_at(m_data + i, std::move(other.m_data[i]));
                    destroy_at(other.m_data + i);
                }
            }
            else
            {
                for (size_type i = other.m_size; i < m_size; ++i)
                {
                    construct_at(other.m_data + i, std::move(m_data[i]));
                    destroy_at(m_data + i);
                }
            }
            std::swap(m_size, other.m_size);
        }
        // Both heap: just swap pointers.
        else if (!is_inline() && !other.is_inline())
        {
            std::swap(m_data, other.m_data);
            std::swap(m_size, other.m_size);
            std::swap(m_capacity, other.m_capacity);
        }
        // Mixed: move inline elements to heap side, transfer heap pointer.
        else
        {
            short_vector& inline_vec = is_inline() ? *this : other;
            short_vector& heap_vec = is_inline() ? other : *this;

            pointer heap_data = heap_vec.m_data;
            size_type heap_size = heap_vec.m_size;
            size_type heap_capacity = heap_vec.m_capacity;

            // Move inline elements to heap_vec's inline buffer.
            heap_vec.m_data = heap_vec.inline_buffer();
            heap_vec.m_size = inline_vec.m_size;
            heap_vec.m_capacity = N;
            move_construct_range(inline_vec.m_data, inline_vec.m_data + inline_vec.m_size, heap_vec.m_data);

            // Destroy inline elements and transfer heap pointer.
            destroy_range(inline_vec.m_data, inline_vec.m_data + inline_vec.m_size);
            inline_vec.m_data = heap_data;
            inline_vec.m_size = heap_size;
            inline_vec.m_capacity = heap_capacity;
        }
    }

    friend void swap(short_vector& a, short_vector& b) noexcept(
        std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value
    )
    {
        a.swap(b);
    }

    bool operator==(const short_vector& other) const
    {
        if (m_size != other.m_size)
            return false;
        for (size_type i = 0; i < m_size; ++i)
        {
            if (!(m_data[i] == other.m_data[i]))
                return false;
        }
        return true;
    }

    bool operator!=(const short_vector& other) const { return !(*this == other); }

private:
    pointer inline_buffer() noexcept { return std::launder(reinterpret_cast<pointer>(&m_inline_storage)); }

    const_pointer inline_buffer() const noexcept
    {
        return std::launder(reinterpret_cast<const_pointer>(&m_inline_storage));
    }

    void free_heap()
    {
        if (!is_inline())
            std::free(m_data);
    }

    void grow(size_type new_capacity)
    {
        if (new_capacity <= m_capacity)
            return;

        pointer new_data = static_cast<pointer>(std::malloc(new_capacity * sizeof(T)));
        SLANG_RHI_ASSERT(new_data != nullptr);

        move_construct_range(m_data, m_data + m_size, new_data);
        destroy_range(m_data, m_data + m_size);
        free_heap();

        m_data = new_data;
        m_capacity = new_capacity;
    }

    /// Push back without capacity check (caller must ensure capacity).
    void unchecked_push_back(const value_type& value)
    {
        construct_at(m_data + m_size, value);
        ++m_size;
    }

    // Helper functions for object lifetime management.

    template<typename... Args>
    static void construct_at(pointer p, Args&&... args)
    {
        ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
    }

    static void construct_range(pointer first, pointer last)
    {
        if constexpr (std::is_trivially_default_constructible_v<T>)
        {
            std::memset(first, 0, (last - first) * sizeof(T));
        }
        else
        {
            for (; first != last; ++first)
                ::new (static_cast<void*>(first)) T();
        }
    }

    static void destroy_at(pointer p)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
            p->~T();
    }

    static void destroy_range(pointer first, pointer last)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (; first != last; ++first)
                first->~T();
        }
    }

    void destroy_all()
    {
        destroy_range(m_data, m_data + m_size);
        m_size = 0;
    }

    static void copy_construct_range(const_pointer src_first, const_pointer src_last, pointer dest)
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dest, src_first, (src_last - src_first) * sizeof(T));
        }
        else
        {
            for (; src_first != src_last; ++src_first, ++dest)
                ::new (static_cast<void*>(dest)) T(*src_first);
        }
    }

    static void move_construct_range(pointer src_first, pointer src_last, pointer dest)
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dest, src_first, (src_last - src_first) * sizeof(T));
        }
        else
        {
            for (; src_first != src_last; ++src_first, ++dest)
                ::new (static_cast<void*>(dest)) T(std::move(*src_first));
        }
    }

    struct alignas(T) inline_storage_type
    {
        std::byte data[sizeof(T) * N];
    };

    pointer m_data;
    size_type m_size;
    size_type m_capacity;
    inline_storage_type m_inline_storage;
};

} // namespace rhi
