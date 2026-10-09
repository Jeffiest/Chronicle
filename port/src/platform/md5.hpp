#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace platform {

class Md5 {
public:
    Md5() {
        Reset();
    }

    void Reset() {
        m_state[0] = 0x67452301;
        m_state[1] = 0xefcdab89;
        m_state[2] = 0x98badcfe;
        m_state[3] = 0x10325476;
        m_count = 0;
        m_buffer.fill(0);
    }

    void Update(const void *data_ptr, size_t length) {
        const uint8_t *data = static_cast<const uint8_t *>(data_ptr);
        size_t index = static_cast<size_t>((m_count >> 3) & 0x3F);
        m_count += static_cast<uint64_t>(length) << 3;
        size_t part_len = 64 - index;

        size_t i = 0;
        if (length >= part_len) {
            std::memcpy(&m_buffer[index], data, part_len);
            Transform(m_buffer.data());
            for (i = part_len; i + 63 < length; i += 64) {
                Transform(&data[i]);
            }
            index = 0;
        }
        std::memcpy(&m_buffer[index], &data[i], length - i);
    }

    std::array<uint8_t, 16> FinalDigest() {
        std::array<uint8_t, 16> digest{};
        uint8_t count_bytes[8];
        for (int i = 0; i < 8; ++i) {
            count_bytes[i] = static_cast<uint8_t>((m_count >> (i * 8)) & 0xFF);
        }

        size_t index = static_cast<size_t>((m_count >> 3) & 0x3F);
        size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
        static const uint8_t padding[64] = {0x80};
        Update(padding, pad_len);
        Update(count_bytes, 8);

        for (int i = 0; i < 4; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(m_state[i] & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 8) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 16) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>((m_state[i] >> 24) & 0xFF);
        }
        return digest;
    }

    std::string FinalHex() {
        auto digest = FinalDigest();
        static const char hex_chars[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(32);
        for (uint8_t b : digest) {
            hex.push_back(hex_chars[(b >> 4) & 0xF]);
            hex.push_back(hex_chars[b & 0xF]);
        }
        return hex;
    }

private:
    static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static uint32_t G2(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

    static uint32_t RotateLeft(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    static void FF(uint32_t &a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
        a = RotateLeft(a + F(b, c, d) + x + ac, s) + b;
    }
    static void GG(uint32_t &a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
        a = RotateLeft(a + G2(b, c, d) + x + ac, s) + b;
    }
    static void HH(uint32_t &a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
        a = RotateLeft(a + H(b, c, d) + x + ac, s) + b;
    }
    static void II(uint32_t &a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
        a = RotateLeft(a + I(b, c, d) + x + ac, s) + b;
    }

    void Transform(const uint8_t block[64]) {
        uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
        uint32_t x[16];
        for (int i = 0; i < 16; ++i) {
            x[i] = static_cast<uint32_t>(block[i * 4 + 0]) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
        }

        // Round 1
        FF(a, b, c, d, x[0], 7, 0xd76aa478);
        FF(d, a, b, c, x[1], 12, 0xe8c7b756);
        FF(c, d, a, b, x[2], 17, 0x242070db);
        FF(b, c, d, a, x[3], 22, 0xc1bdceee);
        FF(a, b, c, d, x[4], 7, 0xf57c0faf);
        FF(d, a, b, c, x[5], 12, 0x4787c62a);
        FF(c, d, a, b, x[6], 17, 0xa8304613);
        FF(b, c, d, a, x[7], 22, 0xfd469501);
        FF(a, b, c, d, x[8], 7, 0x698098d8);
        FF(d, a, b, c, x[9], 12, 0x8b44f7af);
        FF(c, d, a, b, x[10], 17, 0xffff5bb1);
        FF(b, c, d, a, x[11], 22, 0x895cd7be);
        FF(a, b, c, d, x[12], 7, 0x6b901122);
        FF(d, a, b, c, x[13], 12, 0xfd987193);
        FF(c, d, a, b, x[14], 17, 0xa679438e);
        FF(b, c, d, a, x[15], 22, 0x49b40821);

        // Round 2
        GG(a, b, c, d, x[1], 5, 0xf61e2562);
        GG(d, a, b, c, x[6], 9, 0xc040b340);
        GG(c, d, a, b, x[11], 14, 0x265e5a51);
        GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
        GG(a, b, c, d, x[5], 5, 0xd62f105d);
        GG(d, a, b, c, x[10], 9, 0x02441453);
        GG(c, d, a, b, x[15], 14, 0xd8a1e681);
        GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
        GG(a, b, c, d, x[9], 5, 0x21e1cde6);
        GG(d, a, b, c, x[14], 9, 0xc33707d6);
        GG(c, d, a, b, x[3], 14, 0xf4d50d87);
        GG(b, c, d, a, x[8], 20, 0x455a14ed);
        GG(a, b, c, d, x[13], 5, 0xa9e3e905);
        GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
        GG(c, d, a, b, x[7], 14, 0x676f02d9);
        GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

        // Round 3
        HH(a, b, c, d, x[5], 4, 0xfffa3942);
        HH(d, a, b, c, x[8], 11, 0x8771f681);
        HH(c, d, a, b, x[11], 16, 0x6d9d6122);
        HH(b, c, d, a, x[14], 23, 0xfde5380c);
        HH(a, b, c, d, x[1], 4, 0xa4beea44);
        HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
        HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
        HH(b, c, d, a, x[10], 23, 0xbebfbc70);
        HH(a, b, c, d, x[13], 4, 0x289b7ec6);
        HH(d, a, b, c, x[0], 11, 0xeaa127fa);
        HH(c, d, a, b, x[3], 16, 0xd4ef3085);
        HH(b, c, d, a, x[6], 23, 0x04881d05);
        HH(a, b, c, d, x[9], 4, 0xd9d4d039);
        HH(d, a, b, c, x[12], 11, 0xe6db99e5);
        HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
        HH(b, c, d, a, x[2], 23, 0xc4ac5665);

        // Round 4
        II(a, b, c, d, x[0], 6, 0xf4292244);
        II(d, a, b, c, x[7], 10, 0x432aff97);
        II(c, d, a, b, x[14], 15, 0xab9423a7);
        II(b, c, d, a, x[5], 21, 0xfc93a039);
        II(a, b, c, d, x[12], 6, 0x655b59c3);
        II(d, a, b, c, x[3], 10, 0x8f0ccc92);
        II(c, d, a, b, x[10], 15, 0xffeff47d);
        II(b, c, d, a, x[1], 21, 0x85845dd1);
        II(a, b, c, d, x[8], 6, 0x6fa87e4f);
        II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
        II(c, d, a, b, x[6], 15, 0xa3014314);
        II(b, c, d, a, x[13], 21, 0x4e0811a1);
        II(a, b, c, d, x[4], 6, 0xf7537e82);
        II(d, a, b, c, x[11], 10, 0xbd3af235);
        II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
        II(b, c, d, a, x[9], 21, 0xeb86d391);

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
    }

    uint32_t              m_state[4];
    uint64_t              m_count;
    std::array<uint8_t, 64> m_buffer;
};

inline std::string ComputeTextureKey(uint32_t width, uint32_t height, const uint8_t *rgba) {
    Md5 md5;
    uint8_t header[8];
    header[0] = static_cast<uint8_t>(width & 0xFF);
    header[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>((width >> 16) & 0xFF);
    header[3] = static_cast<uint8_t>((width >> 24) & 0xFF);
    header[4] = static_cast<uint8_t>(height & 0xFF);
    header[5] = static_cast<uint8_t>((height >> 8) & 0xFF);
    header[6] = static_cast<uint8_t>((height >> 16) & 0xFF);
    header[7] = static_cast<uint8_t>((height >> 24) & 0xFF);
    md5.Update(header, 8);
    md5.Update(rgba, static_cast<size_t>(width) * height * 4);
    return md5.FinalHex();
}

} // namespace platform
