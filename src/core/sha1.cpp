#include "sha1.h"

namespace rhi {

SHA1::SHA1()
    : m_index(0)
    , m_bits(0)
{
    m_state[0] = 0x67452301;
    m_state[1] = 0xefcdab89;
    m_state[2] = 0x98badcfe;
    m_state[3] = 0x10325476;
    m_state[4] = 0xc3d2e1f0;
}

SHA1& SHA1::update(uint8_t byte)
{
    addByte(byte);
    m_bits += 8;
    return *this;
}

SHA1& SHA1::update(const void* data, size_t len)
{
    if (!data || len == 0)
        return *this;

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);

    // Fill up buffer if not full.
    while (len > 0 && m_index != 0)
    {
        update(*ptr++);
        len--;
    }

    // Process full blocks.
    while (len >= sizeof(m_buf))
    {
        processBlock(ptr);
        ptr += sizeof(m_buf);
        len -= sizeof(m_buf);
        m_bits += sizeof(m_buf) * 8;
    }

    // Process remaining bytes.
    while (len > 0)
    {
        update(*ptr++);
        len--;
    }

    return *this;
}

SHA1::Digest SHA1::getDigest() const
{
    SHA1 copy{*this};
    return copy.finalize();
}

std::string SHA1::getHexDigest() const
{
    static const char* hex_digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(40);
    for (auto b : getDigest())
    {
        hex.push_back(hex_digits[b >> 4]);
        hex.push_back(hex_digits[b & 0xf]);
    }
    return hex;
}

SHA1::Digest SHA1::finalize()
{
    // Finalize with 0x80, some zero padding and the length in bits.
    addByte(0x80);
    while (m_index % 64 != 56)
    {
        addByte(0);
    }
    for (int i = 7; i >= 0; --i)
    {
        addByte(uint8_t(m_bits >> (i * 8)));
    }

    Digest digest;
    for (int i = 0; i < 5; i++)
    {
        for (int j = 3; j >= 0; j--)
        {
            digest[i * 4 + j] = (m_state[i] >> ((3 - j) * 8)) & 0xff;
        }
    }

    return digest;
}

void SHA1::addByte(uint8_t byte)
{
    m_buf[m_index++] = byte;

    if (m_index >= sizeof(m_buf))
    {
        m_index = 0;
        processBlock(m_buf);
    }
}

void SHA1::processBlock(const uint8_t* ptr)
{
    auto rol32 = [](uint32_t x, uint32_t n)
    {
        return (x << n) | (x >> (32 - n));
    };

    auto make_word = [](const uint8_t* p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };

    const uint32_t c0 = 0x5a827999;
    const uint32_t c1 = 0x6ed9eba1;
    const uint32_t c2 = 0x8f1bbcdc;
    const uint32_t c3 = 0xca62c1d6;

    uint32_t a = m_state[0];
    uint32_t b = m_state[1];
    uint32_t c = m_state[2];
    uint32_t d = m_state[3];
    uint32_t e = m_state[4];

    uint32_t w[16];

    for (size_t i = 0; i < 16; i++)
    {
        w[i] = make_word(ptr + i * 4);
    }

    // clang-format off
#define SHA1_LOAD(i) w[i&15] = rol32(w[(i + 13) & 15] ^ w[(i + 8) & 15] ^ w[(i + 2) & 15] ^ w[i & 15], 1);
#define SHA1_ROUND_0(v,u,x,y,z,i)              z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + rol32(v, 5); u = rol32(u, 30);
#define SHA1_ROUND_1(v,u,x,y,z,i) SHA1_LOAD(i) z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + rol32(v, 5); u = rol32(u, 30);
#define SHA1_ROUND_2(v,u,x,y,z,i) SHA1_LOAD(i) z += (u ^ x ^ y) + w[i & 15] + c1 + rol32(v, 5); u = rol32(u, 30);
#define SHA1_ROUND_3(v,u,x,y,z,i) SHA1_LOAD(i) z += (((u | x) & y) | (u & x)) + w[i & 15] + c2 + rol32(v, 5); u = rol32(u, 30);
#define SHA1_ROUND_4(v,u,x,y,z,i) SHA1_LOAD(i) z += (u ^ x ^ y) + w[i & 15] + c3 + rol32(v, 5); u = rol32(u, 30);
    // clang-format on

    SHA1_ROUND_0(a, b, c, d, e, 0);
    SHA1_ROUND_0(e, a, b, c, d, 1);
    SHA1_ROUND_0(d, e, a, b, c, 2);
    SHA1_ROUND_0(c, d, e, a, b, 3);
    SHA1_ROUND_0(b, c, d, e, a, 4);
    SHA1_ROUND_0(a, b, c, d, e, 5);
    SHA1_ROUND_0(e, a, b, c, d, 6);
    SHA1_ROUND_0(d, e, a, b, c, 7);
    SHA1_ROUND_0(c, d, e, a, b, 8);
    SHA1_ROUND_0(b, c, d, e, a, 9);
    SHA1_ROUND_0(a, b, c, d, e, 10);
    SHA1_ROUND_0(e, a, b, c, d, 11);
    SHA1_ROUND_0(d, e, a, b, c, 12);
    SHA1_ROUND_0(c, d, e, a, b, 13);
    SHA1_ROUND_0(b, c, d, e, a, 14);
    SHA1_ROUND_0(a, b, c, d, e, 15);
    SHA1_ROUND_1(e, a, b, c, d, 16);
    SHA1_ROUND_1(d, e, a, b, c, 17);
    SHA1_ROUND_1(c, d, e, a, b, 18);
    SHA1_ROUND_1(b, c, d, e, a, 19);
    SHA1_ROUND_2(a, b, c, d, e, 20);
    SHA1_ROUND_2(e, a, b, c, d, 21);
    SHA1_ROUND_2(d, e, a, b, c, 22);
    SHA1_ROUND_2(c, d, e, a, b, 23);
    SHA1_ROUND_2(b, c, d, e, a, 24);
    SHA1_ROUND_2(a, b, c, d, e, 25);
    SHA1_ROUND_2(e, a, b, c, d, 26);
    SHA1_ROUND_2(d, e, a, b, c, 27);
    SHA1_ROUND_2(c, d, e, a, b, 28);
    SHA1_ROUND_2(b, c, d, e, a, 29);
    SHA1_ROUND_2(a, b, c, d, e, 30);
    SHA1_ROUND_2(e, a, b, c, d, 31);
    SHA1_ROUND_2(d, e, a, b, c, 32);
    SHA1_ROUND_2(c, d, e, a, b, 33);
    SHA1_ROUND_2(b, c, d, e, a, 34);
    SHA1_ROUND_2(a, b, c, d, e, 35);
    SHA1_ROUND_2(e, a, b, c, d, 36);
    SHA1_ROUND_2(d, e, a, b, c, 37);
    SHA1_ROUND_2(c, d, e, a, b, 38);
    SHA1_ROUND_2(b, c, d, e, a, 39);
    SHA1_ROUND_3(a, b, c, d, e, 40);
    SHA1_ROUND_3(e, a, b, c, d, 41);
    SHA1_ROUND_3(d, e, a, b, c, 42);
    SHA1_ROUND_3(c, d, e, a, b, 43);
    SHA1_ROUND_3(b, c, d, e, a, 44);
    SHA1_ROUND_3(a, b, c, d, e, 45);
    SHA1_ROUND_3(e, a, b, c, d, 46);
    SHA1_ROUND_3(d, e, a, b, c, 47);
    SHA1_ROUND_3(c, d, e, a, b, 48);
    SHA1_ROUND_3(b, c, d, e, a, 49);
    SHA1_ROUND_3(a, b, c, d, e, 50);
    SHA1_ROUND_3(e, a, b, c, d, 51);
    SHA1_ROUND_3(d, e, a, b, c, 52);
    SHA1_ROUND_3(c, d, e, a, b, 53);
    SHA1_ROUND_3(b, c, d, e, a, 54);
    SHA1_ROUND_3(a, b, c, d, e, 55);
    SHA1_ROUND_3(e, a, b, c, d, 56);
    SHA1_ROUND_3(d, e, a, b, c, 57);
    SHA1_ROUND_3(c, d, e, a, b, 58);
    SHA1_ROUND_3(b, c, d, e, a, 59);
    SHA1_ROUND_4(a, b, c, d, e, 60);
    SHA1_ROUND_4(e, a, b, c, d, 61);
    SHA1_ROUND_4(d, e, a, b, c, 62);
    SHA1_ROUND_4(c, d, e, a, b, 63);
    SHA1_ROUND_4(b, c, d, e, a, 64);
    SHA1_ROUND_4(a, b, c, d, e, 65);
    SHA1_ROUND_4(e, a, b, c, d, 66);
    SHA1_ROUND_4(d, e, a, b, c, 67);
    SHA1_ROUND_4(c, d, e, a, b, 68);
    SHA1_ROUND_4(b, c, d, e, a, 69);
    SHA1_ROUND_4(a, b, c, d, e, 70);
    SHA1_ROUND_4(e, a, b, c, d, 71);
    SHA1_ROUND_4(d, e, a, b, c, 72);
    SHA1_ROUND_4(c, d, e, a, b, 73);
    SHA1_ROUND_4(b, c, d, e, a, 74);
    SHA1_ROUND_4(a, b, c, d, e, 75);
    SHA1_ROUND_4(e, a, b, c, d, 76);
    SHA1_ROUND_4(d, e, a, b, c, 77);
    SHA1_ROUND_4(c, d, e, a, b, 78);
    SHA1_ROUND_4(b, c, d, e, a, 79);

#undef SHA1_LOAD
#undef SHA1_ROUND_0
#undef SHA1_ROUND_1
#undef SHA1_ROUND_2
#undef SHA1_ROUND_3
#undef SHA1_ROUND_4

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

} // namespace rhi
