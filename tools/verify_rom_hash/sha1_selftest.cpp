// Known-answer tests for the bundled SHA-1.
#include <cstdio>
#include <string>

#include "sha1.h"

static int failures = 0;

static void check(const char* name, const std::string& got, const char* want) {
    const bool ok = got == want;
    std::printf("%-28s %s\n  got  %s\n  want %s\n", name, ok ? "OK" : "FAIL", got.c_str(), want);
    if (!ok) ++failures;
}

int main() {
    using gbaimp_tools::sha1_hex;
    check("empty", sha1_hex("", 0), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check("abc", sha1_hex("abc", 3), "a9993e364706816aba3e25717850c26c9cd0d89d");
    const std::string two_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes
    check("448-bit (2 blocks)", sha1_hex(two_block.data(), two_block.size()),
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    const std::string million(1000000, 'a');
    check("one million 'a'", sha1_hex(million.data(), million.size()),
          "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    // Same input fed in odd-sized pieces must give the same digest.
    gbaimp_tools::Sha1 s;
    for (std::size_t i = 0; i < million.size();) {
        std::size_t n = (i % 97) + 1;
        if (i + n > million.size()) n = million.size() - i;
        s.update(million.data() + i, n);
        i += n;
    }
    check("one million 'a' chunked", s.hex_digest(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    return failures ? 1 : 0;
}
