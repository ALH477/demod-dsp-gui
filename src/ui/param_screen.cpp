// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Parameter Screen                                       ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/param_screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace demod::ui {

ParamScreen::ParamScreen(audio::FXChainProcessor& fx, FXChainScreen& chain)
    : fx_(fx), chain_(chain) { init_demo_params(); }

void ParamScreen::init_demo_params() {
    demo_params_ = {
        {"CUTOFF",     0.65f, 20,20000, 1000, 1},
        {"RESONANCE",  0.30f, 0, 1, 0.3f, 0.01f},
        {"DRIVE",      0.45f, 0, 10, 2, 0.1f},
        {"OSC MIX",    0.50f, 0, 1, 0.5f, 0.01f},
        {"ATTACK",     0.10f, 0.001f, 2, 0.01f, 0.001f},
        {"DECAY",      0.25f, 0.001f, 2, 0.2f, 0.001f},
        {"SUSTAIN",    0.70f, 0, 1, 0.7f, 0.01f},
        {"RELEASE",    0.40f, 0.001f, 5, 0.3f, 0.001f},
        {"LFO RATE",   0.15f, 0.01f, 20, 1, 0.01f},
        {"LFO DEPTH",  0.35f, 0, 1, 0, 0.01f},
        {"DETUNE",     0.02f, 0, 1, 0, 0.001f},
        {"REVERB MIX", 0.20f, 0, 1, 0.2f, 0.01f},
        {"REVERB SIZE",0.60f, 0, 1, 0.5f, 0.01f},
        {"DELAY TIME", 0.38f, 0, 2, 0.25f, 0.01f},
        {"DELAY FB",   0.55f, 0, 0.95f, 0.4f, 0.01f},
        {"OUTPUT",     0.80f, 0, 1, 0.8f, 0.01f},
    };
}

int ParamScreen::param_count() const {
    if (fx_.slot_loaded(active_fx_))
        return fx_.slot_num_params(active_fx_);
    return (int)demo_params_.size();
}
std::string ParamScreen::param_label(int i) const {
    if (fx_.slot_loaded(active_fx_)) {
        const auto& params = fx_.slot_params(active_fx_);
        return (i < (int)params.size()) ? params[i].label : "???";
    }
    return demo_params_[i].label;
}
float ParamScreen::param_normalized(int i) const {
    if (fx_.slot_loaded(active_fx_)) {
        const auto& params = fx_.slot_params(active_fx_);
        if (i >= (int)params.size()) return 0;
        const auto& p = params[i];
        return (p.max<=p.min) ? 0 : (fx_.get_slot_param(active_fx_, i)-p.min)/(p.max-p.min);
    }
    return demo_params_[i].value;
}
float ParamScreen::param_default_normalized(int i) const {
    if (fx_.slot_loaded(active_fx_)) {
        const auto& params = fx_.slot_params(active_fx_);
        if (i >= (int)params.size()) return 0;
        const auto& p = params[i];
        return (p.max<=p.min) ? 0 : (p.init-p.min)/(p.max-p.min);
    }
    return demo_params_[i].init;
}
void ParamScreen::adjust_param(int i, float delta) {
    if (fx_.slot_loaded(active_fx_)) {
        const auto& params = fx_.slot_params(active_fx_);
        if (i >= (int)params.size()) return;
        const auto& p = params[i];
        float val = fx_.get_slot_param(active_fx_, i) + delta * p.step;
        fx_.set_slot_param(active_fx_, i, std::clamp(val, p.min, p.max));
    } else {
        demo_params_[i].value = std::clamp(demo_params_[i].value+delta*0.01f, 0.f, 1.f);
    }
}
void ParamScreen::reset_param(int i) {
    if (fx_.slot_loaded(active_fx_)) {
        const auto& params = fx_.slot_params(active_fx_);
        if (i >= (int)params.size()) return;
        fx_.set_slot_param(active_fx_, i, params[i].init);
    } else {
        demo_params_[i].value = demo_params_[i].init;
    }
}

void ParamScreen::update(const input::InputManager& input, float dt) {
    int count = param_count();
    if (count == 0) return;

    // FX slot switching with page up/down
    int num_fx = (int)chain_.slots().size();
    if (input.pressed(Action::NAV_PAGE_DOWN)) {
        active_fx_ = std::min(active_fx_ + 1, num_fx - 1);
        focused_param_ = 0; scroll_ = 0;
    }
    if (input.pressed(Action::NAV_PAGE_UP)) {
        active_fx_ = std::max(active_fx_ - 1, 0);
        focused_param_ = 0; scroll_ = 0;
    }

    // Param navigation
    if (input.pressed(Action::NAV_DOWN))
        focused_param_ = std::min(focused_param_+1, count-1);
    if (input.pressed(Action::NAV_UP))
        focused_param_ = std::max(focused_param_-1, 0);

    // Adjust with repeat
    bool inc = input.held(Action::PARAM_INC) || input.held(Action::NAV_RIGHT);
    bool dec = input.held(Action::PARAM_DEC) || input.held(Action::NAV_LEFT);
    if (inc || dec) {
        repeat_timer_ += dt;
        bool fire = input.pressed(Action::PARAM_INC) || input.pressed(Action::PARAM_DEC) ||
                    input.pressed(Action::NAV_RIGHT) || input.pressed(Action::NAV_LEFT);
        if (!fire && repeat_timer_ > 0.35f) { fire = true; repeat_timer_ = 0.29f; }
        if (fire) adjust_param(focused_param_, inc ? 1.f : -1.f);
    } else {
        repeat_timer_ = 0;
    }

    // Analog axis
    float ax = input.axis(Action::AXIS_X);
    if (std::fabs(ax) > 0.1f) adjust_param(focused_param_, ax * dt * 2);
    // Right stick for fine control
    float az = input.axis(Action::AXIS_Z);
    if (std::fabs(az) > 0.1f) adjust_param(focused_param_, az * dt * 0.5f);

    if (input.pressed(Action::PARAM_RESET)) reset_param(focused_param_);
    if (input.pressed(Action::PARAM_RANDOMIZE)) {
        if (fx_.slot_loaded(active_fx_))
            fx_.randomize_slot_params(active_fx_);
        else
            for (int i = 0; i < count; ++i)
                demo_params_[i].value = float(rand()) / RAND_MAX;
    }
    if (input.pressed(Action::BYPASS_TOGGLE)) bypass_ = !bypass_;

    // Scroll
    int vis = 14;
    if (focused_param_ < scroll_) scroll_ = focused_param_;
    if (focused_param_ >= scroll_ + vis) scroll_ = focused_param_ - vis + 1;
}

void ParamScreen::draw(renderer::Renderer& r) {
    using namespace demod::palette;
    using namespace demod::renderer;

    int W = r.fb_w(), H = r.fb_h();
    int count = param_count();

    // ── Header ───────────────────────────────────────────────────────
    r.rect_fill(0, 0, W, 14, DARK_GRAY);
    r.hline(0, W-1, 14, CYAN_DARK);
    Font::draw_glow(r, 4, 3, "PARAMS", CYAN, GLOW_CYAN);

    // Active FX name
    auto& slots = chain_.slots();
    if (active_fx_ < (int)slots.size()) {
        Color fc = slots[active_fx_].color;
        Font::draw_string(r, 50, 3, slots[active_fx_].name, fc);
    }

    if (bypass_) Font::draw_right(r, W-4, 3, "[BYPASS]", RED);

    // ── Breadcrumb ───────────────────────────────────────────────────
    Breadcrumb bc;
    bc.x = 4; bc.y = 17;
    bc.path = { "FX CHAIN" };
    if (active_fx_ < (int)slots.size())
        bc.path.push_back(slots[active_fx_].name);
    bc.path.push_back("PARAMS");
    bc.draw(r);

    // ── Parameter list ───────────────────────────────────────────────
    int list_y = 28;
    int row_h  = std::max(12, std::min(16, (H - list_y - 14) / 14));
    int vis    = (H - list_y - 14) / row_h;

    for (int vi = 0; vi < vis; ++vi) {
        int pi = scroll_ + vi;
        if (pi >= count) break;

        int y = list_y + vi * row_h;
        bool focused = (pi == focused_param_);

        if (focused) {
            r.rect_fill(0, y, W, row_h-1, MENU_HL);
            r.rect_fill(0, y, 2, row_h-1, active_fx_ < (int)slots.size()
                        ? slots[active_fx_].color : CYAN);
        }

        Color slot_accent = (active_fx_ < (int)slots.size())
                            ? slots[active_fx_].color : CYAN;

        HSlider slider;
        slider.x = 4; slider.y = y+1;
        slider.w = W-8; slider.h = row_h-2;
        slider.label = param_label(pi);
        slider.value = param_normalized(pi);
        slider.default_value = param_default_normalized(pi);
        slider.focused = focused;
        slider.bypassed = bypass_;
        slider.accent = slot_accent;
        slider.draw(r);
    }

    // Scroll indicators
    if (scroll_ > 0)
        Font::draw_centered(r, 0, list_y-8, W, "^^^", MID_GRAY);
    if (scroll_+vis < count)
        Font::draw_centered(r, 0, list_y+vis*row_h, W, "vvv", MID_GRAY);

    // ── FX switcher hint ─────────────────────────────────────────────
    char buf[48];
    snprintf(buf, sizeof(buf), "PgUp/PgDn: FX %d/%d", active_fx_+1, (int)slots.size());
    Font::draw_right(r, W-4, 17, buf, MID_GRAY);
}

} // namespace demod::ui
