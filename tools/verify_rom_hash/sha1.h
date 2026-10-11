// Minimal self-contained SHA-1 (FIPS 180-4). Written for gba-improvements;
// no third-party code. Used only to identify files, not for security.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gbaimp_tools {

class Sha1 {
public:
    Sha1() { reset(); }

    void reset() {
        h_[0] = 0x67452301u; h_[1] = 0xEFCDAB89u; h_[2] = 0x98BADCFEu;
        h_[3] = 0x10325476u; h_[4] = 0xC3D2E1F0u;
        total_bytes_ = 0;
        buffered_ = 0;
    }

    void update(const void* data, std::size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        total_bytes_ += size;
        if (buffered_ > 0) {
            std::size_t take = 64 - buffered_;
            if (take > size) take = size;
            for (std::size_t i = 0; i < take; ++i) buf_[buffered_ + i] = p[i];
            buffered_ += take; p += take; size -= take;
            if (buffered_ == 64) { process_block(buf_); buffered_ = 0; }
        }
        while (size >= 64) { process_block(p); p += 64; size -= 64; }
        for (std::size_t i = 0; i < size; ++i) buf_[i] = p[i];
        buffered_ += size;
    }

    // Finalizes and returns the 40-character lowercase hex digest.
    std::string hex_digest() {
        const uint64_t bit_len = total_bytes_ * 8u;
        const uint8_t pad_first = 0x80;
        update(&pad_first, 1);
        const uint8_t zero = 0;
        while (buffered_ != 56) update(&zero, 1);
        uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
        update(len_be, 8);

        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(40);
        for (int i = 0; i < 5; ++i)
            for (int s = 28; s >= 0; s -= 4) out.push_back(digits[(h_[i] >> s) & 0xF]);
        return out;
    }

private:
    static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void process_block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
                   (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
    }

    uint32_t h_[5];
    uint64_t total_bytes_;
    uint8_t buf_[64];
    std::size_t buffered_;
};

inline std::string sha1_hex(const void* data, std::size_t size) {
    Sha1 s;
    s.update(data, size);
    return s.hex_digest();
}

} // namespace gbaimp_tools
