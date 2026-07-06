#pragma once

#include <array>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rhi {

/// Helper to compute SHA-1 hash.
class SHA1
{
public:
    /// Message digest.
    using Digest = std::array<uint8_t, 20>;

    SHA1();

    SHA1(const void* data, size_t len)
        : SHA1()
    {
        update(data, len);
    }

    SHA1(std::string_view str)
        : SHA1()
    {
        update(str);
    }

    SHA1(const SHA1&) = default;
    SHA1(SHA1&&) = delete;

    SHA1& operator=(const SHA1&) = default;
    SHA1& operator=(SHA1&&) = delete;

    /**
     * Update hash by adding one byte.
     * \param byte Byte to hash.
     */
    SHA1& update(uint8_t byte) { return update(&byte, 1); }

    /**
     * Update hash by adding the given data.
     * \param data Data to hash.
     * \param len Length of data in bytes.
     */
    SHA1& update(const void* data, size_t len)
    {
        if (!data || len == 0)
            return *this;

        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
        m_bits += static_cast<uint64_t>(len) * 8;

        // If there's existing data in the buffer, try to fill it.
        if (m_index != 0 && m_index < sizeof(m_buf))
        {
            const uint32_t space = 64 - m_index;
            if (len < space)
            {
                std::memcpy(m_buf + m_index, ptr, len);
                m_index += static_cast<uint32_t>(len);
                return *this;
            }
            std::memcpy(m_buf + m_index, ptr, space);
            processBlock(m_buf);
            m_index = 0;
            ptr += space;
            len -= space;
        }

        // Process full 64-byte blocks directly from input.
        while (len >= 64)
        {
            processBlock(ptr);
            ptr += 64;
            len -= 64;
        }

        // Copy remaining bytes into buffer.
        if (len > 0)
        {
            std::memcpy(m_buf, ptr, len);
            m_index = static_cast<uint32_t>(len);
        }

        return *this;
    }

    /**
     * Update hash by adding the given string.
     * \param str String to hash.
     */
    SHA1& update(std::string_view str) { return update(str.data(), str.size()); }

    /// Return the message digest.
    Digest getDigest() const;

    /// Return the message digest as a hex string.
    std::string getHexDigest() const;

private:
    using ProcessBlockFn = void (*)(const uint8_t*, uint32_t[5]);

    void processBlock(const uint8_t* ptr) { m_processBlock(ptr, m_state); }
    Digest finalize();

    ProcessBlockFn m_processBlock;
    uint32_t m_index;
    uint64_t m_bits;
    uint32_t m_state[5];
    uint8_t m_buf[64];
};

} // namespace rhi
