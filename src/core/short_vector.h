#pragma once

#include <initializer_list>
#include <type_traits>
#include <utility>
#include <cstdlib>

namespace rhi {

namespace detail {
template<typename T>
inline void construct_range(T* begin, T* end)
{
    if (!std::is_trivial_v<T>)
    {
        while (begin != end)
        {
            new (begin) T;
            begin++;
        }
    }
}

template<typename T>
inline void copy_range(T* begin, T* end, T* dest)
{
    if (std::is_trivial_v<T>)
    {
        std::memcpy(dest, begin, (end - begin) * sizeof(T));
    }
    else
    {
        while (begin != end)
        {
            new (dest) T(*begin);
            begin++;
            dest++;
        }
    }
}

template<typename T>
inline void move_range(T* begin, T* end, T* dest)
{
    while (begin != end)
    {
        *dest = std::move(*begin);
        begin++;
        dest++;
    }
}

template<typename T>
inline void destruct_range(T* begin, T* end)
{
    if (!std::is_trivial_v<T>)
    {
        while (begin != end)
        {
            begin->~T();
            begin++;
        }
    }
}
} // namespace detail

/**
 * \brief A vector that stores a small number of elements on the stack.
 *
 * \tparam T Element type
 * \tparam N Size of the short vector
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
    using iterator = value_type*;
    using const_iterator = const value_type*;

    /// Default constructor.
    short_vector() noexcept
        : m_data((value_type*)m_short_data)
        , m_size(0)
        , m_capacity(N)
    {
    }

    /// Size constructor.
    short_vector(size_type size, const value_type& value)
        : m_data((value_type*)m_short_data)
        , m_size(0)
        , m_capacity(N)
    {
        if (size > m_capacity)
            grow(size);
        for (size_type i = 0; i < size; ++i)
            push_back(value);
    }

    /// Initializer list constructor.
    short_vector(std::initializer_list<value_type> list)
        : m_data((value_type*)m_short_data)
        , m_size(0)
        , m_capacity(N)
    {
        for (const auto& value : list)
            push_back(value);
    }

    ~short_vector()
    {
        detail::destruct_range(m_data, m_data + m_size);
        if ((void*)m_data != (void*)m_short_data)
        {
            std::free(m_data);
        }
    }

    short_vector(const short_vector& other)
        : short_vector()
    {
        grow(other.m_size);
        detail::copy_range(other.m_data, other.m_data + other.m_size, m_data);
    }

#if 0
    short_vector(short_vector&& other) : short_vector()
    {
        if (other.m_data == other.m_short_data)
        {
            detail::move_range(other.m_data, other.m_data + other.m_size, m_data);
        }
        else
        {
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        m_size = other.m_size;
        other.m_size = 0;
        m_capacity = other.m_capacity;
        other.m_capacity = 0;
    }
#endif

    short_vector& operator=(const short_vector& other)
    {
        if (this == &other)
            return *this;
        resize(other.m_size);
        detail::copy_range(other.m_data, other.m_data + other.m_size, m_data);
        return *this;
    }

    short_vector& operator=(short_vector&& other)
    {
        if (this == &other)
            return *this;
        if ((void*)other.m_data == (void*)other.m_short_data)
        {
            detail::move_range(other.m_data, other.m_data + other.m_size, m_data);
        }
        else
        {
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        m_size = other.m_size;
        other.m_size = 0;
        m_capacity = other.m_capacity;
        other.m_capacity = 0;
        return *this;
    }

    reference operator[](size_type index) noexcept { return m_data[index]; }
    const_reference operator[](size_type index) const noexcept { return m_data[index]; }

    reference front() noexcept { return m_data[0]; }
    const_reference front() const noexcept { return m_data[0]; }

    reference back() noexcept { return m_data[m_size - 1]; }
    const_reference back() const noexcept { return m_data[m_size - 1]; }

    iterator begin() noexcept { return m_data; }
    const_iterator begin() const noexcept { return m_data; }

    iterator end() noexcept { return m_data + m_size; }
    const_iterator end() const noexcept { return m_data + m_size; }

    bool empty() const noexcept { return m_size == 0; }

    value_type* data() noexcept { return m_data; }
    const value_type* data() const noexcept { return m_data; }

    size_type size() const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void clear() noexcept
    {
        detail::destruct_range(m_data, m_data + m_size);
        m_size = 0;
    }

    void resize(size_t size)
    {
        if (size > m_capacity)
            grow(size);
        if (size > m_size)
            detail::construct_range(m_data + m_size, m_data + size);
        else
            detail::destruct_range(m_data + size, m_data + m_size);
        m_size = size;
    }

    void reserve(size_type new_capacity) { grow(new_capacity); }

    void push_back(const value_type& value)
    {
        if (m_size == m_capacity)
            grow(m_capacity * 2);
        if (std::is_trivial_v<value_type>)
            m_data[m_size] = value;
        else
            new (m_data + m_size) value_type(value);
        m_size++;
    }

    void push_back(value_type&& value)
    {
        if (m_size == m_capacity)
            grow(m_capacity * 2);
        if (std::is_trivial_v<value_type>)
            m_data[m_size] = value;
        else
            new (m_data + m_size) value_type(std::move(value));
        m_size++;
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        static_assert(!std::is_trivial_v<T>, "Use push_back() instead of emplace_back() with trivial types");

        if (m_size == m_capacity)
            grow(m_capacity * 2);
        new (m_data + m_size) T(std::forward<Args>(args)...);
        m_size++;
    }

    // void pop_back() { --m_size; }

    bool operator==(const short_vector& other) const
    {
        if (m_size != other.m_size)
            return false;
        for (size_type i = 0; i < m_size; ++i)
        {
            if (m_data[i] != other.m_data[i])
                return false;
        }
        return true;
    }

    bool operator!=(const short_vector& other) const { return !(*this == other); }

private:
    void grow(size_type new_capacity)
    {
        if (new_capacity <= m_capacity)
            return;
        m_capacity = new_capacity;
        value_type* new_data = reinterpret_cast<value_type*>(std::malloc(m_capacity * sizeof(T)));
        detail::move_range(m_data, m_data + m_size, new_data);
        if ((void*)m_data != (void*)m_short_data)
            std::free(m_data);
        m_data = new_data;
    }

    uint8_t m_short_data[sizeof(value_type) * N];
    value_type* m_data;
    size_type m_size;
    size_type m_capacity;
};

} // namespace rhi
