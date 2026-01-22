#pragma once

#include "assert.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waggressive-loop-optimizations"
#endif

namespace rhi {

/**
 * \brief A vector that stores up to a fixed number of elements.
 *
 * Uses uninitialized storage with proper object lifetime management,
 * supporting both trivial and non-trivial types correctly.
 *
 * \tparam T Element type
 * \tparam N Maximum size of the static vector
 */
template<typename T, std::size_t N>
class static_vector
{
public:
    static_assert(N > 0, "static_vector must have a size greater than zero.");

    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    /// Default constructor.
    static_vector() noexcept = default;

    /// Size constructor with default value.
    explicit static_vector(size_type count)
    {
        SLANG_RHI_ASSERT(count <= N);
        construct_range(0, count);
        m_size = count;
    }

    /// Size constructor with fill value.
    static_vector(size_type count, const value_type& value)
    {
        SLANG_RHI_ASSERT(count <= N);
        for (size_type i = 0; i < count; ++i)
            construct_at(i, value);
        m_size = count;
    }

    /// Initializer list constructor.
    static_vector(std::initializer_list<value_type> list)
    {
        SLANG_RHI_ASSERT(list.size() <= N);
        for (const auto& value : list)
        {
            construct_at(m_size, value);
            ++m_size;
        }
    }

    /// Iterator range constructor.
    template<typename InputIt, typename = std::enable_if_t<!std::is_integral<InputIt>::value>>
    static_vector(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
        {
            SLANG_RHI_ASSERT(m_size < N);
            construct_at(m_size, *first);
            ++m_size;
        }
    }

    /// Copy constructor.
    static_vector(const static_vector& other)
    {
        copy_construct_range(other.ptr_at(0), other.m_size, ptr_at(0));
        m_size = other.m_size;
    }

    /// Move constructor.
    static_vector(static_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
    {
        move_construct_range(other.ptr_at(0), other.m_size, ptr_at(0));
        m_size = other.m_size;
        other.destroy_all();
    }

    /// Destructor.
    ~static_vector() { clear(); }

    /// Copy assignment.
    static_vector& operator=(const static_vector& other)
    {
        if (this != &other)
        {
            clear();
            copy_construct_range(other.ptr_at(0), other.m_size, ptr_at(0));
            m_size = other.m_size;
        }
        return *this;
    }

    /// Move assignment.
    static_vector& operator=(static_vector&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
    {
        if (this != &other)
        {
            clear();
            move_construct_range(other.ptr_at(0), other.m_size, ptr_at(0));
            m_size = other.m_size;
            other.destroy_all();
        }
        return *this;
    }

    [[nodiscard]] reference operator[](size_type index) noexcept
    {
        SLANG_RHI_ASSERT(index < m_size);
        return *ptr_at(index);
    }

    [[nodiscard]] const_reference operator[](size_type index) const noexcept
    {
        SLANG_RHI_ASSERT(index < m_size);
        return *ptr_at(index);
    }

    [[nodiscard]] reference front() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return *ptr_at(0);
    }

    [[nodiscard]] const_reference front() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return *ptr_at(0);
    }

    [[nodiscard]] reference back() noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return *ptr_at(m_size - 1);
    }

    [[nodiscard]] const_reference back() const noexcept
    {
        SLANG_RHI_ASSERT(m_size > 0);
        return *ptr_at(m_size - 1);
    }

    [[nodiscard]] iterator begin() noexcept { return ptr_at(0); }
    [[nodiscard]] const_iterator begin() const noexcept { return ptr_at(0); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return ptr_at(0); }

    [[nodiscard]] iterator end() noexcept { return ptr_at(m_size); }
    [[nodiscard]] const_iterator end() const noexcept { return ptr_at(m_size); }
    [[nodiscard]] const_iterator cend() const noexcept { return ptr_at(m_size); }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] pointer data() noexcept { return ptr_at(0); }
    [[nodiscard]] const_pointer data() const noexcept { return ptr_at(0); }

    [[nodiscard]] size_type size() const noexcept { return m_size; }
    [[nodiscard]] constexpr size_type max_size() const noexcept { return N; }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return N; }

    void clear() noexcept
    {
        destroy_range(0, m_size);
        m_size = 0;
    }

    void resize(size_type count)
    {
        SLANG_RHI_ASSERT(count <= N);
        if (count < m_size)
        {
            destroy_range(count, m_size);
        }
        else if (count > m_size)
        {
            construct_range(m_size, count);
        }
        m_size = count;
    }

    void resize(size_type count, const value_type& value)
    {
        SLANG_RHI_ASSERT(count <= N);
        if (count < m_size)
        {
            destroy_range(count, m_size);
        }
        else if (count > m_size)
        {
            for (size_type i = m_size; i < count; ++i)
                construct_at(i, value);
        }
        m_size = count;
    }

    void push_back(const value_type& value)
    {
        SLANG_RHI_ASSERT(m_size < N);
        construct_at(m_size, value);
        ++m_size;
    }

    void push_back(value_type&& value)
    {
        SLANG_RHI_ASSERT(m_size < N);
        construct_at(m_size, std::move(value));
        ++m_size;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args)
    {
        SLANG_RHI_ASSERT(m_size < N);
        construct_at(m_size, std::forward<Args>(args)...);
        ++m_size;
        return *ptr_at(m_size - 1);
    }

    void pop_back()
    {
        SLANG_RHI_ASSERT(m_size > 0);
        --m_size;
        destroy_at(m_size);
    }

    iterator erase(const_iterator pos)
    {
        SLANG_RHI_ASSERT(pos >= begin() && pos < end());
        iterator it = begin() + (pos - cbegin());
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
        iterator it_first = begin() + (first - cbegin());
        iterator it_last = begin() + (last - cbegin());
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
                destroy_range(m_size - num_erased, m_size);
            }
            m_size -= num_erased;
        }
        return it_first;
    }

    iterator insert(const_iterator pos, const value_type& value)
    {
        SLANG_RHI_ASSERT(m_size < N);
        SLANG_RHI_ASSERT(pos >= begin() && pos <= end());
        iterator it = begin() + (pos - cbegin());
        if (it == end())
        {
            construct_at(m_size, value);
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            // Copy value first in case it references an element in this vector.
            value_type copy = value;
            std::memmove(it + 1, it, (end() - it) * sizeof(T));
            *it = copy;
        }
        else
        {
            construct_at(m_size, std::move(back()));
            for (iterator dst = end() - 1, src = dst - 1; dst != it; --dst, --src)
                *dst = std::move(*src);
            *it = value;
        }
        ++m_size;
        return it;
    }

    iterator insert(const_iterator pos, value_type&& value)
    {
        SLANG_RHI_ASSERT(m_size < N);
        SLANG_RHI_ASSERT(pos >= begin() && pos <= end());
        iterator it = begin() + (pos - cbegin());
        if (it == end())
        {
            construct_at(m_size, std::move(value));
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            // Copy value first in case it references an element in this vector.
            value_type copy = std::move(value);
            std::memmove(it + 1, it, (end() - it) * sizeof(T));
            *it = copy;
        }
        else
        {
            construct_at(m_size, std::move(back()));
            for (iterator dst = end() - 1, src = dst - 1; dst != it; --dst, --src)
                *dst = std::move(*src);
            *it = std::move(value);
        }
        ++m_size;
        return it;
    }

    void assign(size_type count, const value_type& value)
    {
        SLANG_RHI_ASSERT(count <= N);
        clear();
        for (size_type i = 0; i < count; ++i)
            construct_at(i, value);
        m_size = count;
    }

    template<typename InputIt, typename = std::enable_if_t<!std::is_integral<InputIt>::value>>
    void assign(InputIt first, InputIt last)
    {
        clear();
        for (; first != last; ++first)
        {
            SLANG_RHI_ASSERT(m_size < N);
            construct_at(m_size, *first);
            ++m_size;
        }
    }

    void assign(std::initializer_list<value_type> list)
    {
        SLANG_RHI_ASSERT(list.size() <= N);
        clear();
        for (const auto& value : list)
        {
            construct_at(m_size, value);
            ++m_size;
        }
    }

    void swap(static_vector& other) noexcept(
        std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value
    )
    {
        if (this == &other)
            return;

        // Swap common elements
        size_type common = (m_size < other.m_size) ? m_size : other.m_size;
        for (size_type i = 0; i < common; ++i)
        {
            value_type tmp = std::move((*this)[i]);
            (*this)[i] = std::move(other[i]);
            other[i] = std::move(tmp);
        }

        // Move extra elements from the larger to the smaller
        if (m_size < other.m_size)
        {
            for (size_type i = m_size; i < other.m_size; ++i)
            {
                construct_at(i, std::move(other[i]));
                other.destroy_at(i);
            }
        }
        else
        {
            for (size_type i = other.m_size; i < m_size; ++i)
            {
                other.construct_at(i, std::move((*this)[i]));
                destroy_at(i);
            }
        }

        size_type tmp_size = m_size;
        m_size = other.m_size;
        other.m_size = tmp_size;
    }

    friend void swap(static_vector& a, static_vector& b) noexcept(
        std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value
    )
    {
        a.swap(b);
    }

private:
    pointer ptr_at(size_type index) noexcept { return std::launder(reinterpret_cast<T*>(&m_storage[index])); }

    const_pointer ptr_at(size_type index) const noexcept
    {
        return std::launder(reinterpret_cast<const T*>(&m_storage[index]));
    }

    template<typename... Args>
    void construct_at(size_type index, Args&&... args)
    {
        ::new (static_cast<void*>(&m_storage[index])) T(std::forward<Args>(args)...);
    }

    void destroy_at(size_type index)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
            ptr_at(index)->~T();
    }

    void destroy_range(size_type first, size_type last)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_type i = first; i < last; ++i)
                ptr_at(i)->~T();
        }
    }

    /// Destroys all elements after they have been moved from.
    void destroy_all()
    {
        destroy_range(0, m_size);
        m_size = 0;
    }

    static void copy_construct_range(const_pointer src, size_type count, pointer dest)
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dest, src, count * sizeof(T));
        }
        else
        {
            for (size_type i = 0; i < count; ++i)
                ::new (static_cast<void*>(dest + i)) T(src[i]);
        }
    }

    static void move_construct_range(pointer src, size_type count, pointer dest)
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dest, src, count * sizeof(T));
        }
        else
        {
            for (size_type i = 0; i < count; ++i)
                ::new (static_cast<void*>(dest + i)) T(std::move(src[i]));
        }
    }

    void construct_range(size_type first, size_type last)
    {
        if constexpr (std::is_trivially_default_constructible_v<T>)
        {
            std::memset(ptr_at(first), 0, (last - first) * sizeof(T));
        }
        else
        {
            for (size_type i = first; i < last; ++i)
                ::new (static_cast<void*>(&m_storage[i])) T();
        }
    }

    struct alignas(T) storage_type
    {
        std::byte data[sizeof(T)];
    };

    storage_type m_storage[N];
    size_type m_size{0};
};

} // namespace rhi

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
