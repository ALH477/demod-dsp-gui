// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Settings Screen                                        ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/settings_screen.hpp"
#include <algorithm>
#include <cstdio>
#include <SDL2/SDL.h>

namespace demod::ui {

SettingsScreen::SettingsScreen(renderer::Renderer& rend,
                               audio::AudioEngine& audio,
                               audio::ChiptuneSynth& chiptune,
                               input::InputManager& input_mgr)
    : rend_(rend), audio_(audio), chiptune_(chiptune), input_mgr_(input_mgr)
{
    chiptune_enabled_ = true;
    chiptune_volume_  = chiptune_.volume();
}

const char* SettingsScreen::section_name(Section s) const {
    switch (s) {
        case Section::DISPLAY: return "DISPLAY";
        case Section::AUDIO:   return "AUDIO";
        case Section::POST_FX: return "POST-FX";
        case Section::PRESETS: return "PRESETS";
        case Section::INPUT:   return "INPUT";
        case Section::ABOUT:   return "ABOUT";
        default: return "?";
    }
}

void SettingsScreen::set_preset_callbacks(
    PresetManager* mgr,
    std::function<bool(const std::string&, PresetFormat)> save_cb,
    std::function<bool(const std::string&)> load_cb) {
    preset_mgr_ = mgr;
    preset_save_cb_ = save_cb;
    preset_load_cb_ = load_cb;
    if (mgr) {
        preset_list_ = mgr->list_presets();
        preset_format_idx_ = int(mgr->format());
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  ITEM GENERATION — each section builds its list of SettingItems.
//  Item indices within each section MUST match apply_change() cases.
// ═══════════════════════════════════════════════════════════════════════

std::vector<SettingsScreen::SettingItem> SettingsScreen::current_items() const {
    std::vector<SettingItem> items;

    switch (section_) {

    // ── DISPLAY ──────────────────────────────────────────────────────
    case Section::DISPLAY: {
        // 0: Resolution (editable)
        items.push_back({"Resolution", RESOLUTIONS[rend_.resolution_index()].label});
        // 1: Framebuffer info (read-only)
        items.push_back({"Framebuffer",
            std::to_string(rend_.fb_w()) + "x" + std::to_string(rend_.fb_h()), false});
        // 2: Fullscreen hint (read-only)
        items.push_back({"Fullscreen", "F11 to toggle", false});
        // 3: FPS (read-only)
        items.push_back({"Target FPS", std::to_string(TARGET_FPS), false});
    } break;

    // ── AUDIO ────────────────────────────────────────────────────────
    case Section::AUDIO: {
        char buf[32];
        // 0: Menu Music toggle
        items.push_back({"Menu Music", chiptune_enabled_ ? "ON" : "OFF",
                          true, true, chiptune_enabled_});
        // 1: Menu Music volume
        snprintf(buf, sizeof(buf), "%.0f%%", chiptune_volume_ * 100);
        items.push_back({"  Volume", buf});
        // 2: separator
        items.push_back({"", "", false});
        // 3: Sample Rate (read-only)
        snprintf(buf, sizeof(buf), "%d Hz", audio_.sample_rate());
        items.push_back({"Sample Rate", buf, false});
        // 4: CPU Load (read-only)
        snprintf(buf, sizeof(buf), "%.1f%%", audio_.cpu_load() * 100);
        items.push_back({"CPU Load", buf, false});
        // 5: XRuns (read-only)
        snprintf(buf, sizeof(buf), "%d", audio_.xruns());
        items.push_back({"XRuns", buf, false});
        // 6: Backend (read-only)
        items.push_back({"Backend", "PipeWire", false});
        // 7: Status (read-only)
        items.push_back({"Status", audio_.running() ? "Running" : "Stopped", false});
    } break;

    // ── POST-FX ──────────────────────────────────────────────────────
    case Section::POST_FX: {
        char buf[16];
        // 0: Scanlines toggle
        items.push_back({"Scanlines", rend_.scanlines_enabled ? "ON" : "OFF",
                          true, true, rend_.scanlines_enabled});
        // 1: Scanline intensity
        snprintf(buf, sizeof(buf), "%.0f%%", rend_.scanline_intensity * 100);
        items.push_back({"  Intensity", buf});
        // 2: Bloom toggle
        items.push_back({"Bloom", rend_.bloom_enabled ? "ON" : "OFF",
                          true, true, rend_.bloom_enabled});
        // 3: Bloom intensity
        snprintf(buf, sizeof(buf), "%.0f%%", rend_.bloom_intensity * 100);
        items.push_back({"  Intensity", buf});
        // 4: Vignette toggle
        items.push_back({"Vignette", rend_.vignette_enabled ? "ON" : "OFF",
                          true, true, rend_.vignette_enabled});
        // 5: Barrel toggle
        items.push_back({"Barrel Distortion", rend_.barrel_enabled ? "ON" : "OFF",
                          true, true, rend_.barrel_enabled});
    } break;

    // ── PRESETS ───────────────────────────────────────────────────────
    case Section::PRESETS: {
        char buf[64];
        // 0: Format selector
        PresetFormat fmt = PresetFormat(preset_format_idx_);
        items.push_back({"Format", preset_format_name(fmt), true});
        // 1: Save action
        items.push_back({"Save Preset", "[Enter to save]", true});
        // 2: separator
        items.push_back({"", "", false});
        // 3+: Saved presets
        if (preset_list_.empty()) {
            items.push_back({"No presets", "saved yet", false});
        } else {
            for (int i = 0; i < (int)preset_list_.size(); ++i) {
                bool sel = (i == preset_selected_);
                snprintf(buf, sizeof(buf), "%s%s", sel ? "> " : "",
                         preset_list_[i].c_str());
                items.push_back({buf, "Load", true});
            }
        }
    } break;

    // ── INPUT ────────────────────────────────────────────────────────
    case Section::INPUT: {
        int n = input_mgr_.device_count();
        items.push_back({"Devices", std::to_string(n) + " registered", false});
        for (int i = 0; i < n; ++i) {
            std::string status = input_mgr_.device_connected(i) ? "Active" : "Disconnected";
            items.push_back({input_mgr_.device_name(i), status, false});
        }
        items.push_back({"", "", false});

        // Key bindings list
        struct ActionEntry { const char* name; Action action; };
        const ActionEntry actions[] = {
            {"Navigate", Action::NAV_UP},
            {"Select", Action::NAV_SELECT},
            {"Back/Menu", Action::NAV_BACK},
            {"Param +", Action::PARAM_INC},
            {"Param -", Action::PARAM_DEC},
            {"Reset Param", Action::PARAM_RESET},
            {"Bypass", Action::BYPASS_TOGGLE},
            {"Next Screen", Action::SCREEN_NEXT},
            {"Play", Action::TRANSPORT_PLAY},
            {"Quit", Action::QUIT},
        };
        for (const auto& ae : actions) {
            int ai = &ae - actions;
            bool is_rebinding = rebind_mode_ && rebind_target_ == ai;
            items.push_back({ae.name, is_rebinding ? "[Listening...]" : "[Enter to rebind]", true});
        }
    } break;

    // ── ABOUT ────────────────────────────────────────────────────────
    case Section::ABOUT: {
        items.push_back({"Application", "DeMoDOOM v0.2.0", false});
        items.push_back({"Engine", "DOOM-style Software Renderer", false});
        items.push_back({"Audio", "PipeWire Native API", false});
        items.push_back({"DSP", "Faust JIT / DSO / C++", false});
        items.push_back({"Music", "Built-in Chiptune Synth", false});
        items.push_back({"Input", "SDL2 + LSL abstraction", false});
        items.push_back({"", "", false});
        items.push_back({"License", "GPL-3.0", false});
        items.push_back({"Author", "DeMoD LLC", false});
        items.push_back({"", "", false});
        items.push_back({"github.com/ALH477", "", false});
    } break;

    default: break;
    }
    return items;
}

// ═══════════════════════════════════════════════════════════════════════
//  APPLY CHANGES — direction: -1 = left, +1 = right, 0 = toggle
// ═══════════════════════════════════════════════════════════════════════

void SettingsScreen::apply_change(int idx, int dir) {
    switch (section_) {

    case Section::DISPLAY: {
        if (idx == 0) {
            int next = std::clamp(rend_.resolution_index() + dir, 0, NUM_RESOLUTIONS - 1);
            rend_.set_resolution(next);
        }
    } break;

    case Section::AUDIO: {
        switch (idx) {
        case 0: // Menu Music toggle
            chiptune_enabled_ = !chiptune_enabled_;
            chiptune_.set_enabled(chiptune_enabled_);
            if (!chiptune_enabled_) {
                chiptune_.set_playing(false);
            }
            break;
        case 1: // Volume
            chiptune_volume_ = std::clamp(chiptune_volume_ + dir * 0.05f, 0.0f, 1.0f);
            chiptune_.set_volume(chiptune_volume_);
            break;
        }
    } break;

    case Section::POST_FX: {
        switch (idx) {
        case 0: rend_.scanlines_enabled = !rend_.scanlines_enabled; break;
        case 1: rend_.scanline_intensity = std::clamp(
                    rend_.scanline_intensity + dir * 0.05f, 0.f, 1.f); break;
        case 2: rend_.bloom_enabled = !rend_.bloom_enabled; break;
        case 3: rend_.bloom_intensity = std::clamp(
                    rend_.bloom_intensity + dir * 0.02f, 0.f, 0.5f); break;
        case 4: rend_.vignette_enabled = !rend_.vignette_enabled; break;
        case 5: rend_.barrel_enabled = !rend_.barrel_enabled; break;
        }
    } break;

    case Section::PRESETS: {
        if (idx == 0) {
            // Cycle format
            preset_format_idx_ = (preset_format_idx_ + dir + int(PresetFormat::FORMAT_COUNT))
                                 % int(PresetFormat::FORMAT_COUNT);
            if (preset_mgr_) preset_mgr_->set_format(PresetFormat(preset_format_idx_));
        } else if (idx == 1) {
            // Save current state
            if (preset_save_cb_) {
                std::string name = "preset_" + std::to_string(preset_list_.size() + 1);
                preset_save_cb_(name, PresetFormat(preset_format_idx_));
                if (preset_mgr_) preset_list_ = preset_mgr_->list_presets();
            }
        } else if (idx >= 3 && preset_load_cb_) {
            // Load selected preset
            int pi = idx - 3;
            if (pi >= 0 && pi < (int)preset_list_.size()) {
                preset_load_cb_(preset_list_[pi]);
            }
        }
    } break;

    case Section::INPUT: {
        // Action entries start at index 5 (after 4 device/separator items)
        struct ActionEntry { const char* name; Action action; };
        const ActionEntry actions[] = {
            {"Navigate", Action::NAV_UP},
            {"Select", Action::NAV_SELECT},
            {"Back/Menu", Action::NAV_BACK},
            {"Param +", Action::PARAM_INC},
            {"Param -", Action::PARAM_DEC},
            {"Reset Param", Action::PARAM_RESET},
            {"Bypass", Action::BYPASS_TOGGLE},
            {"Next Screen", Action::SCREEN_NEXT},
            {"Play", Action::TRANSPORT_PLAY},
            {"Quit", Action::QUIT},
        };
        int action_idx = idx - 5;
        if (action_idx >= 0 && action_idx < 10) {
            rebind_mode_ = true;
            rebind_target_ = action_idx;
            rebind_action_ = actions[action_idx].action;
            fprintf(stderr, "[SETTINGS] Rebind mode: listening for key...\n");
        }
    } break;

    default: break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  NAVIGATION HELPER
// ═══════════════════════════════════════════════════════════════════════

bool SettingsScreen::skip_to_editable(int dir, const std::vector<SettingItem>& items) {
    // Try to find the next editable item in the given direction.
    // If none found, stay put. Skips separators (empty labels) and
    // read-only items, but allows landing on read-only if no editable
    // item exists in that direction (so the user isn't stuck).
    int count = (int)items.size();
    int start = focused_;
    for (int i = 0; i < count; ++i) {
        int next = focused_ + dir;
        if (next < 0 || next >= count) return false;
        focused_ = next;
        // Accept any non-separator item
        if (!items[focused_].label.empty()) return true;
    }
    focused_ = start; // Couldn't move
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════════════

void SettingsScreen::update(const input::InputManager& input, float dt) {
    (void)dt;

    // Key rebind capture
    if (rebind_mode_) {
        // Check for a new keypress via SDL
        int num_keys = 0;
        const uint8_t* keys = SDL_GetKeyboardState(&num_keys);
        for (int i = 0; i < num_keys && i < SDL_NUM_SCANCODES; ++i) {
            if (keys[i] && i != SDL_SCANCODE_ESCAPE) {
                // Create a new keyboard binding for this action
                input_mgr_.bind("keyboard", { i, rebind_action_, 0, 1, false });
                fprintf(stderr, "[SETTINGS] Bound scancode %d (%s) to action %d\n",
                        i, SDL_GetScancodeName(SDL_Scancode(i)), int(rebind_action_));
                rebind_mode_ = false;
                rebind_target_ = -1;
                return;
            }
        }
        // Escape cancels
        if (keys[SDL_SCANCODE_ESCAPE]) {
            rebind_mode_ = false;
            rebind_target_ = -1;
            return;
        }
        return; // Don't process other input while rebinding
    }

    // Section switching
    if (input.pressed(Action::NAV_TAB_NEXT)) {
        section_ = Section((int(section_) + 1) % section_count());
        focused_ = 0; scroll_ = 0;
    }
    if (input.pressed(Action::NAV_TAB_PREV)) {
        section_ = Section((int(section_) - 1 + section_count()) % section_count());
        focused_ = 0; scroll_ = 0;
    }

    auto items = current_items();
    int count = (int)items.size();

    // Navigation — skip separators
    if (input.pressed(Action::NAV_DOWN)) {
        skip_to_editable(+1, items);
    }
    if (input.pressed(Action::NAV_UP)) {
        skip_to_editable(-1, items);
    }

    // Clamp in case items changed
    focused_ = std::clamp(focused_, 0, std::max(0, count - 1));

    // Adjust / toggle
    if (focused_ < count && items[focused_].editable) {
        if (input.pressed(Action::NAV_RIGHT) || input.pressed(Action::PARAM_INC))
            apply_change(focused_, 1);
        if (input.pressed(Action::NAV_LEFT) || input.pressed(Action::PARAM_DEC))
            apply_change(focused_, -1);
        if (input.pressed(Action::NAV_SELECT)) {
            if (items[focused_].is_toggle)
                apply_change(focused_, 0);
            else
                apply_change(focused_, 1); // Enter triggers rebind for INPUT items
        }
    }

    // Scroll
    int vis = 12;
    if (focused_ < scroll_) scroll_ = focused_;
    if (focused_ >= scroll_ + vis) scroll_ = focused_ - vis + 1;
}

// ═══════════════════════════════════════════════════════════════════════
//  DRAW
// ═══════════════════════════════════════════════════════════════════════

void SettingsScreen::draw(renderer::Renderer& r) {
    using namespace demod::palette;
    using namespace demod::renderer;

    int W = r.fb_w(), H = r.fb_h();

    // ── Header ───────────────────────────────────────────────────────
    r.rect_fill(0, 0, W, 14, DARK_GRAY);
    r.hline(0, W-1, 14, CYAN_DARK);
    Font::draw_glow(r, 4, 3, "SETTINGS", VIOLET, GLOW_VIOLET);

    // ── Section tabs ─────────────────────────────────────────────────
    TabBar tabs;
    tabs.x = 0; tabs.y = 15; tabs.w = W;
    tabs.active_tab = int(section_);
    for (int i = 0; i < section_count(); ++i)
        tabs.tabs.push_back(section_name(Section(i)));
    tabs.draw(r);

    // ── Items ────────────────────────────────────────────────────────
    auto items = current_items();
    int list_y = 30;
    int row_h  = 14;
    int vis    = std::max(4, (H - list_y - 14) / row_h);

    for (int vi = 0; vi < vis; ++vi) {
        int idx = scroll_ + vi;
        if (idx >= (int)items.size()) break;
        const auto& item = items[idx];

        int iy = list_y + vi * row_h;
        bool sel = (idx == focused_);

        // Separator
        if (item.label.empty()) {
            r.hline(4, W-5, iy + row_h/2, MENU_BORDER);
            continue;
        }

        // Highlight
        if (sel) {
            r.rect_fill(0, iy, W, row_h-1, MENU_HL);
            r.vline(0, iy, iy+row_h-2, item.editable ? CYAN : MID_GRAY);
        }

        Color lc = sel ? WHITE : (item.editable ? LIGHT_GRAY : MID_GRAY);
        Font::draw_string(r, 8, iy+(row_h-7)/2, item.label, lc);

        // Value
        Color vc = sel && item.editable ? CYAN : MID_GRAY;
        if (item.is_toggle) {
            vc = item.toggle_val ? GREEN : RED;
        }
        Font::draw_right(r, W-8, iy+(row_h-7)/2, item.value, vc);

        // L/R arrows for editable items when focused
        if (sel && item.editable) {
            int val_w = Font::measure(item.value);
            Font::draw_string(r, W - val_w - 22, iy+(row_h-7)/2, "<", CYAN);
            Font::draw_right(r, W-2, iy+(row_h-7)/2, ">", CYAN);
        }
    }
}

} // namespace demod::ui
