// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "sha1.h"

#include <slang.h>

#if SLANG_PROCESSOR_X86_64
#if SLANG_VC && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <immintrin.h>
#elif SLANG_PROCESSOR_ARM_64
#if SLANG_VC && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#if SLANG_WINDOWS_FAMILY
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif SLANG_LINUX_FAMILY
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#endif

namespace rhi {

namespace {

void sha1_process_block_software(const uint8_t* ptr, uint32_t state[5])
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

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

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

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

#if SLANG_PROCESSOR_X86_64
bool cpu_has_sha_ni()
{
#if SLANG_VC && !defined(__clang__)
    int info[4] = {0};
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 29)) != 0; // EBX bit 29 = SHA
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
    {
        return (ebx & (1 << 29)) != 0;
    }
    return false;
#endif
}

#if SLANG_VC && !defined(__clang__)
#define SLANG_RHI_SHA_TARGET
#else
#define SLANG_RHI_SHA_TARGET __attribute__((target("sha,sse4.1")))
#endif

SLANG_RHI_SHA_TARGET
void sha1_process_block_shani(const uint8_t* data, uint32_t state[5])
{
    __m128i abcd, e0, e1;
    __m128i msg0, msg1, msg2, msg3;

    // Load state: SHA1 state is [A, B, C, D, E]
    abcd = _mm_loadu_si128(reinterpret_cast<const __m128i*>(state));
    e0 = _mm_set_epi32(state[4], 0, 0, 0);

    // Byte-swap since SHA1 is big-endian.
    const __m128i shuf_mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    // Reverse the order of A,B,C,D for the SHA-NI instructions.
    abcd = _mm_shuffle_epi32(abcd, 0x1B);

    // Save original state for addition at end.
    __m128i abcd_save = abcd;
    __m128i e_save = e0;

    // Load and byte-swap message.
    msg0 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 0)), shuf_mask);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 16)), shuf_mask);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 32)), shuf_mask);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 48)), shuf_mask);

    // Rounds 0-3
    e0 = _mm_add_epi32(e0, msg0);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);

    // Rounds 4-7
    e1 = _mm_sha1nexte_epu32(e1, msg1);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
    msg0 = _mm_sha1msg1_epu32(msg0, msg1);

    // Rounds 8-11
    e0 = _mm_sha1nexte_epu32(e0, msg2);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
    msg1 = _mm_sha1msg1_epu32(msg1, msg2);
    msg0 = _mm_xor_si128(msg0, msg2);

    // Rounds 12-15
    e1 = _mm_sha1nexte_epu32(e1, msg3);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
    msg2 = _mm_sha1msg1_epu32(msg2, msg3);
    msg1 = _mm_xor_si128(msg1, msg3);
    msg0 = _mm_sha1msg2_epu32(msg0, msg3);

    // Rounds 16-19
    e0 = _mm_sha1nexte_epu32(e0, msg0);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
    msg3 = _mm_sha1msg1_epu32(msg3, msg0);
    msg2 = _mm_xor_si128(msg2, msg0);
    msg1 = _mm_sha1msg2_epu32(msg1, msg0);

    // Rounds 20-23
    e1 = _mm_sha1nexte_epu32(e1, msg1);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
    msg0 = _mm_sha1msg1_epu32(msg0, msg1);
    msg3 = _mm_xor_si128(msg3, msg1);
    msg2 = _mm_sha1msg2_epu32(msg2, msg1);

    // Rounds 24-27
    e0 = _mm_sha1nexte_epu32(e0, msg2);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
    msg1 = _mm_sha1msg1_epu32(msg1, msg2);
    msg0 = _mm_xor_si128(msg0, msg2);
    msg3 = _mm_sha1msg2_epu32(msg3, msg2);

    // Rounds 28-31
    e1 = _mm_sha1nexte_epu32(e1, msg3);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
    msg2 = _mm_sha1msg1_epu32(msg2, msg3);
    msg1 = _mm_xor_si128(msg1, msg3);
    msg0 = _mm_sha1msg2_epu32(msg0, msg3);

    // Rounds 32-35
    e0 = _mm_sha1nexte_epu32(e0, msg0);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
    msg3 = _mm_sha1msg1_epu32(msg3, msg0);
    msg2 = _mm_xor_si128(msg2, msg0);
    msg1 = _mm_sha1msg2_epu32(msg1, msg0);

    // Rounds 36-39
    e1 = _mm_sha1nexte_epu32(e1, msg1);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
    msg0 = _mm_sha1msg1_epu32(msg0, msg1);
    msg3 = _mm_xor_si128(msg3, msg1);
    msg2 = _mm_sha1msg2_epu32(msg2, msg1);

    // Rounds 40-43
    e0 = _mm_sha1nexte_epu32(e0, msg2);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
    msg1 = _mm_sha1msg1_epu32(msg1, msg2);
    msg0 = _mm_xor_si128(msg0, msg2);
    msg3 = _mm_sha1msg2_epu32(msg3, msg2);

    // Rounds 44-47
    e1 = _mm_sha1nexte_epu32(e1, msg3);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
    msg2 = _mm_sha1msg1_epu32(msg2, msg3);
    msg1 = _mm_xor_si128(msg1, msg3);
    msg0 = _mm_sha1msg2_epu32(msg0, msg3);

    // Rounds 48-51
    e0 = _mm_sha1nexte_epu32(e0, msg0);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
    msg3 = _mm_sha1msg1_epu32(msg3, msg0);
    msg2 = _mm_xor_si128(msg2, msg0);
    msg1 = _mm_sha1msg2_epu32(msg1, msg0);

    // Rounds 52-55
    e1 = _mm_sha1nexte_epu32(e1, msg1);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
    msg0 = _mm_sha1msg1_epu32(msg0, msg1);
    msg3 = _mm_xor_si128(msg3, msg1);
    msg2 = _mm_sha1msg2_epu32(msg2, msg1);

    // Rounds 56-59
    e0 = _mm_sha1nexte_epu32(e0, msg2);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
    msg1 = _mm_sha1msg1_epu32(msg1, msg2);
    msg0 = _mm_xor_si128(msg0, msg2);
    msg3 = _mm_sha1msg2_epu32(msg3, msg2);

    // Rounds 60-63
    e1 = _mm_sha1nexte_epu32(e1, msg3);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
    msg2 = _mm_sha1msg1_epu32(msg2, msg3);
    msg1 = _mm_xor_si128(msg1, msg3);
    msg0 = _mm_sha1msg2_epu32(msg0, msg3);

    // Rounds 64-67
    e0 = _mm_sha1nexte_epu32(e0, msg0);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
    msg3 = _mm_sha1msg1_epu32(msg3, msg0);
    msg2 = _mm_xor_si128(msg2, msg0);
    msg1 = _mm_sha1msg2_epu32(msg1, msg0);

    // Rounds 68-71
    e1 = _mm_sha1nexte_epu32(e1, msg1);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
    msg3 = _mm_xor_si128(msg3, msg1);
    msg2 = _mm_sha1msg2_epu32(msg2, msg1);

    // Rounds 72-75
    e0 = _mm_sha1nexte_epu32(e0, msg2);
    e1 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
    msg3 = _mm_sha1msg2_epu32(msg3, msg2);

    // Rounds 76-79
    e1 = _mm_sha1nexte_epu32(e1, msg3);
    e0 = abcd;
    abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

    // Add saved state.
    e0 = _mm_sha1nexte_epu32(e0, e_save);
    abcd = _mm_add_epi32(abcd, abcd_save);

    // Store state (reverse ABCD back to original order).
    abcd = _mm_shuffle_epi32(abcd, 0x1B);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state), abcd);
    state[4] = static_cast<uint32_t>(_mm_extract_epi32(e0, 3));
}

#undef SLANG_RHI_SHA_TARGET
#endif // SLANG_PROCESSOR_X86_64

#if SLANG_PROCESSOR_ARM_64
bool cpu_has_sha1_ce()
{
#if SLANG_APPLE_FAMILY
    // Apple Silicon always has SHA1 crypto extensions.
    return true;
#elif SLANG_LINUX_FAMILY
    return (getauxval(AT_HWCAP) & HWCAP_SHA1) != 0;
#elif SLANG_WINDOWS_FAMILY
    return IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
#else
    return false;
#endif
}

#if SLANG_VC && !defined(__clang__)
#define SLANG_RHI_SHA_CE_TARGET
#elif defined(__clang__)
#define SLANG_RHI_SHA_CE_TARGET __attribute__((target("crypto")))
#else
#define SLANG_RHI_SHA_CE_TARGET __attribute__((target("+crypto")))
#endif

SLANG_RHI_SHA_CE_TARGET
void sha1_process_block_arm_ce(const uint8_t* data, uint32_t state[5])
{
    // Load state into NEON registers.
    uint32x4_t abcd = vld1q_u32(state);
    uint32_t e0 = state[4];

    // Save for final addition.
    uint32x4_t abcd_save = abcd;
    uint32_t e0_save = e0;

    // Load message block (big-endian byte swap).
    uint32x4_t msg0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 0)));
    uint32x4_t msg1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16)));
    uint32x4_t msg2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 32)));
    uint32x4_t msg3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 48)));

    uint32x4_t tmp0, tmp1;

    // Rounds 0-3
    tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x5a827999));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1cq_u32(abcd, e0_save, tmp0);
    msg0 = vsha1su0q_u32(msg0, msg1, msg2);

    // Rounds 4-7
    tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x5a827999));
    uint32_t e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1cq_u32(abcd, e0, tmp1);
    msg0 = vsha1su1q_u32(msg0, msg3);
    msg1 = vsha1su0q_u32(msg1, msg2, msg3);

    // Rounds 8-11
    tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x5a827999));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1cq_u32(abcd, e1, tmp0);
    msg1 = vsha1su1q_u32(msg1, msg0);
    msg2 = vsha1su0q_u32(msg2, msg3, msg0);

    // Rounds 12-15
    tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x5a827999));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1cq_u32(abcd, e0, tmp1);
    msg2 = vsha1su1q_u32(msg2, msg1);
    msg3 = vsha1su0q_u32(msg3, msg0, msg1);

    // Rounds 16-19
    tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x5a827999));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1cq_u32(abcd, e1, tmp0);
    msg3 = vsha1su1q_u32(msg3, msg2);
    msg0 = vsha1su0q_u32(msg0, msg1, msg2);

    // Rounds 20-23
    tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x6ed9eba1));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);
    msg0 = vsha1su1q_u32(msg0, msg3);
    msg1 = vsha1su0q_u32(msg1, msg2, msg3);

    // Rounds 24-27
    tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x6ed9eba1));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e1, tmp0);
    msg1 = vsha1su1q_u32(msg1, msg0);
    msg2 = vsha1su0q_u32(msg2, msg3, msg0);

    // Rounds 28-31
    tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x6ed9eba1));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);
    msg2 = vsha1su1q_u32(msg2, msg1);
    msg3 = vsha1su0q_u32(msg3, msg0, msg1);

    // Rounds 32-35
    tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x6ed9eba1));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e1, tmp0);
    msg3 = vsha1su1q_u32(msg3, msg2);
    msg0 = vsha1su0q_u32(msg0, msg1, msg2);

    // Rounds 36-39
    tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x6ed9eba1));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);
    msg0 = vsha1su1q_u32(msg0, msg3);
    msg1 = vsha1su0q_u32(msg1, msg2, msg3);

    // Rounds 40-43
    tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x8f1bbcdc));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1mq_u32(abcd, e1, tmp0);
    msg1 = vsha1su1q_u32(msg1, msg0);
    msg2 = vsha1su0q_u32(msg2, msg3, msg0);

    // Rounds 44-47
    tmp1 = vaddq_u32(msg3, vdupq_n_u32(0x8f1bbcdc));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1mq_u32(abcd, e0, tmp1);
    msg2 = vsha1su1q_u32(msg2, msg1);
    msg3 = vsha1su0q_u32(msg3, msg0, msg1);

    // Rounds 48-51
    tmp0 = vaddq_u32(msg0, vdupq_n_u32(0x8f1bbcdc));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1mq_u32(abcd, e1, tmp0);
    msg3 = vsha1su1q_u32(msg3, msg2);
    msg0 = vsha1su0q_u32(msg0, msg1, msg2);

    // Rounds 52-55
    tmp1 = vaddq_u32(msg1, vdupq_n_u32(0x8f1bbcdc));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1mq_u32(abcd, e0, tmp1);
    msg0 = vsha1su1q_u32(msg0, msg3);
    msg1 = vsha1su0q_u32(msg1, msg2, msg3);

    // Rounds 56-59
    tmp0 = vaddq_u32(msg2, vdupq_n_u32(0x8f1bbcdc));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1mq_u32(abcd, e1, tmp0);
    msg1 = vsha1su1q_u32(msg1, msg0);
    msg2 = vsha1su0q_u32(msg2, msg3, msg0);

    // Rounds 60-63
    tmp1 = vaddq_u32(msg3, vdupq_n_u32(0xca62c1d6));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);
    msg2 = vsha1su1q_u32(msg2, msg1);
    msg3 = vsha1su0q_u32(msg3, msg0, msg1);

    // Rounds 64-67
    tmp0 = vaddq_u32(msg0, vdupq_n_u32(0xca62c1d6));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e1, tmp0);
    msg3 = vsha1su1q_u32(msg3, msg2);

    // Rounds 68-71
    tmp1 = vaddq_u32(msg1, vdupq_n_u32(0xca62c1d6));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);

    // Rounds 72-75
    tmp0 = vaddq_u32(msg2, vdupq_n_u32(0xca62c1d6));
    e0 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e1, tmp0);

    // Rounds 76-79
    tmp1 = vaddq_u32(msg3, vdupq_n_u32(0xca62c1d6));
    e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
    abcd = vsha1pq_u32(abcd, e0, tmp1);

    // Add saved state.
    e0 = e1 + e0_save;
    abcd = vaddq_u32(abcd, abcd_save);

    // Store state.
    vst1q_u32(state, abcd);
    state[4] = e0;
}

#undef SLANG_RHI_SHA_CE_TARGET
#endif // SLANG_PROCESSOR_ARM_64

using ProcessBlockFn = void (*)(const uint8_t*, uint32_t[5]);

ProcessBlockFn get_process_block_fn()
{
    static ProcessBlockFn fn = []() -> ProcessBlockFn
    {
#if SLANG_PROCESSOR_X86_64
        if (cpu_has_sha_ni())
            return sha1_process_block_shani;
#elif SLANG_PROCESSOR_ARM_64
        if (cpu_has_sha1_ce())
            return sha1_process_block_arm_ce;
#endif
        return sha1_process_block_software;
    }();
    return fn;
}

} // anonymous namespace

SHA1::SHA1()
    : m_processBlock(get_process_block_fn())
    , m_index(0)
    , m_bits(0)
{
    m_state[0] = 0x67452301;
    m_state[1] = 0xefcdab89;
    m_state[2] = 0x98badcfe;
    m_state[3] = 0x10325476;
    m_state[4] = 0xc3d2e1f0;
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
    auto add_byte = [this](uint8_t x)
    {
        m_buf[m_index++] = x;
        if (m_index >= sizeof(m_buf))
        {
            m_index = 0;
            processBlock(m_buf);
        }
    };

    // Finalize with 0x80, some zero padding and the length in bits.
    add_byte(0x80);
    while (m_index % 64 != 56)
    {
        add_byte(0);
    }
    for (int i = 7; i >= 0; --i)
    {
        add_byte(uint8_t(m_bits >> (i * 8)));
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

} // namespace rhi
