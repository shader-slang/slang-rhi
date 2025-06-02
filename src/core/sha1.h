#pragma once

#include <array>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdlib>

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
    SHA1& update(uint8_t byte);

    /**
     * Update hash by adding the given data.
     * \param data Data to hash.
     * \param len Length of data in bytes.
     */
    SHA1& update(const void* data, size_t len);

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
    void addByte(uint8_t x);
    void processBlock(const uint8_t* ptr);
    Digest finalize();

    uint32_t m_index;
    uint64_t m_bits;
    uint32_t m_state[5];
    uint8_t m_buf[64];
};

} // namespace rhi
