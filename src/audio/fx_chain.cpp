// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — FX Chain Processor Implementation                      ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "audio/fx_chain.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace demod::audio {

FXChainProcessor::FXChainProcessor() {
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        loaded_[i].store(false, std::memory_order_relaxed);
        bypassed_[i].store(true, std::memory_order_relaxed);  // Bypassed by default
        wet_mix_[i].store(1.0f, std::memory_order_relaxed);
        order_[i].store(i, std::memory_order_relaxed);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  SLOT OPERATIONS (UI thread)
// ═══════════════════════════════════════════════════════════════════════

bool FXChainProcessor::load_slot(int index, const std::string& dsp_path) {
    if (index < 0 || index >= MAX_FX_SLOTS) return false;

    std::lock_guard<std::mutex> lock(slot_mutex_);

    // Unload existing
    bridges_[index].unload();
    loaded_[index].store(false, std::memory_order_release);

    // Determine load method by extension
    bool ok = false;
    auto dot = dsp_path.rfind('.');
    std::string ext = (dot != std::string::npos) ? dsp_path.substr(dot) : "";

    if (ext == ".dsp")
        ok = bridges_[index].load_dsp_source(dsp_path, sample_rate_);
    else if (ext == ".cpp" || ext == ".cxx")
        ok = bridges_[index].load_dsp_cpp(dsp_path, sample_rate_);
    else if (ext == ".so" || ext == ".dylib")
        ok = bridges_[index].load_dsp_library(dsp_path, sample_rate_);

    if (ok) {
        loaded_[index].store(true, std::memory_order_release);
        fprintf(stderr, "[FX] Slot %d loaded: %s (%s)\n",
                index, dsp_path.c_str(), bridges_[index].dsp_name().c_str());
    } else {
        fprintf(stderr, "[FX] Slot %d load failed: %s\n",
                index, dsp_path.c_str());
    }

    return ok;
}

void FXChainProcessor::unload_slot(int index) {
    if (index < 0 || index >= MAX_FX_SLOTS) return;

    std::lock_guard<std::mutex> lock(slot_mutex_);
    loaded_[index].store(false, std::memory_order_release);
    bridges_[index].unload();
}

void FXChainProcessor::unload_all() {
    std::lock_guard<std::mutex> lock(slot_mutex_);
    for (int i = 0; i < MAX_FX_SLOTS; ++i) {
        loaded_[i].store(false, std::memory_order_release);
        bridges_[i].unload();
    }
}

void FXChainProcessor::set_slot_bypassed(int index, bool bypass) {
    if (index >= 0 && index < MAX_FX_SLOTS)
        bypassed_[index].store(bypass, std::memory_order_release);
}

void FXChainProcessor::set_slot_wet_mix(int index, float wet) {
    if (index >= 0 && index < MAX_FX_SLOTS)
        wet_mix_[index].store(std::clamp(wet, 0.0f, 1.0f),
                              std::memory_order_release);
}

void FXChainProcessor::swap_slots(int a, int b) {
    if (a < 0 || a >= MAX_FX_SLOTS || b < 0 || b >= MAX_FX_SLOTS || a == b)
        return;

    std::lock_guard<std::mutex> lock(slot_mutex_);

    // Swap bridge contents via unload/load (bridges contain non-movable mutex)
    std::string path_a = bridges_[a].loaded() ? bridges_[a].dsp_name() : "";
    std::string path_b = bridges_[b].loaded() ? bridges_[b].dsp_name() : "";
    bool la = loaded_[a].load(std::memory_order_relaxed);
    bool lb = loaded_[b].load(std::memory_order_relaxed);

    if (la) bridges_[a].unload();
    if (lb) bridges_[b].unload();
    if (lb && !path_b.empty()) bridges_[a].load_dsp_source(path_b, sample_rate_);
    if (la && !path_a.empty()) bridges_[b].load_dsp_source(path_a, sample_rate_);

    // Swap atomic state
    loaded_[a].store(lb, std::memory_order_release);
    loaded_[b].store(la, std::memory_order_release);

    bool ba = bypassed_[a].load(std::memory_order_relaxed);
    bool bb = bypassed_[b].load(std::memory_order_relaxed);
    bypassed_[a].store(bb, std::memory_order_release);
    bypassed_[b].store(ba, std::memory_order_release);

    float wa = wet_mix_[a].load(std::memory_order_relaxed);
    float wb = wet_mix_[b].load(std::memory_order_relaxed);
    wet_mix_[a].store(wb, std::memory_order_release);
    wet_mix_[b].store(wa, std::memory_order_release);
}

// ═══════════════════════════════════════════════════════════════════════
//  QUERY (UI thread)
// ═══════════════════════════════════════════════════════════════════════

bool FXChainProcessor::slot_loaded(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return false;
    return loaded_[index].load(std::memory_order_acquire);
}

bool FXChainProcessor::slot_bypassed(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return true;
    return bypassed_[index].load(std::memory_order_acquire);
}

float FXChainProcessor::slot_wet_mix(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return 0.0f;
    return wet_mix_[index].load(std::memory_order_acquire);
}

std::string FXChainProcessor::slot_dsp_path(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return "";
    std::lock_guard<std::mutex> lock(slot_mutex_);
    return bridges_[index].loaded() ? bridges_[index].dsp_name() : "";
}

std::string FXChainProcessor::slot_dsp_name(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return "";
    std::lock_guard<std::mutex> lock(slot_mutex_);
    return bridges_[index].dsp_name();
}

int FXChainProcessor::slot_num_params(int index) const {
    if (index < 0 || index >= MAX_FX_SLOTS) return 0;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    return bridges_[index].num_params();
}

float FXChainProcessor::get_slot_param(int slot, int param_index) const {
    if (slot < 0 || slot >= MAX_FX_SLOTS) return 0.0f;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    return bridges_[slot].get_param(param_index);
}

void FXChainProcessor::set_slot_param(int slot, int param_index, float value) {
    if (slot < 0 || slot >= MAX_FX_SLOTS) return;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    bridges_[slot].set_param(param_index, value);
}

const std::vector<ParamDescriptor>& FXChainProcessor::slot_params(int slot) const {
    static const std::vector<ParamDescriptor> empty;
    if (slot < 0 || slot >= MAX_FX_SLOTS) return empty;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    return bridges_[slot].params();
}

void FXChainProcessor::randomize_slot_params(int slot) {
    if (slot < 0 || slot >= MAX_FX_SLOTS) return;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    bridges_[slot].randomize_params();
}

void FXChainProcessor::reset_slot_params(int slot) {
    if (slot < 0 || slot >= MAX_FX_SLOTS) return;
    std::lock_guard<std::mutex> lock(slot_mutex_);
    bridges_[slot].reset_params();
}

// ═══════════════════════════════════════════════════════════════════════
//  AUDIO PROCESSING (audio RT thread)
// ═══════════════════════════════════════════════════════════════════════

void FXChainProcessor::process_serial(float* interleaved_buf,
                                       int n_channels, int n_frames) {
    for (int slot = 0; slot < MAX_FX_SLOTS; ++slot) {
        bool is_loaded  = loaded_[slot].load(std::memory_order_acquire);
        bool is_bypassed = bypassed_[slot].load(std::memory_order_acquire);

        if (!is_loaded || is_bypassed) continue;

        float wet = wet_mix_[slot].load(std::memory_order_acquire);
        if (wet <= 0.0001f) continue;

        std::lock_guard<std::mutex> lock(slot_mutex_);
        if (!bridges_[slot].loaded()) continue;

        if (wet >= 0.9999f) {
            // Fully wet — process in-place
            bridges_[slot].process_interleaved(interleaved_buf, n_frames);
        } else {
            // Wet/dry blend
            int total_samples = n_frames * n_channels;
            dry_buf_.resize(total_samples);
            std::memcpy(dry_buf_.data(), interleaved_buf,
                        total_samples * sizeof(float));

            bridges_[slot].process_interleaved(interleaved_buf, n_frames);

            float dry = 1.0f - wet;
            for (int i = 0; i < total_samples; ++i) {
                interleaved_buf[i] = dry_buf_[i] * dry +
                                     interleaved_buf[i] * wet;
            }
        }
    }
}

void FXChainProcessor::process_serial(const float* const* inputs,
                                       float* interleaved_out,
                                       int n_channels, int n_frames) {
    // First slot receives input, rest chain in-place
    for (int slot = 0; slot < MAX_FX_SLOTS; ++slot) {
        bool is_loaded  = loaded_[slot].load(std::memory_order_acquire);
        bool is_bypassed = bypassed_[slot].load(std::memory_order_acquire);

        if (!is_loaded || is_bypassed) continue;

        float wet = wet_mix_[slot].load(std::memory_order_acquire);
        if (wet <= 0.0001f) continue;

        std::lock_guard<std::mutex> lock(slot_mutex_);
        if (!bridges_[slot].loaded()) continue;

        if (slot == 0 && inputs) {
            // First slot with input
            if (wet >= 0.9999f) {
                bridges_[slot].process_interleaved(inputs, interleaved_out, n_frames);
            } else {
                int total_samples = n_frames * n_channels;
                dry_buf_.resize(total_samples);
                std::memset(interleaved_out, 0, total_samples * sizeof(float));

                // Copy first channel of input as dry signal
                for (int ch = 0; ch < n_channels && ch < 1; ++ch) {
                    for (int i = 0; i < n_frames; ++i)
                        dry_buf_[i * n_channels + ch] = inputs[ch][i];
                }

                bridges_[slot].process_interleaved(inputs, interleaved_out, n_frames);

                float dry = 1.0f - wet;
                for (int i = 0; i < total_samples; ++i) {
                    interleaved_out[i] = dry_buf_[i] * dry +
                                         interleaved_out[i] * wet;
                }
            }
        } else {
            // Subsequent slots process in-place
            if (wet >= 0.9999f) {
                bridges_[slot].process_interleaved(interleaved_out, n_frames);
            } else {
                int total_samples = n_frames * n_channels;
                dry_buf_.resize(total_samples);
                std::memcpy(dry_buf_.data(), interleaved_out,
                            total_samples * sizeof(float));

                bridges_[slot].process_interleaved(interleaved_out, n_frames);

                float dry = 1.0f - wet;
                for (int i = 0; i < total_samples; ++i) {
                    interleaved_out[i] = dry_buf_[i] * dry +
                                         interleaved_out[i] * wet;
                }
            }
        }
    }
}

} // namespace demod::audio
