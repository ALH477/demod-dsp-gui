// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Visualization Screen                                   ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/viz_screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace demod::ui {

static constexpr float PI = 3.14159265f;

VizScreen::VizScreen(audio::AudioEngine& audio)
    : audio_(audio) {
    std::memset(display_L_, 0, sizeof(display_L_));
    std::memset(display_R_, 0, sizeof(display_R_));
    std::memset(spectrum_, 0, sizeof(spectrum_));
    std::memset(spectrum_peak_, 0, sizeof(spectrum_peak_));
    std::memset(waterfall_, 0, sizeof(waterfall_));
}

void VizScreen::update(const input::InputManager& input, float dt) {
    anim_t_ += dt;

    // Mode switch — L/R shoulder buttons only (SCREEN_NEXT is consumed by engine)
    if (input.pressed(Action::NAV_TAB_NEXT)) {
        mode_ = VizMode((int(mode_) + 1) % int(VizMode::VIZ_COUNT));
    }
    if (input.pressed(Action::NAV_TAB_PREV)) {
        mode_ = VizMode((int(mode_) - 1 + int(VizMode::VIZ_COUNT)) % int(VizMode::VIZ_COUNT));
    }

    // Y scale
    if (input.pressed(Action::NAV_UP))   y_scale_ = std::min(y_scale_*1.5f, 20.f);
    if (input.pressed(Action::NAV_DOWN)) y_scale_ = std::max(y_scale_/1.5f, 0.05f);

    // Time scale
    if (input.pressed(Action::NAV_RIGHT)) time_scale_ = std::min(time_scale_*2, 8.f);
    if (input.pressed(Action::NAV_LEFT))  time_scale_ = std::max(time_scale_/2, 0.125f);

    // Trigger
    if (input.pressed(Action::NAV_SELECT)) trigger_ = (trigger_+1) % 3;

    // Freeze
    if (input.pressed(Action::TRANSPORT_PLAY)) freeze_ = !freeze_;

    // Reset
    if (input.pressed(Action::PARAM_RESET)) {
        y_scale_ = 1; time_scale_ = 1; trigger_ = 0; freeze_ = false;
    }

    if (!freeze_) {
        capture_waveform();
        if (mode_ == VizMode::SPECTRUM || mode_ == VizMode::WATERFALL)
            compute_spectrum();
        if (mode_ == VizMode::WATERFALL)
            update_waterfall();
        if (mode_ == VizMode::LISSAJOUS || mode_ == VizMode::PHASE_METER)
            compute_phase();
    }

    // Peak decay
    for (int i = 0; i < FFT_SIZE/2; ++i) {
        spectrum_peak_[i] = std::max(spectrum_[i], spectrum_peak_[i] - dt * 0.5f);
    }
}

void VizScreen::capture_waveform() {
    int wp = audio_.scope_write_pos.load(std::memory_order_relaxed);
    int bs = audio::AudioEngine::SCOPE_BUF_SIZE;
    int needed = int(DISPLAY_BUF * time_scale_);
    needed = std::min(needed, bs);

    int start = (wp - needed + bs) % bs;

    float temp_L[2048], temp_R[2048];
    for (int i = 0; i < needed && i < 2048; ++i) {
        temp_L[i] = audio_.scope_buffer[(start+i)%bs];
        temp_R[i] = audio_.scope_buffer_R[(start+i)%bs];
    }

    int trig_off = (trigger_ > 0) ? find_trigger(temp_L, needed) : 0;

    for (int i = 0; i < DISPLAY_BUF; ++i) {
        int si = trig_off + int(float(i) * needed / DISPLAY_BUF);
        if (si >= needed) si = needed-1;
        display_L_[i] = temp_L[si];
        display_R_[i] = temp_R[si];
    }
}

int VizScreen::find_trigger(const float* buf, int len) const {
    bool rising = (trigger_ == 1);
    for (int i = 1; i < len - DISPLAY_BUF; ++i) {
        bool cross = rising ? (buf[i-1]<0 && buf[i]>=0) : (buf[i-1]>0 && buf[i]<=0);
        if (cross) return i;
    }
    return 0;
}

void VizScreen::dft_magnitude(const float* in, float* mag, int n) {
    // Hanning-windowed DFT — O(n²) but n is small (512)
    for (int k = 0; k < n/2; ++k) {
        float re = 0, im = 0;
        for (int j = 0; j < n; ++j) {
            float window = 0.5f * (1 - std::cos(2*PI*j/(n-1)));
            float angle = 2*PI*k*j/n;
            re += in[j] * window * std::cos(angle);
            im -= in[j] * window * std::sin(angle);
        }
        mag[k] = std::sqrt(re*re + im*im) / n;
    }
}

void VizScreen::compute_spectrum() {
    float input_buf[FFT_SIZE];
    int avail = std::min(DISPLAY_BUF, FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i)
        input_buf[i] = (i < avail) ? display_L_[i] : 0;
    dft_magnitude(input_buf, spectrum_, FFT_SIZE);
}

void VizScreen::update_waterfall() {
    int cols = std::min(WF_COLS, FFT_SIZE/2);
    for (int i = 0; i < cols; ++i) {
        // Log scale: dB to 0–255
        float db = 20 * std::log10(std::max(1e-6f, spectrum_[i]));
        float norm = std::clamp((db + 80) / 80, 0.f, 1.f);
        waterfall_[wf_row_][i] = uint8_t(norm * 255);
    }
    wf_row_ = (wf_row_ + 1) % WF_ROWS;
}

void VizScreen::compute_phase() {
    // Correlation between L and R
    float sum_lr = 0, sum_ll = 0, sum_rr = 0;
    for (int i = 0; i < DISPLAY_BUF; ++i) {
        sum_lr += display_L_[i] * display_R_[i];
        sum_ll += display_L_[i] * display_L_[i];
        sum_rr += display_R_[i] * display_R_[i];
    }
    float denom = std::sqrt(sum_ll * sum_rr);
    phase_corr_ = (denom > 1e-6f) ? sum_lr / denom : 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  DRAWING
// ═══════════════════════════════════════════════════════════════════════

void VizScreen::draw(renderer::Renderer& r) {
    using namespace demod::palette;
    using namespace demod::renderer;

    int W = r.fb_w(), H = r.fb_h();

    // ── Header ───────────────────────────────────────────────────────
    r.rect_fill(0, 0, W, 14, DARK_GRAY);
    r.hline(0, W-1, 14, CYAN_DARK);
    Font::draw_glow(r, 4, 3, "VIZ", VIOLET, GLOW_VIOLET);

    // Mode tabs
    TabBar tabs;
    tabs.x = 30; tabs.y = 0; tabs.w = W - 30;
    tabs.active_tab = int(mode_);
    for (int i = 0; i < int(VizMode::VIZ_COUNT); ++i)
        tabs.tabs.push_back(viz_mode_name(VizMode(i)));
    tabs.draw(r);

    // ── Status info line ─────────────────────────────────────────────
    int info_y = 15;
    char buf[64];

    const char* trig_lbl[] = {"FREE","RISE","FALL"};
    Font::draw_string(r, 4, info_y, trig_lbl[trigger_],
                      trigger_ ? YELLOW : MID_GRAY);
    snprintf(buf, sizeof(buf), "Y:%.1fx  T:%.2fx", y_scale_, time_scale_);
    Font::draw_string(r, 50, info_y, buf, LIGHT_GRAY);
    if (freeze_) Font::draw_right(r, W-4, info_y, "[FROZEN]", ORANGE);

    snprintf(buf, sizeof(buf), "CPU:%.0f%% XR:%d",
             audio_.cpu_load()*100, audio_.xruns());
    Font::draw_right(r, W-50, info_y, buf, MID_GRAY);

    // ── Main viz area ────────────────────────────────────────────────
    int vx = 2, vy = 25;
    int vw = W - 4, vh = H - 42;

    switch (mode_) {
        case VizMode::SCOPE:       draw_scope(r, vx, vy, vw, vh); break;
        case VizMode::SPECTRUM:    draw_spectrum(r, vx, vy, vw, vh); break;
        case VizMode::WATERFALL:   draw_waterfall(r, vx, vy, vw, vh); break;
        case VizMode::LISSAJOUS:   draw_lissajous(r, vx, vy, vw, vh); break;
        case VizMode::PHASE_METER: draw_phase_meter(r, vx, vy, vw, vh); break;
        default: break;
    }

    // ── Level meters ─────────────────────────────────────────────────
    int meter_y = H - 14;
    float rms = 0;
    for (int i = 0; i < DISPLAY_BUF; ++i) rms += display_L_[i]*display_L_[i];
    rms = std::sqrt(rms / DISPLAY_BUF);
    float peak = 0;
    for (int i = 0; i < DISPLAY_BUF; ++i) peak = std::max(peak, std::fabs(display_L_[i]));

    VUMeter meter;
    meter.x = 2; meter.y = meter_y; meter.w = W - 4; meter.h = 5;
    meter.level = std::min(rms*2, 1.f); meter.peak = std::min(peak, 1.f);
    meter.vertical = false;
    meter.draw(r);

    snprintf(buf, sizeof(buf), "RMS:%.1fdB  Pk:%.1fdB",
             20*std::log10(std::max(1e-6f, rms)),
             20*std::log10(std::max(1e-6f, peak)));
    Font::draw_string(r, 4, meter_y+6, buf, MID_GRAY);
}

void VizScreen::draw_scope(renderer::Renderer& r, int x, int y, int w, int h) {
    using namespace demod::palette;
    using namespace demod::renderer;
    Scope scope;
    scope.x = x; scope.y = y; scope.w = w; scope.h = h;
    scope.label = ""; scope.buffer = display_L_; scope.buffer_len = DISPLAY_BUF;
    scope.y_scale = y_scale_; scope.trace_color = CYAN;
    scope.draw(r);
}

void VizScreen::draw_spectrum(renderer::Renderer& r, int x, int y, int w, int h) {
    using namespace demod::palette;
    using namespace demod::renderer;

    r.rect_fill(x, y, w, h, {5,5,8});
    r.rect(x, y, w, h, MID_GRAY);

    int bins = FFT_SIZE / 2;
    int bot = y + h - 2;

    // dB grid lines
    for (int db = -60; db <= 0; db += 20) {
        float norm = std::clamp((db + 80.f) / 80.f, 0.f, 1.f);
        int gy = bot - int(norm * (h-4));
        for (int gx = x+1; gx < x+w-1; gx += 6)
            r.pixel(gx, gy, {20,20,25});
        char lbl[16]; snprintf(lbl, sizeof(lbl), "%ddB", db);
        Font::draw_string(r, x+2, gy-3, lbl, {30,30,35});
    }

    // Frequency labels
    int sr = audio_.sample_rate();
    for (int f : {100, 1000, 5000, 10000, 20000}) {
        if (f > sr/2) break;
        int bin = f * bins / (sr/2);
        int bx = x + 1 + (bin * (w-2)) / bins;
        r.vline(bx, y+1, y+h-2, {20,20,25});
        char lbl[16];
        if (f >= 1000) snprintf(lbl, sizeof(lbl), "%dk", f/1000);
        else           snprintf(lbl, sizeof(lbl), "%d", f);
        Font::draw_string(r, bx+1, y+h-9, lbl, {30,30,35});
    }

    // Bars
    for (int px = 0; px < w - 2; ++px) {
        int bin = (px * bins) / (w - 2);
        if (bin >= bins) break;
        float db = 20 * std::log10(std::max(1e-6f, spectrum_[bin] * y_scale_));
        float norm = std::clamp((db + 80.f) / 80.f, 0.f, 1.f);
        int bar_h = int(norm * (h - 4));

        // Color ramp: green → yellow → red
        Color c = CYAN_DARK;
        if (norm > 0.85f)      c = RED;
        else if (norm > 0.6f)  c = ORANGE;
        else if (norm > 0.35f) c = CYAN;

        for (int by = 0; by < bar_h; ++by)
            r.pixel(x + 1 + px, bot - by, c);

        // Peak dot
        float pk_db = 20 * std::log10(std::max(1e-6f, spectrum_peak_[bin] * y_scale_));
        float pk_n = std::clamp((pk_db + 80.f) / 80.f, 0.f, 1.f);
        int pk_y = bot - int(pk_n * (h-4));
        r.pixel(x + 1 + px, pk_y, WHITE);
    }
}

void VizScreen::draw_waterfall(renderer::Renderer& r, int x, int y, int w, int h) {
    using namespace demod::palette;
    using namespace demod::renderer;

    r.rect_fill(x, y, w, h, {5,5,8});
    r.rect(x, y, w, h, MID_GRAY);

    int rows = std::min(WF_ROWS, h - 2);
    int cols = std::min(WF_COLS, w - 2);

    for (int ry = 0; ry < rows; ++ry) {
        int src_row = (wf_row_ - rows + ry + WF_ROWS) % WF_ROWS;
        for (int cx = 0; cx < cols; ++cx) {
            uint8_t val = waterfall_[src_row][cx * WF_COLS / cols];
            // Colormap: black → violet → cyan → white
            float t = val / 255.f;
            Color c = (t < 0.33f)
                ? BLACK.lerp(VIOLET_DARK, t / 0.33f)
                : (t < 0.66f)
                    ? VIOLET_DARK.lerp(CYAN_MID, (t - 0.33f) / 0.33f)
                    : CYAN_MID.lerp(WHITE, (t - 0.66f) / 0.34f);

            r.pixel(x + 1 + cx * (w-2) / cols, y + 1 + ry, c);
        }
    }

    Font::draw_string(r, x+3, y+2, "WATERFALL", CYAN_DARK);
}

void VizScreen::draw_lissajous(renderer::Renderer& r, int x, int y, int w, int h) {
    using namespace demod::palette;
    using namespace demod::renderer;

    int sz = std::min(w, h);
    int ox = x + (w - sz) / 2;
    int oy = y + (h - sz) / 2;

    r.rect_fill(ox, oy, sz, sz, {5,5,8});
    r.rect(ox, oy, sz, sz, MID_GRAY);

    // Center crosshairs
    int cx = ox + sz/2, cy = oy + sz/2;
    for (int i = ox+1; i < ox+sz-1; i += 4) r.pixel(i, cy, {20,20,25});
    for (int i = oy+1; i < oy+sz-1; i += 4) r.pixel(cx, i, {20,20,25});

    // Diamond (mono = diagonal)
    int half = sz/2 - 2;
    r.line(cx-half, cy, cx, cy-half, {25,25,30});
    r.line(cx+half, cy, cx, cy-half, {25,25,30});
    r.line(cx-half, cy, cx, cy+half, {25,25,30});
    r.line(cx+half, cy, cx, cy+half, {25,25,30});

    // Plot L vs R
    for (int i = 0; i < DISPLAY_BUF; ++i) {
        float lv = display_L_[i] * y_scale_;
        float rv = display_R_[i] * y_scale_;
        int px = cx + int(lv * half);
        int py = cy - int(rv * half);
        px = std::clamp(px, ox+1, ox+sz-2);
        py = std::clamp(py, oy+1, oy+sz-2);

        // Fade by index (newer = brighter)
        float fade = float(i) / DISPLAY_BUF;
        Color c = VIOLET.lerp(CYAN, fade);
        r.pixel(px, py, c);
        r.blend_pixel(px, py-1, c.with_alpha(30));
        r.blend_pixel(px, py+1, c.with_alpha(30));
    }

    // Labels
    Font::draw_string(r, ox+2, oy+2, "L/R LISSAJOUS", CYAN_DARK);
    char buf[32]; snprintf(buf, sizeof(buf), "Corr: %.2f", phase_corr_);
    Font::draw_right(r, ox+sz-2, oy+2, buf, phase_corr_ > 0 ? GREEN : RED);
}

void VizScreen::draw_phase_meter(renderer::Renderer& r, int x, int y, int w, int h) {
    using namespace demod::palette;
    using namespace demod::renderer;

    r.rect_fill(x, y, w, h, {5,5,8});
    r.rect(x, y, w, h, MID_GRAY);

    int cy = y + 20;

    Font::draw_centered(r, x, y+2, w, "PHASE CORRELATION", CYAN_DARK);

    // Big meter: -1 (out of phase) to +1 (mono)
    int bar_w = w - 20;
    int bar_x = x + 10;
    int bar_y = cy + 10;
    int bar_h = 12;

    r.rect(bar_x, bar_y, bar_w, bar_h, MID_GRAY);

    // Center line
    r.vline(bar_x + bar_w/2, bar_y, bar_y+bar_h-1, LIGHT_GRAY);

    // Fill from center
    float norm = phase_corr_ * 0.5f + 0.5f; // 0..1 where 0.5 = zero
    int fill_center = bar_x + bar_w/2;
    int fill_end = bar_x + int(norm * bar_w);

    Color fill_c = phase_corr_ > 0 ? GREEN : RED;
    int f0 = std::min(fill_center, fill_end);
    int f1 = std::max(fill_center, fill_end);
    r.rect_fill(f0, bar_y+1, f1-f0, bar_h-2, fill_c);

    // Labels
    Font::draw_string(r, bar_x, bar_y-8, "-1", RED);
    Font::draw_centered(r, bar_x, bar_y-8, bar_w, "0", MID_GRAY);
    Font::draw_right(r, bar_x+bar_w, bar_y-8, "+1", GREEN);

    // Large numeric readout
    char buf[16]; snprintf(buf, sizeof(buf), "%+.3f", phase_corr_);
    Font::draw_centered(r, x, bar_y+bar_h+10, w, buf,
                        phase_corr_ > 0 ? GREEN : RED);

    // Stereo balance meters
    int meter_base = bar_y + bar_h + 30;

    float rms_l = 0, rms_r = 0;
    for (int i = 0; i < DISPLAY_BUF; ++i) {
        rms_l += display_L_[i] * display_L_[i];
        rms_r += display_R_[i] * display_R_[i];
    }
    rms_l = std::sqrt(rms_l / DISPLAY_BUF);
    rms_r = std::sqrt(rms_r / DISPLAY_BUF);

    Font::draw_string(r, x+4, meter_base, "L", LIGHT_GRAY);
    VUMeter ml;
    ml.x = x+12; ml.y = meter_base; ml.w = w-16; ml.h = 6;
    ml.level = std::min(rms_l*2, 1.f); ml.peak = 0; ml.vertical = false;
    ml.draw(r);

    Font::draw_string(r, x+4, meter_base+10, "R", LIGHT_GRAY);
    VUMeter mr;
    mr.x = x+12; mr.y = meter_base+10; mr.w = w-16; mr.h = 6;
    mr.level = std::min(rms_r*2, 1.f); mr.peak = 0; mr.vertical = false;
    mr.draw(r);

    // Balance indicator
    float balance = (rms_l + rms_r > 1e-6f)
                    ? (rms_r - rms_l) / (rms_l + rms_r) : 0;
    snprintf(buf, sizeof(buf), "Balance: %+.1f%%", balance*100);
    Font::draw_centered(r, x, meter_base+22, w, buf, LIGHT_GRAY);
}

} // namespace demod::ui
