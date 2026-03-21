# DeMoDOOM Roadmap

## v0.2.0 — Initial Release
- [x] Software framebuffer renderer (320×200 → 960×540)
- [x] CRT post-processing (scanlines, bloom, vignette, barrel distortion)
- [x] PipeWire native audio engine
- [x] Faust DSP bridge (JIT, DSO, runtime compile)
- [x] Chiptune synth engine (4 voices, randomized progressions)
- [x] Input abstraction (keyboard, gamepad, BCI)
- [x] 6 UI screens (menu, FX chain, params, viz, settings, help)
- [x] Multi-resolution runtime switching
- [x] Nix flake build system

## v0.3.0 — Full Chain
- [x] Denormal protection — FTZ/DAZ via MXCSR on audio thread
- [x] Stereo scope capture — L/R channels for Lissajous and Phase modes
- [x] Gamepad hotplug — SDL auto-detect connect/disconnect at runtime
- [x] FX chain wiring — 12-slot serial DSP with per-slot load/bypass/wet
- [x] Lock-free slot state — atomic loaded/bypassed/wet_mix, no audio-thread locks
- [x] MIDI input — RtMidi device, CC/notes/pitch bend → Actions
- [x] Preset system — JSON, key=value text, binary blob, reverse trie DB
- [x] Chiptune audio quality — no input stream by default

## v0.4.0 — Polish
- [ ] On-demand input stream — create PipeWire capture only when a DSP with inputs is loaded
- [ ] MIDI learn — hold key combo, wiggle knob, auto-create binding
- [ ] Key rebinding UI — replace "Coming soon" in Settings > INPUT
- [ ] FFT for spectrum — replace O(n²) DFT with real FFT (kissfft or custom)
- [ ] Gamepad → slot param mapping — analog sticks control FX parameters
- [ ] Slot param save in presets — snapshot per-slot parameter values from loaded DSP

## v1.0.0 — Production
- [ ] VST3 plugin export — embed DOOM GUI as a plugin window in a DAW
- [ ] OSC remote control — receive/send parameter changes over network
- [ ] Multi-engine — load and switch between multiple .dsp files
- [ ] Per-slot CPU meter — show processing time per FX slot in debug overlay
- [ ] Full documentation — man page, tutorial, example .dsp files
- [ ] Automated tests — unit tests for FX chain, presets, chiptune RNG

## Backlog
- [ ] DOOM-themed preset files (.wad extension)
- [ ] Scrolling spectrogram mode in Viz screen
- [ ] Lua scripting for custom UI widgets
- [ ] Network sync — multiple DeMoDOOM instances sharing state
- [ ] Custom input device template — scaffold command for new InputDevice
- [ ] PipeWire native MIDI — avoid JACK dependency
- [ ] WASM build — run in browser with WebAudio
