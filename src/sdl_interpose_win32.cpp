// Windows implementation of the SDL interposers: Import Address Table patching.
// See include/gbaimp/sdl_interpose.h for the contract and limitations.
#include "gbaimp/sdl_interpose.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2  // EnumProcessModules -> K32EnumProcessModules (kernel32)
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace gbaimp {
namespace detail {
void set_real_forced(const SdlFunctions& real);
} // namespace detail

namespace {

constexpr int kFnCount = 6;
const char* const kFnNames[kFnCount] = {
    "SDL_PollEvent", "SDL_UpdateTexture", "SDL_RenderCopy",
    "SDL_RenderPresent", "SDL_QueueAudio", "SDL_OpenAudioDevice"};

struct PatchedSlot {
    void** slot;
    void* original;
    void* thunk;
    int fn;
};

std::mutex& mu() { static std::mutex m; return m; }
std::vector<PatchedSlot>& patched_slots() { static std::vector<PatchedSlot> v; return v; }

void* thunk_for(int fn) {
    switch (fn) {
        case 0: return reinterpret_cast<void*>(&detail::thunk_poll_event);
        case 1: return reinterpret_cast<void*>(&detail::thunk_update_texture);
        case 2: return reinterpret_cast<void*>(&detail::thunk_render_copy);
        case 3: return reinterpret_cast<void*>(&detail::thunk_render_present);
        case 4: return reinterpret_cast<void*>(&detail::thunk_queue_audio);
        default: return reinterpret_cast<void*>(&detail::thunk_open_audio_device);
    }
}

bool has_handler(const SdlFunctions& h, int fn) {
    switch (fn) {
        case 0: return h.poll_event != nullptr;
        case 1: return h.update_texture != nullptr;
        case 2: return h.render_copy != nullptr;
        case 3: return h.render_present != nullptr;
        case 4: return h.queue_audio != nullptr;
        default: return h.open_audio_device != nullptr;
    }
}

struct Candidate {
    void** slot;
    int fn;
    bool delay;
};
constexpr int kMaxCandidates = 32;

int match_name(const char* name) {
    for (int i = 0; i < kFnCount; ++i)
        if (std::strcmp(name, kFnNames[i]) == 0) return i;
    return -1;
}

// Walks the PE import and delay-import directories of `module` and records the
// IAT slots that import one of the interposed functions from `dll_name`.
// Plain-old-data only: this function uses SEH to survive a malformed image.
int collect_candidates(HMODULE module, const char* dll_name, Candidate* out, int max_out) {
    int n = 0;
    __try {
        BYTE* base = reinterpret_cast<BYTE*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        // Regular imports (also covers bound imports: after load the IAT holds
        // the resolved addresses and the name table is still in OriginalFirstThunk).
        const IMAGE_DATA_DIRECTORY& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imp.VirtualAddress && imp.Size) {
            auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imp.VirtualAddress);
            for (; d->Name; ++d) {
                if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll_name) != 0) continue;
                if (!d->OriginalFirstThunk || !d->FirstThunk) continue;
                auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
                auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++iat) {
                    if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                    auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    int fn = match_name(reinterpret_cast<const char*>(ibn->Name));
                    if (fn >= 0 && n < max_out) {
                        out[n].slot = reinterpret_cast<void**>(&iat->u1.Function);
                        out[n].fn = fn;
                        out[n].delay = false;
                        ++n;
                    }
                }
            }
        }

        // Delay-load imports (RVA-based descriptors only).
        const IMAGE_DATA_DIRECTORY& dly = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (dly.VirtualAddress && dly.Size) {
            auto* d = reinterpret_cast<IMAGE_DELAYLOAD_DESCRIPTOR*>(base + dly.VirtualAddress);
            for (; d->DllNameRVA; ++d) {
                if (!(d->Attributes.AllAttributes & 1)) continue;  // not RVA-based
                if (_stricmp(reinterpret_cast<const char*>(base + d->DllNameRVA), dll_name) != 0) continue;
                if (!d->ImportNameTableRVA || !d->ImportAddressTableRVA) continue;
                auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->ImportNameTableRVA);
                auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->ImportAddressTableRVA);
                for (; names->u1.AddressOfData; ++names, ++iat) {
                    if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                    auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    int fn = match_name(reinterpret_cast<const char*>(ibn->Name));
                    if (fn >= 0 && n < max_out) {
                        out[n].slot = reinterpret_cast<void**>(&iat->u1.Function);
                        out[n].fn = fn;
                        out[n].delay = true;
                        ++n;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

bool image_range(HMODULE module, BYTE** lo, BYTE** hi) {
    __try {
        BYTE* base = reinterpret_cast<BYTE*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        *lo = base;
        *hi = base + nt->OptionalHeader.SizeOfImage;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Atomic pointer write into a possibly read-only page.
bool write_slot(void** slot, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), value);
    DWORD ignore = 0;
    VirtualProtect(slot, sizeof(void*), old, &ignore);
    return true;
}

std::string module_basename(HMODULE m) {
    char path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(m, path, MAX_PATH);
    std::string s(path, len);
    size_t p = s.find_last_of("\\/");
    return p == std::string::npos ? s : s.substr(p + 1);
}

void log_line(const SdlInstallOptions* o, const std::string& s) {
    if (o && o->log) o->log(s.c_str());
}

} // namespace

namespace detail {
bool resolve_real_platform(SdlFunctions& out) {
    HMODULE sdl = GetModuleHandleA("SDL2.dll");
    if (!sdl) return false;
    auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(sdl, n)); };
    if (!out.poll_event) out.poll_event = reinterpret_cast<decltype(out.poll_event)>(sym("SDL_PollEvent"));
    if (!out.update_texture) out.update_texture = reinterpret_cast<decltype(out.update_texture)>(sym("SDL_UpdateTexture"));
    if (!out.render_copy) out.render_copy = reinterpret_cast<decltype(out.render_copy)>(sym("SDL_RenderCopy"));
    if (!out.render_present) out.render_present = reinterpret_cast<decltype(out.render_present)>(sym("SDL_RenderPresent"));
    if (!out.queue_audio) out.queue_audio = reinterpret_cast<decltype(out.queue_audio)>(sym("SDL_QueueAudio"));
    if (!out.open_audio_device) out.open_audio_device = reinterpret_cast<decltype(out.open_audio_device)>(sym("SDL_OpenAudioDevice"));
    return true;
}
} // namespace detail

bool install_sdl_interposers(const SdlHandlers& handlers, SdlInstallStatus* status,
                             const SdlInstallOptions* options) {
    SdlInstallStatus st;
    st.applicable = true;
    const char* dll_name = (options && options->dll_name) ? options->dll_name : "SDL2.dll";

    set_sdl_handlers(handlers);

    std::lock_guard<std::mutex> lock(mu());

    HMODULE sdl = GetModuleHandleA(dll_name);
    st.sdl_module_found = sdl != nullptr;
    if (!sdl) {
        st.error = std::string(dll_name) + " is not loaded in this process";
        log_line(options, "[gbaimp] sdl interposers: " + st.error);
        if (status) *status = st;
        return !patched_slots().empty();
    }

    // Original export addresses: what the "real" table holds and what a
    // pristine IAT slot contains.
    void* exports[kFnCount];
    SdlFunctions real;
    for (int i = 0; i < kFnCount; ++i) exports[i] = reinterpret_cast<void*>(GetProcAddress(sdl, kFnNames[i]));
    real.poll_event = reinterpret_cast<decltype(real.poll_event)>(exports[0]);
    real.update_texture = reinterpret_cast<decltype(real.update_texture)>(exports[1]);
    real.render_copy = reinterpret_cast<decltype(real.render_copy)>(exports[2]);
    real.render_present = reinterpret_cast<decltype(real.render_present)>(exports[3]);
    real.queue_audio = reinterpret_cast<decltype(real.queue_audio)>(exports[4]);
    real.open_audio_device = reinterpret_cast<decltype(real.open_audio_device)>(exports[5]);
    detail::set_real_forced(real);

    std::vector<HMODULE> mods(256);
    DWORD needed = 0;
    for (;;) {
        if (!EnumProcessModules(GetCurrentProcess(), mods.data(),
                                static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed)) {
            st.error = "EnumProcessModules failed";
            log_line(options, "[gbaimp] sdl interposers: " + st.error);
            if (status) *status = st;
            return !patched_slots().empty();
        }
        if (needed <= mods.size() * sizeof(HMODULE)) break;
        mods.resize(needed / sizeof(HMODULE) + 16);
    }
    mods.resize(needed / sizeof(HMODULE));

    for (HMODULE m : mods) {
        if (m == sdl) continue;  // SDL does not import itself through an IAT
        ++st.modules_scanned;
        Candidate cands[kMaxCandidates];
        int n = collect_candidates(m, dll_name, cands, kMaxCandidates);
        if (n == 0) continue;
        BYTE* lo = nullptr;
        BYTE* hi = nullptr;
        const bool have_range = image_range(m, &lo, &hi);
        std::string mname = module_basename(m);
        for (int i = 0; i < n; ++i) {
            const Candidate& c = cands[i];
            if (!has_handler(handlers, c.fn)) continue;
            void* thunk = thunk_for(c.fn);
            void* cur = *c.slot;
            if (cur == thunk) { ++st.slots_already_patched; continue; }
            // Delay-load slots that were never resolved point at a stub inside
            // the importing image; writing our thunk there is equivalent to the
            // helper having resolved the import (the real address is the export).
            const bool unresolved_stub = c.delay && have_range &&
                reinterpret_cast<BYTE*>(cur) >= lo && reinterpret_cast<BYTE*>(cur) < hi;
            if (cur != exports[c.fn] && !unresolved_stub) {
                ++st.slots_skipped_foreign;
                log_line(options, "[gbaimp] sdl interposers: " + mname + " " + kFnNames[c.fn] +
                                      " is not SDL's export (foreign hook), left alone");
                continue;
            }
            if (!write_slot(c.slot, thunk)) {
                ++st.slots_failed;
                log_line(options, "[gbaimp] sdl interposers: " + mname + " " + kFnNames[c.fn] +
                                      " VirtualProtect/write failed");
                continue;
            }
            patched_slots().push_back({c.slot, cur, thunk, c.fn});
            ++st.slots_patched;
            if (unresolved_stub) ++st.delay_load_unresolved;
            SdlPatchedImport pi;
            pi.module = mname;
            pi.function = kFnNames[c.fn];
            pi.delay_load = c.delay;
            st.patched.push_back(pi);
        }
    }

    if (options && options->log) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "[gbaimp] sdl interposers: %d module(s) scanned, %d slot(s) patched, %d already, %d foreign, %d failed",
                      st.modules_scanned, st.slots_patched, st.slots_already_patched,
                      st.slots_skipped_foreign, st.slots_failed);
        options->log(buf);
        for (const auto& p : st.patched)
            log_line(options, "[gbaimp]   patched " + p.module + " -> " + p.function +
                                  (p.delay_load ? " (delay-load)" : ""));
    }
    if (status) *status = st;
    return !patched_slots().empty();
}

int uninstall_sdl_interposers() {
    int restored = 0;
    {
        std::lock_guard<std::mutex> lock(mu());
        for (const PatchedSlot& p : patched_slots()) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(p.slot, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) continue;  // module unloaded
            if (*p.slot != p.thunk) continue;  // someone else changed it since
            if (write_slot(p.slot, p.original)) ++restored;
        }
        patched_slots().clear();
    }
    set_sdl_handlers(SdlHandlers{});
    return restored;
}

int sdl_interposers_patched_slots() {
    std::lock_guard<std::mutex> lock(mu());
    return static_cast<int>(patched_slots().size());
}

} // namespace gbaimp

#endif // _WIN32
