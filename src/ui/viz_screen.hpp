#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Visualization Screen                                   ║
// ║  5 modes: Scope, Spectrum, Waterfall, Lissajous, Phase Meter       ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/screen.hpp"
#include "audio/audio_engine.hpp"

namespace demod::ui {

class VizScreen : public Screen {
public:
    VizScreen(audio::AudioEngine& audio);

    std::string name() const override { return "VIZ"; }
    std::string help_text() const override {
        return "[/]:Mode  U/D:YScale  L/R:TScale  Space:Freeze  Enter:Trigger";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;

private:
    audio::AudioEngine& audio_;

    VizMode mode_ = VizMode::SCOPE;

    // Scope state
    float y_scale_    = 1.0f;
    float time_scale_ = 1.0f;
    int   trigger_    = 0;  // 0=free, 1=rise, 2=fall
    bool  freeze_     = false;

    static constexpr int DISPLAY_BUF = 1024;
    float display_L_[DISPLAY_BUF] = {};
    float display_R_[DISPLAY_BUF] = {};

    // Spectrum state
    static constexpr int FFT_SIZE = 512;
    float spectrum_[FFT_SIZE/2] = {};
    float spectrum_peak_[FFT_SIZE/2] = {};

    // Waterfall state
    static constexpr int WF_ROWS = 128;
    static constexpr int WF_COLS = 256;
    uint8_t waterfall_[WF_ROWS][WF_COLS] = {};
    int wf_row_ = 0;

    // Lissajous / phase state
    float phase_corr_ = 0;

    float anim_t_ = 0;

    void capture_waveform();
    void compute_spectrum();
    void update_waterfall();
    void compute_phase();

    void draw_scope(renderer::Renderer& r, int x, int y, int w, int h);
    void draw_spectrum(renderer::Renderer& r, int x, int y, int w, int h);
    void draw_waterfall(renderer::Renderer& r, int x, int y, int w, int h);
    void draw_lissajous(renderer::Renderer& r, int x, int y, int w, int h);
    void draw_phase_meter(renderer::Renderer& r, int x, int y, int w, int h);

    int find_trigger(const float* buf, int len) const;

    // Simple DFT (no FFTW dependency)
    void dft_magnitude(const float* in, float* mag, int n);
};

} // namespace demod::ui
