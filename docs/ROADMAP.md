# Roadmap

## Done
- [x] Library builds with CMake and SDL2; `GBAIMP_BUILD_TESTS` unit tests (one executable per module, run by `ctest`) and CI.
- [x] Game-agnostic HUD anchoring: `HudZone` list with `HudAnchoring::add_zone/clear_zones/zones`.
- [x] Generic gamepad API: `pad_a/pad_b/pad_l/pad_r`, `GamepadProfile::Triggers`, `[gamepad]` section in the keybinds ini.
- [x] Configurable HD audio output rate: `HdAudioSettings::target_sample_rate`.
- [x] Configurable thumbnail location: `SavestateThumbnailManager::set_base_directory`.
- [x] Python scripts parametrized by command-line arguments (see `tools/README.md`).

## Ideas for the existing modules
- [ ] `hd_audio_player`: formats beyond WAV, per-track loop points, streaming instead of whole-file decode.
- [ ] `savestate_thumbnails`: selectable image format.
- [ ] `input_enhancements`: more `PadButtonChoice` entries (stick clicks, back/guide), persistence of analog settings in the ini.

## Future work: widescreen core
A reusable widescreen layer is the most valuable missing piece. The architecture (tilemap, background X and sprite X providers, effective viewport computation, pillarboxing) is generic, but it has to be kept separate from per-game knowledge.
- [ ] Define a game-profile interface (for example: whether the current mode should be widened, which layers to extend, which HUD zones to register).
- [ ] Move the generic providers and viewport math into the library; keep each game's profile (addresses, mode detection) in the host project.
- [ ] Confirm which hooks are needed from the recompilation runtime and document them.

## Future work: option registry and UI overlay
- [ ] A declarative option registry (name, type, range, default) that each module fills in.
- [ ] A generic ini persistence layer and an overlay (for example ImGui-based) generated from the registry, so a host does not need to know every module's settings individually.

## Out of scope
Game-specific text, dialogue or font systems belong in the host project; only general techniques could be documented as guides.

## Pending decision
Choice of license (the README currently says "License: to be defined").
