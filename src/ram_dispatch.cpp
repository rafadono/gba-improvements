#include "gbaimp/ram_dispatch.h"

#include <cstdio>

namespace gbaimp {

void log_ram_dispatch_installed(const char* log_name, std::size_t count) {
    std::printf("[%s] Native RAM overlay dispatcher installed (%zu functions registered)\n",
                log_name ? log_name : "gbaimp", count);
}

} // namespace gbaimp
