# Notices and provenance

This file records third-party material, algorithm references and open provenance questions for
`gba-improvements`. The project is released under the MIT License (see `LICENSE`).

## Open item: 5x7 bitmap font (`src/perf_hud.cpp`, `kFont5x7`)

The performance overlay draws text with a hand-entered 5x7 ASCII bitmap font (the table
`kFont5x7`, glyphs for ASCII 32..126, five column bytes per glyph). A review found that its data
matches a widely copied public 5x7 font whose exact origin could not be established. Because the
original author and terms cannot be proven, the font is treated as **of uncertain provenance**.

Recommendation: before any formal release, replace `kFont5x7` with an
original font drawn for this project, or with a font that is explicitly CC0 / public domain with a
documented source, and then remove this item. The table is private to `perf_hud.cpp` and only
indexed by character code, so replacing it does not change any public API.

## Algorithm references

- **Biquad filters (`src/audio_dsp.cpp`)**: the low-pass, peaking EQ, low-shelf and high-shelf
  coefficient formulas follow R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter
  coefficients" (the "Audio EQ Cookbook"). The formulas are published for general use; the code
  here is an independent implementation.
- **Pixel-art upscaler (`src/pixel_art_upscaler.cpp`)**: an original edge-aware implementation in
  the Scale2x / EPX family (replace a pixel's corner with a neighbor when the surrounding pixels
  agree on an edge). It is not xBRZ and contains no xBRZ code. The former name `xbrz_filter` is
  kept only as a deprecated compatibility header.
- **SDL interposition (`src/sdl_interpose*.cpp`)**: The Linux mechanism is the standard GNU ld `--wrap` option; the Windows mechanism is a plain Import Address Table rewrite using documented PE structures and the Win32 API (`VirtualProtect`). No third-party hooking library is used (the Windows file includes only `windows.h` and `psapi.h`).
- **SHA-1 (`tools/verify_rom_hash/sha1.h`)**: a self-contained implementation of the FIPS 180-4
  algorithm, checked against the standard test vectors by `verify_rom_hash_sha1_selftest`.

## Third-party software

- **SDL2** (https://libsdl.org) is the only external dependency. SDL2 is distributed under the zlib
  license. It is linked, not vendored: this repository does not contain SDL2 source or binaries.
  Hosts that ship SDL2 must follow its license terms.
- **stb_truetype** (`third_party/stb/stb_truetype.h`, v1.26, Sean Barrett, https://github.com/nothings/stb) is vendored unmodified and compiled into `src/text_fonts.cpp` (with `STBTT_STATIC`). It is available under either the MIT License or the Unlicense / public domain, at your choice; both notices are kept at the end of the header and must stay with any copy of it. The header itself warns that it does not range-check malformed fonts: the library therefore only feeds it embedded (trusted) fonts and user fonts that passed `validate_truetype()` (see `docs/MODULES.md`, "text_fonts.h").
- **Fonts**: the library contains **no font data** (the text engine tests build a tiny synthetic TrueType font in memory). A host that embeds fonts injects them with `FontLibrary::add_builtin_font()` and is responsible for shipping the licence text of each font it embeds (for example the SIL Open Font License 1.1 files that accompany OFL fonts) next to its binaries, and for the reserved-font-name rules of those licences.

## Related projects

- **`gbarecomp`**: hosts that link `gbarecomp` are bound by its license (PolyForm Noncommercial)
  for the combined work. This library contains no `gbarecomp` code and only documents the
  interface it expects (`docs/GBARECOMP_CONTRACT.md`). This library's MIT license is
  compatible with that arrangement.
