#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
#include "gba/sha1.h"

namespace {
// Expected hash is passed on the command line (argv[2]).
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: %s <path_to_rom.gba> <expected_sha1>\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];
    const char* EXPECTED_SHA1 = argv[2];
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "Error: Could not open ROM file: %s\n", path);
        return 1;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::fprintf(stderr, "Error: Failed reading ROM file: %s\n", path);
        return 1;
    }

    std::string calculated_hash = gba::sha1(buffer.data(), buffer.size()).hex();
    std::printf("ROM: %s\n", path);
    std::printf("Size: %zu bytes\n", buffer.size());
    std::printf("Calculated SHA-1: %s\n", calculated_hash.c_str());
    std::printf("Expected SHA-1:   %s\n", EXPECTED_SHA1);

    if (calculated_hash == std::string(EXPECTED_SHA1)) {
        std::printf("Result: Verification successful.\n");
        return 0;
    } else {
        std::fprintf(stderr, "Result: Hash verification failed.\n");
        return 2;
    }
}
