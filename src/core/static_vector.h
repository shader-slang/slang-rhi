#pragma once

#include <array>
#include <utility>

namespace rhi {

/**
 * \brief A vector that stores up to a fixed number of elements.
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
    using iterator = value_type*;
    using const_iterator = const value_type*;

    /// Default constructor.
    static_vector() = default;

    /// Size constructor.
    static_vector(size_type size, const value_type& value)
    {
        for (size_type i = 0; i < size; ++i)
            push_back(value);
    }

    /// Initializer list constructor.
    static_vector(std::initializer_list<value_type> list)
    {
        for (const auto& value : list)
            push_back(value);
    }

    ~static_vector() = default;

    static_vector(const static_vector& other) = default;
    static_vector(static_vector&& other) = default;

    static_vector& operator=(const static_vector& other) = default;
    static_vector& operator=(static_vector&& other) = default;

    reference operator[](size_type index) noexcept { return m_data[index]; }
    const_reference operator[](size_type index) const noexcept { return m_data[index]; }

    reference front() noexcept { return m_data[0]; }
    const_reference front() const noexcept { return m_data[0]; }

    reference back() noexcept { return m_data[m_size - 1]; }
    const_reference back() const noexcept { return m_data[m_size - 1]; }

    iterator begin() noexcept { return m_data.data(); }
    const_iterator begin() const noexcept { return m_data.data(); }

    iterator end() noexcept { return m_data.data() + m_size; }
    const_iterator end() const noexcept { return m_data.data() + m_size; }

    bool empty() const noexcept { return m_size == 0; }

    value_type* data() noexcept { return m_data.data(); }
    const value_type* data() const noexcept { return m_data.data(); }

    size_type size() const noexcept { return m_size; }
    size_type capacity() const noexcept { return N; }

    void clear() noexcept { m_size = 0; }

    void resize(size_t size) noexcept { m_size = size; }

    void push_back(const value_type& value)
    {
        SLANG_RHI_ASSERT(m_size < capacity());
        m_data[m_size++] = value;
    }

    void push_back(value_type&& value)
    {
        SLANG_RHI_ASSERT(m_size < capacity());
        m_data[m_size++] = std::move(value);
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        SLANG_RHI_ASSERT(m_size < capacity());
        m_data[m_size++] = value_type(std::forward<Args>(args)...);
    }

    void pop_back()
    {
        SLANG_RHI_ASSERT(m_size > 0);
        --m_size;
    }

private:
    std::array<T, N> m_data;
    size_type m_size{0};
};

} // namespace rhi
