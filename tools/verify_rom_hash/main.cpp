// verify_rom_hash <rom> <expected_sha1>
// Exit codes: 0 = match, 1 = mismatch, 2 = usage or I/O error.
#include <cctype>
#include <cstdio>
#include <string>

#include "sha1.h"

namespace {

std::string normalize_hex(const char* s) {
    std::string out;
    for (; *s; ++s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*s))));
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <file> <expected_sha1_hex>\n", argc > 0 ? argv[0] : "verify_rom_hash");
        return 2;
    }

    const char* path = argv[1];
    const std::string expected = normalize_hex(argv[2]);
    if (expected.size() != 40 || expected.find_first_not_of("0123456789abcdef") != std::string::npos) {
        std::fprintf(stderr, "Error: expected hash must be 40 hexadecimal characters.\n");
        return 2;
    }

    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "Error: could not open file: %s\n", path);
        return 2;
    }

    gbaimp_tools::Sha1 sha;
    unsigned char chunk[64 * 1024];
    unsigned long long total = 0;
    std::size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        sha.update(chunk, n);
        total += n;
    }
    const bool io_error = std::ferror(f) != 0;
    std::fclose(f);
    if (io_error) {
        std::fprintf(stderr, "Error: failed while reading file: %s\n", path);
        return 2;
    }

    const std::string actual = sha.hex_digest();
    std::printf("File: %s\n", path);
    std::printf("Size: %llu bytes\n", total);
    std::printf("Calculated SHA-1: %s\n", actual.c_str());
    std::printf("Expected SHA-1:   %s\n", expected.c_str());
    if (actual == expected) {
        std::printf("Result: match.\n");
        return 0;
    }
    std::fprintf(stderr, "Result: MISMATCH.\n");
    return 1;
}
