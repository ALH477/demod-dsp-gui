#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — FFmpeg Recorder                                        ║
// ║  Records video (ARGB framebuffer) + audio (interleaved F32)        ║
// ║  to MP4 via H.264 + AAC.                                           ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

namespace demod::record {

struct RecorderConfig {
    std::string output_path;
    int video_bitrate = 8000000;   // 8 Mbps
    int audio_bitrate = 192000;    // 192 kbps
    int fps = 60;
    int sample_rate = 48000;
    int audio_channels = 2;
    int crf = 23;                  // Constant Rate Factor (lower = better)
};

class Recorder {
public:
    Recorder();
    ~Recorder();

    bool start(const RecorderConfig& config, int fb_w, int fb_h);
    void stop();
    bool recording() const { return running_.load(); }

    // Called from main thread after render()
    void capture_frame(const uint32_t* argb, int w, int h);

    // Called from PipeWire audio thread (lock-free)
    void write_audio(const float* interleaved, int n_frames, int n_channels);

    // Elapsed recording time in seconds
    double elapsed() const;

private:
    // FFmpeg contexts (owned by encode thread only)
    struct FFmpegCtx;
    FFmpegCtx* ctx_ = nullptr;

    // Encode thread
    std::thread encode_thread_;
    std::atomic<bool> running_{false};

    // Video frame queue (main thread → encode thread)
    struct VideoFrame {
        std::vector<uint8_t> data;  // ARGB pixels
        int w, h;
    };
    std::queue<VideoFrame> video_queue_;
    std::mutex video_mutex_;

    // Audio ring buffer (PipeWire thread → encode thread)
    static constexpr int AUDIO_RING_SIZE = 131072;  // ~2.7s at 48kHz stereo
    std::vector<float> audio_ring_;
    std::atomic<int> audio_write_pos_{0};
    std::atomic<int> audio_read_pos_{0};

    // Timing
    std::chrono::steady_clock::time_point start_time_;
    int64_t video_pts_ = 0;
    int64_t audio_samples_written_ = 0;
    int fb_w_ = 0, fb_h_ = 0;
    int sample_rate_ = 48000;
    int audio_channels_ = 2;

    void encode_loop();
};

} // namespace demod::record
