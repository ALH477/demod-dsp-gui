#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoD DSP GUI — PipeWire Audio Engine                             ║
// ║                                                                    ║
// ║  Real-time audio I/O via PipeWire native API.                      ║
// ║  Provides a callback-based interface that the FaustBridge hooks    ║
// ║  into for DSP processing.                                          ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/config.hpp"
#include <functional>
#include <atomic>
#include <string>
#include <vector>
#include <thread>
#include <cstdint>

// Forward declarations for PipeWire types
struct pw_main_loop;
struct pw_stream;
struct spa_hook;

namespace demod::audio {

// Audio processing callback signature:
//   (input_buffers, output_buffers, num_channels, num_frames)
using AudioCallback = std::function<void(
    const float* const*, float* const*, int, int)>;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Configure before starting
    void set_sample_rate(int rate)    { sample_rate_ = rate; }
    void set_block_size(int size)     { block_size_  = size; }
    void set_channels(int in, int out){ n_inputs_ = in; n_outputs_ = out; }
    void set_callback(AudioCallback cb) { callback_ = std::move(cb); }

    // Lifecycle
    bool start(const std::string& client_name = "demod-dsp");
    void stop();
    bool running() const { return running_.load(); }

    // Input stream control — start paused, activate when DSP needs input
    void activate_input();
    void deactivate_input();
    bool input_active() const { return input_active_.load(); }

    // Monitoring
    float cpu_load() const { return cpu_load_.load(); }
    int   xruns()    const { return xrun_count_.load(); }
    int   sample_rate() const { return sample_rate_; }

    // Ring buffers for scope display (lock-free, written from audio thread)
    static constexpr int SCOPE_BUF_SIZE = 2048;
    float scope_buffer[SCOPE_BUF_SIZE] = {};    // Channel 0 (L)
    float scope_buffer_R[SCOPE_BUF_SIZE] = {};  // Channel 1 (R)
    std::atomic<int> scope_write_pos{0};

    // PipeWire C-API trampolines (public so the C lambda wrappers can reach them)
    struct HookData;
    static void on_process(void* userdata);
    static void on_input_process(void* userdata);
    static void on_state_changed(void* userdata, uint32_t old_state,
                                  uint32_t state, const char* error);

private:
    int sample_rate_ = AUDIO_RATE;
    int block_size_  = AUDIO_BLOCKSIZE;
    int n_inputs_    = 0;
    int n_outputs_   = 2;

    AudioCallback callback_;

    std::atomic<bool>  running_{false};
    std::atomic<bool>  input_active_{false};
    std::atomic<float> cpu_load_{0.0f};
    std::atomic<int>   xrun_count_{0};

    // PipeWire internals
    pw_main_loop* loop_   = nullptr;
    pw_stream*    stream_ = nullptr;        // Output stream
    pw_stream*    input_stream_ = nullptr;  // Input capture stream
    std::thread   pw_thread_;

    // SPA hook storage (must outlive the stream)
    HookData* hook_data_       = nullptr;
    HookData* input_hook_data_ = nullptr;

    // Lock-free ring buffer for input capture (interleaved, multi-channel)
    static constexpr int INPUT_RING_SIZE = 16384;  // ~341ms at 48kHz stereo
    std::vector<float> input_ring_;
    std::atomic<int>   input_write_pos_{0};
    std::atomic<int>   input_read_pos_{0};

    // Deinterleaved input buffers for the callback
    std::vector<std::vector<float>> input_bufs_;

    void pw_thread_func();
};

} // namespace demod::audio
