// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoD DSP GUI — PipeWire Audio Engine Implementation              ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "audio/audio_engine.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>

#include <cstring>
#include <cstdio>
#include <chrono>
#include <xmmintrin.h>  // SSE: _mm_setcsr, _mm_getcsr
#include <pmmintrin.h>  // SSE3: _MM_SET_FLUSH_TO_ZERO, _MM_SET_DENORMALS_ZERO

namespace demod::audio {

struct AudioEngine::HookData {
    AudioEngine*   engine;
    struct spa_hook listener;
};

static struct pw_stream_events make_stream_events() {
    struct pw_stream_events ev = {};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.state_changed = [](void* data, enum pw_stream_state old,
                        enum pw_stream_state state, const char* error) {
        auto* hd = static_cast<AudioEngine::HookData*>(data);
        AudioEngine::on_state_changed(hd, uint32_t(old),
                                       uint32_t(state), error);
    };
    ev.process = [](void* data) {
        auto* hd = static_cast<AudioEngine::HookData*>(data);
        AudioEngine::on_process(hd);
    };
    return ev;
}
static const struct pw_stream_events stream_events = make_stream_events();

static struct pw_stream_events make_input_stream_events() {
    struct pw_stream_events ev = {};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.state_changed = [](void* data, enum pw_stream_state old,
                        enum pw_stream_state state, const char* error) {
        auto* hd = static_cast<AudioEngine::HookData*>(data);
        AudioEngine::on_state_changed(hd, uint32_t(old),
                                       uint32_t(state), error);
    };
    ev.process = [](void* data) {
        auto* hd = static_cast<AudioEngine::HookData*>(data);
        AudioEngine::on_input_process(hd);
    };
    return ev;
}
static const struct pw_stream_events input_stream_events = make_input_stream_events();

AudioEngine::AudioEngine() {
    pw_init(nullptr, nullptr);
}

AudioEngine::~AudioEngine() {
    stop();
    pw_deinit();
}

bool AudioEngine::start(const std::string& client_name) {
    if (running_) return true;

    hook_data_ = new HookData{this, {}};

    // Create PipeWire main loop in its own thread
    loop_ = pw_main_loop_new(nullptr);
    if (!loop_) {
        fprintf(stderr, "[AUDIO] Failed to create PipeWire main loop\n");
        return false;
    }

    auto props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_APP_NAME, client_name.c_str(),
        PW_KEY_NODE_NAME, client_name.c_str(),
        PW_KEY_NODE_LATENCY, (std::to_string(block_size_) + "/" +
                              std::to_string(sample_rate_)).c_str(),
        nullptr
    );

    stream_ = pw_stream_new_simple(
        pw_main_loop_get_loop(loop_),
        client_name.c_str(),
        props,
        &stream_events,
        hook_data_
    );

    if (!stream_) {
        fprintf(stderr, "[AUDIO] Failed to create PipeWire stream\n");
        return false;
    }

    // Build audio format params
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    struct spa_audio_info_raw info = {};
    info.format   = SPA_AUDIO_FORMAT_F32;
    info.rate     = (uint32_t)sample_rate_;
    info.channels = (uint32_t)n_outputs_;

    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int res = pw_stream_connect(
        stream_,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                          PW_STREAM_FLAG_MAP_BUFFERS |
                          PW_STREAM_FLAG_RT_PROCESS),
        params, 1
    );

    if (res < 0) {
        fprintf(stderr, "[AUDIO] Stream connect failed: %s\n",
                spa_strerror(res));
        return false;
    }

    // ── Input capture stream ────────────────────────────────────────
    if (n_inputs_ > 0) {
        input_hook_data_ = new HookData{this, {}};

        auto in_props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_APP_NAME, client_name.c_str(),
            PW_KEY_NODE_NAME, (client_name + "_in").c_str(),
            PW_KEY_NODE_LATENCY, (std::to_string(block_size_) + "/" +
                                  std::to_string(sample_rate_)).c_str(),
            nullptr
        );

        input_stream_ = pw_stream_new_simple(
            pw_main_loop_get_loop(loop_),
            (client_name + "_in").c_str(),
            in_props,
            &input_stream_events,
            input_hook_data_
        );

        if (!input_stream_) {
            fprintf(stderr, "[AUDIO] Failed to create input stream\n");
        } else {
            uint8_t in_buf[1024];
            struct spa_pod_builder ib = SPA_POD_BUILDER_INIT(in_buf, sizeof(in_buf));

            struct spa_audio_info_raw in_info = {};
            in_info.format   = SPA_AUDIO_FORMAT_F32;
            in_info.rate     = (uint32_t)sample_rate_;
            in_info.channels = (uint32_t)n_inputs_;

            const struct spa_pod* in_params[1];
            in_params[0] = spa_format_audio_raw_build(&ib, SPA_PARAM_EnumFormat, &in_info);

            int in_res = pw_stream_connect(
                input_stream_,
                PW_DIRECTION_INPUT,
                PW_ID_ANY,
                (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                  PW_STREAM_FLAG_MAP_BUFFERS |
                                  PW_STREAM_FLAG_RT_PROCESS |
                                  PW_STREAM_FLAG_INACTIVE),  // Start paused
                in_params, 1
            );

            if (in_res < 0) {
                fprintf(stderr, "[AUDIO] Input stream connect failed: %s\n",
                        spa_strerror(in_res));
                pw_stream_destroy(input_stream_);
                input_stream_ = nullptr;
            } else {
                // Allocate ring buffer and deinterleave buffers
                input_ring_.resize(INPUT_RING_SIZE * n_inputs_, 0.0f);
                input_bufs_.resize(n_inputs_);
                for (auto& buf : input_bufs_)
                    buf.resize(block_size_ * 2, 0.0f);
                fprintf(stderr, "[AUDIO] Input stream: %d ch (inactive)\n", n_inputs_);
            }
        }
    }

    running_ = true;

    // Run PipeWire loop in dedicated thread
    pw_thread_ = std::thread(&AudioEngine::pw_thread_func, this);

    fprintf(stderr, "[AUDIO] Started: %d Hz, %d ch, %d block\n",
            sample_rate_, n_outputs_, block_size_);
    return true;
}

void AudioEngine::stop() {
    if (!running_) return;
    running_ = false;

    if (loop_) {
        pw_main_loop_quit(loop_);
    }

    if (pw_thread_.joinable()) {
        pw_thread_.join();
    }

    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (input_stream_) {
        pw_stream_destroy(input_stream_);
        input_stream_ = nullptr;
    }
    if (loop_) {
        pw_main_loop_destroy(loop_);
        loop_ = nullptr;
    }
    if (hook_data_) {
        delete hook_data_;
        hook_data_ = nullptr;
    }
    if (input_hook_data_) {
        delete input_hook_data_;
        input_hook_data_ = nullptr;
    }
}

void AudioEngine::activate_input() {
    if (!input_stream_) return;
    input_active_.store(true, std::memory_order_release);
    pw_stream_set_active(input_stream_, true);
    fprintf(stderr, "[AUDIO] Input stream activated\n");
}

void AudioEngine::deactivate_input() {
    if (!input_stream_) return;
    input_active_.store(false, std::memory_order_release);
    pw_stream_set_active(input_stream_, false);
    // Clear ring buffer to avoid stale data
    input_read_pos_.store(0, std::memory_order_relaxed);
    input_write_pos_.store(0, std::memory_order_relaxed);
    fprintf(stderr, "[AUDIO] Input stream deactivated\n");
}

void AudioEngine::pw_thread_func() {
    // Enable Flush-to-Zero and Denormals-Are-Zero on this thread.
    // Denormal floating-point numbers cause 10-100x slowdown in the
    // hardware pipeline; this makes them silently become zero instead.
    unsigned int mxcsr = _mm_getcsr();
    mxcsr |= (1 << 15) | (1 << 6);  // FTZ (bit 15) + DAZ (bit 6)
    _mm_setcsr(mxcsr);

    pw_main_loop_run(loop_);
}

void AudioEngine::on_input_process(void* userdata) {
    auto* hd = static_cast<HookData*>(userdata);
    auto* engine = hd->engine;

    if (!engine->input_stream_ || !engine->input_active_.load(std::memory_order_relaxed))
        return;

    struct pw_buffer* pwbuf = pw_stream_dequeue_buffer(engine->input_stream_);
    if (!pwbuf) return;

    struct spa_buffer* spa_buf = pwbuf->buffer;
    float* src = nullptr;
    int n_frames = 0;

    if (spa_buf->datas[0].data) {
        src = static_cast<float*>(spa_buf->datas[0].data);
        n_frames = spa_buf->datas[0].maxsize /
                   (sizeof(float) * engine->n_inputs_);
        if (pwbuf->requested > 0 && (int)pwbuf->requested < n_frames) {
            n_frames = (int)pwbuf->requested;
        }
    }

    if (src && n_frames > 0 && engine->n_inputs_ > 0) {
        int ring_size = INPUT_RING_SIZE;
        int channels  = engine->n_inputs_;
        int write_pos = engine->input_write_pos_.load(std::memory_order_relaxed);

        for (int i = 0; i < n_frames; ++i) {
            int ring_idx = ((write_pos + i) % ring_size) * channels;
            for (int ch = 0; ch < channels; ++ch) {
                engine->input_ring_[ring_idx + ch] =
                    src[i * channels + ch];
            }
        }

        engine->input_write_pos_.store(
            (write_pos + n_frames) % ring_size, std::memory_order_release);
    }

    pw_stream_queue_buffer(engine->input_stream_, pwbuf);
}

void AudioEngine::on_process(void* userdata) {
    auto* hd = static_cast<HookData*>(userdata);
    auto* engine = hd->engine;

    struct pw_buffer* pwbuf = pw_stream_dequeue_buffer(engine->stream_);
    if (!pwbuf) return;

    struct spa_buffer* spa_buf = pwbuf->buffer;
    float* dst = nullptr;
    int n_frames = 0;

    if (spa_buf->datas[0].data) {
        dst = static_cast<float*>(spa_buf->datas[0].data);
        n_frames = spa_buf->datas[0].maxsize /
                   (sizeof(float) * engine->n_outputs_);
        if (pwbuf->requested > 0 && (int)pwbuf->requested < n_frames) {
            n_frames = (int)pwbuf->requested;
        }
    }

    if (!dst || n_frames <= 0) {
        pw_stream_queue_buffer(engine->stream_, pwbuf);
        return;
    }

    // Measure processing time for CPU load
    auto t0 = std::chrono::high_resolution_clock::now();

    // Read input from ring buffer (if input stream exists)
    bool has_input = false;
    if (engine->input_stream_ && engine->n_inputs_ > 0 &&
        !engine->input_ring_.empty()) {
        int ring_size = INPUT_RING_SIZE;
        int channels  = engine->n_inputs_;
        int read_pos  = engine->input_read_pos_.load(std::memory_order_acquire);
        int write_pos = engine->input_write_pos_.load(std::memory_order_acquire);

        // Available samples in ring buffer
        int available = (write_pos - read_pos + ring_size) % ring_size;
        int to_read   = std::min(n_frames, available);

        // Ensure deinterleave buffers are big enough
        for (int ch = 0; ch < channels; ++ch) {
            if ((int)engine->input_bufs_[ch].size() < n_frames)
                engine->input_bufs_[ch].resize(n_frames, 0.0f);
        }

        // Deinterleave from ring buffer
        for (int i = 0; i < to_read; ++i) {
            int ring_idx = ((read_pos + i) % ring_size) * channels;
            for (int ch = 0; ch < channels; ++ch) {
                engine->input_bufs_[ch][i] =
                    engine->input_ring_[ring_idx + ch];
            }
        }
        // Zero-fill any underrun samples
        for (int i = to_read; i < n_frames; ++i) {
            for (int ch = 0; ch < channels; ++ch)
                engine->input_bufs_[ch][i] = 0.0f;
        }

        engine->input_read_pos_.store(
            (read_pos + to_read) % ring_size, std::memory_order_release);

        has_input = to_read > 0;
    }

    // Call the DSP callback
    if (engine->callback_) {
        float* outputs[] = { dst };

        if (has_input && engine->n_inputs_ > 0) {
            // Build non-interleaved input pointer array
            const float* in_ptrs[32] = {};
            for (int ch = 0; ch < std::min(engine->n_inputs_, 32); ++ch)
                in_ptrs[ch] = engine->input_bufs_[ch].data();
            engine->callback_(in_ptrs, outputs, engine->n_outputs_, n_frames);
        } else {
            engine->callback_(nullptr, outputs, engine->n_outputs_, n_frames);
        }
    } else {
        // Silence
        std::memset(dst, 0, n_frames * engine->n_outputs_ * sizeof(float));
    }

    // Copy to scope ring buffers (channel 0 and 1)
    int n_ch_out = engine->n_outputs_;
    for (int i = 0; i < n_frames; ++i) {
        int pos = engine->scope_write_pos.load(std::memory_order_relaxed);
        int idx = pos % SCOPE_BUF_SIZE;
        engine->scope_buffer[idx]   = dst[i * n_ch_out];       // Channel 0 (L)
        engine->scope_buffer_R[idx] = (n_ch_out >= 2)
            ? dst[i * n_ch_out + 1]                            // Channel 1 (R)
            : dst[i * n_ch_out];                               // Mono fallback
        engine->scope_write_pos.store(
            (pos + 1) % SCOPE_BUF_SIZE, std::memory_order_relaxed);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    double budget  = double(n_frames) / engine->sample_rate_;
    engine->cpu_load_.store(float(elapsed / budget),
                            std::memory_order_relaxed);

    spa_buf->datas[0].chunk->offset = 0;
    spa_buf->datas[0].chunk->stride = sizeof(float) * engine->n_outputs_;
    spa_buf->datas[0].chunk->size   = n_frames * engine->n_outputs_ *
                                      sizeof(float);

    pw_stream_queue_buffer(engine->stream_, pwbuf);
}

void AudioEngine::on_state_changed(void* userdata, uint32_t old_state,
                                    uint32_t state, const char* error) {
    (void)old_state;
    auto* hd = static_cast<HookData*>(userdata);

    const char* state_name = pw_stream_state_as_string(
        static_cast<pw_stream_state>(state));

    fprintf(stderr, "[AUDIO] Stream state: %s", state_name);
    if (error) fprintf(stderr, " (error: %s)", error);
    fprintf(stderr, "\n");

    if (static_cast<pw_stream_state>(state) == PW_STREAM_STATE_ERROR) {
        hd->engine->running_ = false;
    }
}

} // namespace demod::audio
