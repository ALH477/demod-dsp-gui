// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Engine Implementation                                  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/engine.hpp"
#include "input/keyboard_device.hpp"
#include "input/gamepad_device.hpp"
#include "input/bci_device.hpp"
#include "input/midi_device.hpp"
#include "renderer/font.hpp"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>

namespace demod {

Engine::Engine() = default;

Engine::~Engine() {
    audio_.stop();
    renderer_.shutdown();
    SDL_Quit();
}

bool Engine::init(const EngineConfig& config) {
    // ── SDL ──────────────────────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_HAPTIC) != 0) {
        fprintf(stderr, "[ENGINE] SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    // ── Renderer ─────────────────────────────────────────────────────
    if (!renderer_.init("DeMoDOOM")) return false;
    renderer_.set_resolution(config.resolution);
    if (config.fullscreen) renderer_.toggle_fullscreen();

    // ── Input ────────────────────────────────────────────────────────
    input_.add_device(std::make_unique<input::KeyboardDevice>());

    int n_joy = SDL_NumJoysticks();
    fprintf(stderr, "[ENGINE] %d joystick(s) detected\n", n_joy);
    for (int i = 0; i < n_joy; ++i) {
        if (SDL_IsGameController(i)) {
            auto gp = std::make_unique<input::GamepadDevice>(i);
            fprintf(stderr, "[ENGINE] Gamepad %d: %s\n", i, gp->name().c_str());
            gp->set_led(0, 255, 212);
            input_.add_device(std::move(gp));
        }
    }

    if (config.enable_bci) {
        input_.add_device(std::make_unique<input::BCIDevice>());
    }

    // ── MIDI ─────────────────────────────────────────────────────────
    {
        int n_midi = input::MidiDevice::port_count();
        if (n_midi > 0) {
            fprintf(stderr, "[ENGINE] %d MIDI input port(s) detected\n", n_midi);
            // Open first available MIDI port
            auto midi = std::make_unique<input::MidiDevice>(0);
            if (midi->open()) {
                input_.add_device(std::move(midi));
            }
        }
    }

    input_.load_default_bindings();

    // ── Faust DSP (load into FX slot 0) ──────────────────────────────
    fx_processor_.set_sample_rate(config.sample_rate);
    if (!config.dsp_path.empty()) {
        if (!fx_processor_.load_slot(0, config.dsp_path))
            fprintf(stderr, "[ENGINE] DSP load failed: %s — demo mode\n",
                    config.dsp_path.c_str());
        else
            audio_.activate_input();
    }

    // ── Chiptune ──────────────────────────────────────────────────────
    chiptune_.init(config.sample_rate);
    chiptune_.set_playing(true);  // Play on startup (intro)

    // ── Audio ────────────────────────────────────────────────────────
    audio_.set_sample_rate(config.sample_rate);
    audio_.set_block_size(config.block_size);
    audio_.set_channels(2, std::max(2, 2));

    audio_.set_callback([this](const float* const* in, float* const* out,
                                int n_ch, int n_frames) {
        float* buf = out[0];

        // Process FX chain (serial chain of per-slot DSPs)
        if (in) {
            fx_processor_.process_serial(in, buf, n_ch, n_frames);
        } else {
            // Silence base if no input and no DSP loaded
            std::memset(buf, 0, size_t(n_frames) * size_t(n_ch) * sizeof(float));
            fx_processor_.process_serial(buf, n_ch, n_frames);
        }

        // Mix chiptune on top (additive, handles its own playing_ check)
        if (n_ch >= 1) {
            chiptune_.process(buf, n_ch, n_frames);
        }
    });

    if (!audio_.start("demodoom"))
        fprintf(stderr, "[ENGINE] Audio start failed — continuing muted\n");

    // ── Screens ──────────────────────────────────────────────────────
    auto fx_chain = std::make_unique<ui::FXChainScreen>();
    fx_chain_screen_ = fx_chain.get();
    fx_chain_screen_->set_processor(&fx_processor_);
    screens_.push_back(std::move(fx_chain));

    auto params = std::make_unique<ui::ParamScreen>(fx_processor_, *fx_chain_screen_);
    param_screen_ = params.get();
    screens_.push_back(std::move(params));

    auto viz = std::make_unique<ui::VizScreen>(audio_);
    viz_screen_ = viz.get();
    screens_.push_back(std::move(viz));

    auto settings = std::make_unique<ui::SettingsScreen>(renderer_, audio_, chiptune_, input_);
    settings_screen_ = settings.get();
    settings_screen_->set_preset_callbacks(
        &preset_mgr_,
        [this](const std::string& name, PresetFormat fmt) -> bool {
            return this->save_preset(name, fmt);
        },
        [this](const std::string& filename) -> bool {
            return this->load_preset(filename);
        }
    );
    screens_.push_back(std::move(settings));

    // ── Menu ─────────────────────────────────────────────────────────
    setup_menu_entries();

    // Start on the title screen with chiptune music
    menu_open_ = true;
    main_menu_.on_enter();

    running_ = true;
    fprintf(stderr, "[ENGINE] Ready. %d screens, res %dx%d\n",
            (int)screens_.size(), renderer_.fb_w(), renderer_.fb_h());
    return true;
}

void Engine::setup_menu_entries() {
    main_menu_.set_entries({
        {"RESUME", "Close this menu and return to the current screen.",
            [this]{
                menu_open_ = false;
                main_menu_.set_has_session(true);
                chiptune_.set_playing(false);
            }},
        {"FX CHAIN", "Manage your signal chain — reorder, bypass, and mix effects.",
            [this]{
                current_screen_ = 0; menu_open_ = false;
                main_menu_.set_has_session(true);
                chiptune_.set_playing(false);
            }},
        {"PARAMETERS", "Tweak knobs and sliders for each effect in your chain.",
            [this]{
                current_screen_ = 1; menu_open_ = false;
                main_menu_.set_has_session(true);
                chiptune_.set_playing(false);
            }},
        {"VISUALIZER", "See your audio: scope, spectrum, waterfall, and more.",
            [this]{
                current_screen_ = 2; menu_open_ = false;
                main_menu_.set_has_session(true);
                chiptune_.set_playing(false);
            }},
        {"SETTINGS", "Resolution, audio stats, and CRT post-processing options.",
            [this]{
                current_screen_ = 3; menu_open_ = false;
                main_menu_.set_has_session(true);
                chiptune_.set_playing(false);
            }},
        {"", "", []{}, true, true},  // ── separator ──
        {"HELP", "Learn all the controls, features, and how to use DeMoDOOM.",
            [this]{
                help_open_ = true;
                help_screen_.on_enter();
            }},
        {"QUIT", "Exit DeMoDOOM.",
            [this]{ quit(); }},
    });
}

void Engine::run() {
    auto prev = std::chrono::high_resolution_clock::now();

    while (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        // FPS counter
        fps_counter_++;
        fps_timer_ += dt;
        if (fps_timer_ >= 1.0f) {
            fps_display_ = fps_counter_;
            fps_counter_ = 0;
            fps_timer_ -= 1.0f;
        }

        process_events();
        update(dt);
        render();

        auto frame_end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(frame_end - now).count();
        if (elapsed < FRAME_TIME_MS)
            SDL_Delay(uint32_t(FRAME_TIME_MS - elapsed));
    }
}

void Engine::quit() { running_ = false; }

void Engine::process_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) { quit(); return; }
        if (ev.type == SDL_WINDOWEVENT &&
            ev.window.event == SDL_WINDOWEVENT_CLOSE) { quit(); return; }
        input_.process_sdl_event(ev);
    }
}

void Engine::update(float dt) {
    input_.update();

    // Global hotkeys (always active)
    if (input_.pressed(Action::QUIT)) { quit(); return; }
    if (input_.pressed(Action::FULLSCREEN_TOGGLE)) renderer_.toggle_fullscreen();

    // F3 debug
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    static bool f3_prev = false;
    if (keys[SDL_SCANCODE_F3] && !f3_prev) show_debug_ = !show_debug_;
    f3_prev = keys[SDL_SCANCODE_F3];

    // Escape / Menu toggle (only when help is not open)
    if (!help_open_) {
        if (input_.pressed(Action::NAV_BACK) || input_.pressed(Action::MENU_OPEN)) {
            if (menu_open_) {
                menu_open_ = false;
                chiptune_.set_playing(false);
                main_menu_.set_has_session(true);
            } else {
                menu_open_ = true;
                main_menu_.on_enter();
                chiptune_.reset();
                chiptune_.set_playing(true);
            }
        }
    }

    // Help overlay (highest priority — eats all input when open)
    if (help_open_) {
        help_screen_.update(input_, dt);
        if (help_screen_.wants_close()) {
            help_open_ = false;
            help_screen_.reset_close();
        }
        return;
    }

    if (menu_open_) {
        main_menu_.update(input_, dt);
        if (main_menu_.wants_close()) {
            menu_open_ = false;
            main_menu_.reset_close();
            chiptune_.set_playing(false);
            main_menu_.set_has_session(true);
        }
        return; // Menu eats all input
    }

    // Screen switching (Tab)
    if (input_.pressed(Action::SCREEN_NEXT)) {
        screens_[current_screen_]->on_exit();
        current_screen_ = (current_screen_ + 1) % (int)screens_.size();
        screens_[current_screen_]->on_enter();
    }
    if (input_.pressed(Action::SCREEN_PREV)) {
        screens_[current_screen_]->on_exit();
        current_screen_ = (current_screen_ - 1 + (int)screens_.size()) % (int)screens_.size();
        screens_[current_screen_]->on_enter();
    }

    // Apply axes to FX chain slot params (if any slot is loaded)
    // TODO: Per-slot axis mapping

    screens_[current_screen_]->update(input_, dt);
}

void Engine::render() {
    renderer_.begin_frame();

    // Draw current screen
    if (!screens_.empty())
        screens_[current_screen_]->draw(renderer_);

    // Screen tabs
    draw_screen_tabs();

    // Help bar
    draw_help_bar();

    // Menu overlay
    if (menu_open_)
        main_menu_.draw(renderer_);

    // Help overlay (on top of menu)
    if (help_open_)
        help_screen_.draw(renderer_);

    // Debug
    if (show_debug_)
        draw_debug_overlay();

    renderer_.end_frame();
}

void Engine::draw_screen_tabs() {
    using namespace palette;
    using namespace renderer;

    int W = renderer_.fb_w();
    int tx = W - 4;

    for (int i = (int)screens_.size() - 1; i >= 0; --i) {
        const auto& nm = screens_[i]->name();
        int tw = Font::measure(nm) + 8;
        tx -= tw;

        bool active = (i == current_screen_);
        Color bg  = active ? DARK_GRAY : MENU_BG;
        Color txt = active ? CYAN_LIGHT : MID_GRAY;

        renderer_.rect_fill(tx, 0, tw, 11, bg);
        if (active) {
            renderer_.hline(tx, tx+tw-1, 0, CYAN);
            renderer_.hline(tx, tx+tw-1, 11, CYAN_DARK);
        }
        Font::draw_centered(renderer_, tx, 2, tw, nm, txt);
        tx -= 1;
    }

    // DeMoDOOM logo at far left
    Font::draw_glow(renderer_, 4, 2, "DeMoDOOM", CYAN, palette::GLOW_CYAN);
}

void Engine::draw_help_bar() {
    using namespace palette;
    using namespace renderer;

    int W = renderer_.fb_w(), H = renderer_.fb_h();
    int bar_h = 10;
    int by = H - bar_h;

    renderer_.rect_fill(0, by, W, bar_h, {15, 15, 18});
    renderer_.hline(0, W-1, by, MENU_BORDER);

    // Don't draw help bar when overlays are covering it
    if (help_open_) return;

    if (menu_open_) {
        // Menu mode: just show the menu help
        Font::draw_string(renderer_, 4, by+2, main_menu_.help_text(), MID_GRAY);
        return;
    }

    // Normal mode: screen-specific help on left, global hint on right
    if (!screens_.empty()) {
        std::string help = screens_[current_screen_]->help_text();
        if (!help.empty())
            Font::draw_string(renderer_, 4, by+2, help, MID_GRAY);
    }

    // Compact global hint — fit to resolution
    const char* hint = (W >= 480)
        ? "Esc:Menu  Tab:Screen  [/]:Sub-Tab  F3:Debug"
        : "Esc:Menu  Tab:Scrn  F3:Dbg";
    Font::draw_right(renderer_, W-4, by+2, hint, DARK_GRAY.lerp(MID_GRAY, 0.5f));
}

void Engine::draw_debug_overlay() {
    using namespace palette;
    using namespace renderer;

    int panel_w = 170;
    renderer_.dim_region(0, 0, panel_w, renderer_.fb_h(), 180);
    renderer_.vline(panel_w, 0, renderer_.fb_h()-1, MENU_BORDER);

    int y = 4, lh = 9;
    char buf[80];

    Font::draw_string(renderer_, 4, y, "DEBUG [F3]", CYAN); y += lh + 2;

    snprintf(buf, sizeof(buf), "FPS: %d", fps_display_);
    Font::draw_string(renderer_, 4, y, buf, GREEN); y += lh;

    snprintf(buf, sizeof(buf), "Res: %dx%d (#%d)",
             renderer_.fb_w(), renderer_.fb_h(), renderer_.resolution_index());
    Font::draw_string(renderer_, 4, y, buf, LIGHT_GRAY); y += lh;

    snprintf(buf, sizeof(buf), "CPU: %.1f%%", audio_.cpu_load()*100);
    Color cc = audio_.cpu_load()>.8f ? RED : audio_.cpu_load()>.5f ? YELLOW : GREEN;
    Font::draw_string(renderer_, 4, y, buf, cc); y += lh;

    snprintf(buf, sizeof(buf), "XRuns: %d", audio_.xruns());
    Font::draw_string(renderer_, 4, y, buf, audio_.xruns() ? RED : GREEN); y += lh;

    snprintf(buf, sizeof(buf), "SR: %d Hz", audio_.sample_rate());
    Font::draw_string(renderer_, 4, y, buf, LIGHT_GRAY); y += lh;

    y += 4;
    Font::draw_string(renderer_, 4, y, "FX CHAIN:", VIOLET_LIGHT); y += lh;
    int loaded_slots = 0;
    for (int i = 0; i < MAX_FX_SLOTS; ++i)
        if (fx_processor_.slot_loaded(i)) loaded_slots++;
    snprintf(buf, sizeof(buf), "%d/%d slots loaded", loaded_slots, MAX_FX_SLOTS);
    Font::draw_string(renderer_, 4, y, buf, LIGHT_GRAY); y += lh;

    y += 4;
    Font::draw_string(renderer_, 4, y, "SCREEN:", VIOLET_LIGHT); y += lh;
    snprintf(buf, sizeof(buf), "%d/%d: %s", current_screen_+1,
             (int)screens_.size(),
             screens_.empty() ? "none" : screens_[current_screen_]->name().c_str());
    Font::draw_string(renderer_, 4, y, buf, LIGHT_GRAY); y += lh;

    y += 4;
    Font::draw_string(renderer_, 4, y, "POST-FX:", VIOLET_LIGHT); y += lh;
    snprintf(buf, sizeof(buf), "Scan:%s %.0f%%",
             renderer_.scanlines_enabled ? "ON" : "off",
             renderer_.scanline_intensity * 100);
    Font::draw_string(renderer_, 4, y, buf, MID_GRAY); y += lh;
    snprintf(buf, sizeof(buf), "Bloom:%s %.0f%%",
             renderer_.bloom_enabled ? "ON" : "off",
             renderer_.bloom_intensity * 100);
    Font::draw_string(renderer_, 4, y, buf, MID_GRAY); y += lh;
    snprintf(buf, sizeof(buf), "Vig:%s Barrel:%s",
             renderer_.vignette_enabled ? "ON" : "off",
             renderer_.barrel_enabled ? "ON" : "off");
    Font::draw_string(renderer_, 4, y, buf, MID_GRAY); y += lh;

    y += 4;
    Font::draw_string(renderer_, 4, y, "INPUT:", VIOLET_LIGHT); y += lh;
    std::string dbg = input_.debug_string();
    size_t pos = 0;
    int lines = 0;
    while (pos < dbg.size() && lines < 8 && y < renderer_.fb_h() - 12) {
        size_t nl = dbg.find('\n', pos);
        if (nl == std::string::npos) nl = dbg.size();
        std::string ln = dbg.substr(pos, std::min(nl - pos, size_t(28)));
        Font::draw_string(renderer_, 4, y, ln, MID_GRAY);
        y += lh;
        pos = nl + 1;
        lines++;
    }
}

bool Engine::save_preset(const std::string& name, PresetFormat format) {
    Preset p;
    p.name = name;

    // Snapshot FX chain state
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        p.slots[i].bypassed = fx_processor_.slot_bypassed(i);
        p.slots[i].wet_mix = fx_processor_.slot_wet_mix(i);
        p.slots[i].dsp_path = fx_processor_.slot_dsp_name(i);

        if (fx_processor_.slot_loaded(i)) {
            const auto& params = fx_processor_.slot_params(i);
            for (const auto& pd : params) {
                p.slots[i].params[pd.path] =
                    fx_processor_.get_slot_param(i, pd.index);
            }
        }
    }

    // Snapshot post-FX
    p.post_fx.scanlines = renderer_.scanlines_enabled;
    p.post_fx.scanline_intensity = renderer_.scanline_intensity;
    p.post_fx.bloom = renderer_.bloom_enabled;
    p.post_fx.bloom_intensity = renderer_.bloom_intensity;
    p.post_fx.vignette = renderer_.vignette_enabled;
    p.post_fx.barrel = renderer_.barrel_enabled;

    if (preset_mgr_.save(p, format)) {
        fprintf(stderr, "[ENGINE] Preset saved: %s.%s\n",
                name.c_str(), preset_format_ext(format));
        return true;
    }
    fprintf(stderr, "[ENGINE] Preset save failed: %s\n", name.c_str());
    return false;
}

bool Engine::load_preset(const std::string& filename) {
    Preset p;
    if (!preset_mgr_.load(filename, p)) return false;

    // Apply FX chain state
    bool has_input_dsp = false;
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        const auto& s = p.slots[i];
        if (!s.dsp_path.empty()) {
            fx_processor_.load_slot(i, s.dsp_path);
            has_input_dsp = true;
        }
        fx_processor_.set_slot_bypassed(i, s.bypassed);
        fx_processor_.set_slot_wet_mix(i, s.wet_mix);

        // Apply params
        for (const auto& [path, val] : s.params) {
            const auto& params = fx_processor_.slot_params(i);
            for (int j = 0; j < (int)params.size(); ++j) {
                if (params[j].path == path) {
                    fx_processor_.set_slot_param(i, j, val);
                    break;
                }
            }
        }
    }

    // Apply post-FX
    renderer_.scanlines_enabled = p.post_fx.scanlines;
    renderer_.scanline_intensity = p.post_fx.scanline_intensity;
    renderer_.bloom_enabled = p.post_fx.bloom;
    renderer_.bloom_intensity = p.post_fx.bloom_intensity;
    renderer_.vignette_enabled = p.post_fx.vignette;
    renderer_.barrel_enabled = p.post_fx.barrel;

    if (has_input_dsp) audio_.activate_input();

    fprintf(stderr, "[ENGINE] Preset loaded: %s\n", p.name.c_str());
    return true;
}

} // namespace demod
