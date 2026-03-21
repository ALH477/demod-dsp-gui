#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — FX Chain Processor                                     ║
// ║  Per-slot FaustBridge with lock-free audio thread processing.      ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/config.hpp"
#include "audio/faust_bridge.hpp"
#include <array>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

namespace demod::audio {

class FXChainProcessor {
public:
    FXChainProcessor();

    void set_sample_rate(int rate) { sample_rate_ = rate; }
    int  sample_rate() const { return sample_rate_; }

    // ── Slot operations (UI thread only) ─────────────────────────────
    bool load_slot(int index, const std::string& dsp_path);
    void unload_slot(int index);
    void unload_all();

    void set_slot_bypassed(int index, bool bypass);
    void set_slot_wet_mix(int index, float wet);
    void swap_slots(int a, int b);

    // ── Query (UI thread) ────────────────────────────────────────────
    bool slot_loaded(int index) const;
    bool slot_bypassed(int index) const;
    float slot_wet_mix(int index) const;
    std::string slot_dsp_path(int index) const;
    std::string slot_dsp_name(int index) const;
    int slot_num_params(int index) const;

    // Per-slot parameter access (UI thread)
    float get_slot_param(int slot, int param_index) const;
    void  set_slot_param(int slot, int param_index, float value);
    const std::vector<ParamDescriptor>& slot_params(int slot) const;
    void randomize_slot_params(int slot);
    void reset_slot_params(int slot);

    // ── Audio processing (audio RT thread only) ──────────────────────
    // Processes all slots in series with bypass and wet/dry.
    // Input/output are interleaved. Input is overwritten with output.
    void process_serial(float* interleaved_buf, int n_channels, int n_frames);
    void process_serial(const float* const* inputs, float* interleaved_out,
                        int n_channels, int n_frames);

private:
    int sample_rate_ = AUDIO_RATE;

    // Per-slot DSP (owned, UI thread manages lifetime)
    std::array<FaustBridge, MAX_FX_SLOTS> bridges_;

    // Lock-free slot state (written by UI thread, read by audio thread)
    // Each field is an independent atomic — no compound reads needed.
    std::array<std::atomic<bool>,  MAX_FX_SLOTS> loaded_{};
    std::array<std::atomic<bool>,  MAX_FX_SLOTS> bypassed_{};
    std::array<std::atomic<float>, MAX_FX_SLOTS> wet_mix_{};

    // Slot ordering (UI thread writes, audio thread reads atomically)
    // Maps processing position → slot index
    std::array<std::atomic<int>, MAX_FX_SLOTS> order_{};

    // For wet/dry blending: need to copy input before processing
    std::vector<float> dry_buf_;

    mutable std::mutex slot_mutex_;  // Protects bridges_ for load/unload
};

} // namespace demod::audio
