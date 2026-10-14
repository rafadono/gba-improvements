#pragma once
// Header-only adapter between gbaimp/ram_dispatch.h and gbarecomp's RAM-dispatch hook.
//
// Contract with gbarecomp (armv4t/runtime_arm.h), the only thing this file relies on:
//     extern "C" int (*g_runtime_ram_dispatch_hook)(uint32_t pc, int thumb);
// i.e. RuntimeRamDispatchHook, a global with C linkage, null by default. The runtime calls it for
// a PC in 0x02000000..0x03FFFFFF before its fixed dispatch and skips its own fallback when it
// returns non-zero. The declaration below is compatible with the one in runtime_arm.h, so the
// host may include that header before or after this one. The library never defines the variable;
// the host executable gets it from gbarecomp.
//
// Note: gbarecomp's run_game() clears the hook when it returns, so install it before run_game()
// (host_main.h does).
#include "gbaimp/ram_dispatch.h"

extern "C" int (*g_runtime_ram_dispatch_hook)(uint32_t pc, int thumb);

// Defines, in a generated host file:
//   extern "C" int <prefix>_ram_dispatch_hook(uint32_t, int)      (GBAIMP_RAM_DISPATCH_HOOK_N)
//   void <prefix>_install_ram_dispatch()                          (linkage as declared by the host header)
// `log_name` is the text between brackets in the one-line install message. `table` is a C array of
// RamDispatchEntry (or any row type with pc / thumb / fn) sorted by (pc, thumb); `count` is the
// number of valid rows. Installing twice is harmless: only the first call sets the hook and logs.
#define GBAIMP_DEFINE_RAM_DISPATCH_N(prefix, log_name, table, count)                             GBAIMP_RAM_DISPATCH_HOOK_N(prefix, table, count)                                             void prefix##_install_ram_dispatch() {                                                           static bool s_installed = false;                                                             if (!s_installed) {                                                                              g_runtime_ram_dispatch_hook = prefix##_ram_dispatch_hook;                                    s_installed = true;                                                                          ::gbaimp::log_ram_dispatch_installed(log_name, static_cast<std::size_t>(count));         }                                                                                        }
#define GBAIMP_DEFINE_RAM_DISPATCH(prefix, log_name, table)     GBAIMP_DEFINE_RAM_DISPATCH_N(prefix, log_name, table, sizeof(table) / sizeof((table)[0]))
