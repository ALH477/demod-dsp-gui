// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — FFmpeg Recorder Implementation                         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "record/recorder.hpp"
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

namespace demod::record {

#ifdef HAVE_FFMPEG

struct Recorder::FFmpegCtx {
    AVFormatContext* fmt = nullptr;
    AVCodecContext*  video_codec = nullptr;
    AVCodecContext*  audio_codec = nullptr;
    AVStream*        video_stream = nullptr;
    AVStream*        audio_stream = nullptr;
    SwsContext*      sws = nullptr;   // ARGB → YUV420P
    SwrContext*      swr = nullptr;   // F32 interleaved → F32 planar
    AVFrame*         video_frame = nullptr;
    AVFrame*         audio_frame = nullptr;
    AVRational       video_tb;
    AVRational       audio_tb;
};

#endif

Recorder::Recorder() {
    audio_ring_.resize(AUDIO_RING_SIZE * 2, 0.0f);
}

Recorder::~Recorder() {
    stop();
}

bool Recorder::start(const RecorderConfig& config, int fb_w, int fb_h) {
#ifdef HAVE_FFMPEG
    if (running_) return false;

    fb_w_ = fb_w;
    fb_h_ = fb_h;
    sample_rate_ = config.sample_rate;
    audio_channels_ = config.audio_channels;
    video_pts_ = 0;
    audio_samples_written_ = 0;
    audio_write_pos_.store(0, std::memory_order_relaxed);
    audio_read_pos_.store(0, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();

    auto* c = new FFmpegCtx();

    // Open output file
    int ret = avformat_alloc_output_context2(&c->fmt, nullptr, "mp4",
                                              config.output_path.c_str());
    if (ret < 0 || !c->fmt) {
        fprintf(stderr, "[REC] Failed to alloc output context\n");
        delete c;
        return false;
    }

    // ── Video encoder (H.264) ─────────────────────────────────────────
    const AVCodec* vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec) {
        fprintf(stderr, "[REC] H.264 encoder not found\n");
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    c->video_codec = avcodec_alloc_context3(vcodec);
    c->video_codec->width = fb_w;
    c->video_codec->height = fb_h;
    c->video_codec->time_base = {1, config.fps};
    c->video_codec->framerate = {config.fps, 1};
    c->video_codec->pix_fmt = AV_PIX_FMT_YUV420P;
    c->video_codec->bit_rate = config.video_bitrate;
    c->video_codec->gop_size = config.fps * 2; // Keyframe every 2s
    c->video_codec->max_b_frames = 2;

    // CRF mode
    av_opt_set_int(c->video_codec->priv_data, "crf", config.crf, 0);
    av_opt_set(c->video_codec->priv_data, "preset", "veryfast", 0);

    if (c->fmt->oformat->flags & AVFMT_GLOBALHEADER)
        c->video_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(c->video_codec, vcodec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[REC] Failed to open video codec\n");
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    c->video_stream = avformat_new_stream(c->fmt, nullptr);
    c->video_stream->id = 0;
    avcodec_parameters_from_context(c->video_stream->codecpar, c->video_codec);
    c->video_tb = c->video_codec->time_base;

    // ── Audio encoder (AAC) ───────────────────────────────────────────
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec) {
        fprintf(stderr, "[REC] AAC encoder not found\n");
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    c->audio_codec = avcodec_alloc_context3(acodec);
    c->audio_codec->sample_rate = config.sample_rate;
    if (config.audio_channels == 1)
        c->audio_codec->ch_layout = AV_CHANNEL_LAYOUT_MONO;
    else
        c->audio_codec->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    c->audio_codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c->audio_codec->bit_rate = config.audio_bitrate;

    if (c->fmt->oformat->flags & AVFMT_GLOBALHEADER)
        c->audio_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(c->audio_codec, acodec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[REC] Failed to open audio codec\n");
        avcodec_free_context(&c->audio_codec);
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    c->audio_stream = avformat_new_stream(c->fmt, nullptr);
    c->audio_stream->id = 1;
    avcodec_parameters_from_context(c->audio_stream->codecpar, c->audio_codec);
    c->audio_tb = c->audio_codec->time_base;

    // ── Scaler (ARGB → YUV420P) ───────────────────────────────────────
    c->sws = sws_getContext(fb_w, fb_h, AV_PIX_FMT_ARGB,
                            fb_w, fb_h, AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!c->sws) {
        fprintf(stderr, "[REC] Failed to create sws context\n");
        avcodec_free_context(&c->audio_codec);
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    // ── Resampler (F32 interleaved → F32 planar) ──────────────────────
    AVChannelLayout in_layout;
    if (config.audio_channels == 1)
        in_layout = AV_CHANNEL_LAYOUT_MONO;
    else
        in_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout out_layout = c->audio_codec->ch_layout;

    ret = swr_alloc_set_opts2(&c->swr,
        &out_layout, AV_SAMPLE_FMT_FLTP, config.sample_rate,
        &in_layout,  AV_SAMPLE_FMT_FLT,  config.sample_rate,
        0, nullptr);
    if (ret < 0 || !c->swr) {
        fprintf(stderr, "[REC] Failed to create swr context\n");
        sws_freeContext(c->sws);
        avcodec_free_context(&c->audio_codec);
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }
    swr_init(c->swr);

    // ── Video frame ───────────────────────────────────────────────────
    c->video_frame = av_frame_alloc();
    c->video_frame->format = AV_PIX_FMT_YUV420P;
    c->video_frame->width = fb_w;
    c->video_frame->height = fb_h;
    av_frame_get_buffer(c->video_frame, 0);

    // ── Audio frame ───────────────────────────────────────────────────
    c->audio_frame = av_frame_alloc();
    c->audio_frame->format = AV_SAMPLE_FMT_FLTP;
    c->audio_frame->ch_layout = c->audio_codec->ch_layout;
    c->audio_frame->sample_rate = config.sample_rate;
    c->audio_frame->nb_samples = c->audio_codec->frame_size;
    av_frame_get_buffer(c->audio_frame, 0);

    // ── Write header ──────────────────────────────────────────────────
    ret = avio_open(&c->fmt->pb, config.output_path.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "[REC] Failed to open output file: %s\n",
                config.output_path.c_str());
        av_frame_free(&c->audio_frame);
        av_frame_free(&c->video_frame);
        swr_free(&c->swr);
        sws_freeContext(c->sws);
        avcodec_free_context(&c->audio_codec);
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    ret = avformat_write_header(c->fmt, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[REC] Failed to write header\n");
        avio_closep(&c->fmt->pb);
        av_frame_free(&c->audio_frame);
        av_frame_free(&c->video_frame);
        swr_free(&c->swr);
        sws_freeContext(c->sws);
        avcodec_free_context(&c->audio_codec);
        avcodec_free_context(&c->video_codec);
        avformat_free_context(c->fmt);
        delete c;
        return false;
    }

    ctx_ = c;
    running_.store(true, std::memory_order_release);

    // Clear queues
    { std::lock_guard<std::mutex> lock(video_mutex_); video_queue_ = {}; }

    encode_thread_ = std::thread(&Recorder::encode_loop, this);

    fprintf(stderr, "[REC] Recording started: %s (%dx%d @ %d fps, CRF %d)\n",
            config.output_path.c_str(), fb_w, fb_h, config.fps, config.crf);
    return true;
#else
    (void)config; (void)fb_w; (void)fb_h;
    fprintf(stderr, "[REC] FFmpeg not available\n");
    return false;
#endif
}

void Recorder::stop() {
    if (!running_) return;
    running_.store(false, std::memory_order_release);

    if (encode_thread_.joinable())
        encode_thread_.join();

#ifdef HAVE_FFMPEG
    if (ctx_) {
        // Flush encoders
        auto flush = [](AVCodecContext* codec, AVStream* stream,
                         AVFormatContext* fmt) {
            avcodec_send_frame(codec, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(codec, pkt) == 0) {
                pkt->stream_index = stream->index;
                av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);
                av_interleaved_write_frame(fmt, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        };

        flush(ctx_->video_codec, ctx_->video_stream, ctx_->fmt);
        flush(ctx_->audio_codec, ctx_->audio_stream, ctx_->fmt);

        av_write_trailer(ctx_->fmt);
        avio_closep(&ctx_->fmt->pb);

        av_frame_free(&ctx_->audio_frame);
        av_frame_free(&ctx_->video_frame);
        swr_free(&ctx_->swr);
        sws_freeContext(ctx_->sws);
        avcodec_free_context(&ctx_->audio_codec);
        avcodec_free_context(&ctx_->video_codec);
        avformat_free_context(ctx_->fmt);

        delete ctx_;
        ctx_ = nullptr;
    }
#endif

    fprintf(stderr, "[REC] Recording stopped\n");
}

void Recorder::capture_frame(const uint32_t* argb, int w, int h) {
    if (!running_ || !argb) return;

    VideoFrame frame;
    frame.w = w;
    frame.h = h;
    size_t size = w * h * 4;
    frame.data.resize(size);
    std::memcpy(frame.data.data(), argb, size);

    std::lock_guard<std::mutex> lock(video_mutex_);
    video_queue_.push(std::move(frame));
}

void Recorder::write_audio(const float* interleaved, int n_frames, int n_channels) {
    if (!running_ || !interleaved) return;

    int ring_size = AUDIO_RING_SIZE;
    int channels = std::min(n_channels, 2);
    int write_pos = audio_write_pos_.load(std::memory_order_relaxed);

    for (int i = 0; i < n_frames; ++i) {
        int idx = ((write_pos + i) % ring_size) * 2;
        audio_ring_[idx]     = interleaved[i * n_channels];       // L
        audio_ring_[idx + 1] = (channels >= 2)
            ? interleaved[i * n_channels + 1]                     // R
            : interleaved[i * n_channels];                        // Mono → L+R
    }

    audio_write_pos_.store(
        (write_pos + n_frames) % ring_size, std::memory_order_release);
}

double Recorder::elapsed() const {
    if (!running_) return 0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

// ═══════════════════════════════════════════════════════════════════════
//  ENCODE THREAD
// ═══════════════════════════════════════════════════════════════════════

void Recorder::encode_loop() {
#ifdef HAVE_FFMPEG
    AVPacket* pkt = av_packet_alloc();
    int audio_frame_size = ctx_->audio_codec->frame_size;

    while (running_.load(std::memory_order_relaxed)) {
        bool did_work = false;

        // ── Encode video frames ───────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(video_mutex_);
            while (!video_queue_.empty()) {
                auto& frame = video_queue_.front();

                // Convert ARGB → YUV420P
                const uint8_t* src[1] = { frame.data.data() };
                int src_linesize[1] = { frame.w * 4 };
                av_frame_make_writable(ctx_->video_frame);

                sws_scale(ctx_->sws, src, src_linesize, 0, frame.h,
                          ctx_->video_frame->data, ctx_->video_frame->linesize);

                ctx_->video_frame->pts = video_pts_++;

                int ret = avcodec_send_frame(ctx_->video_codec, ctx_->video_frame);
                if (ret < 0) {
                    fprintf(stderr, "[REC] Video send_frame error\n");
                    video_queue_.pop();
                    continue;
                }

                while (avcodec_receive_packet(ctx_->video_codec, pkt) == 0) {
                    pkt->stream_index = ctx_->video_stream->index;
                    av_packet_rescale_ts(pkt, ctx_->video_codec->time_base,
                                          ctx_->video_stream->time_base);
                    av_interleaved_write_frame(ctx_->fmt, pkt);
                    av_packet_unref(pkt);
                }

                video_queue_.pop();
                did_work = true;
            }
        }

        // ── Encode audio frames ───────────────────────────────────────
        int read_pos = audio_read_pos_.load(std::memory_order_acquire);
        int write_pos = audio_write_pos_.load(std::memory_order_acquire);
        int ring_size = AUDIO_RING_SIZE;
        int available = (write_pos - read_pos + ring_size) % ring_size;

        while (available >= audio_frame_size) {
            // Read interleaved F32 from ring buffer
            std::vector<float> interleaved(audio_frame_size * 2);
            for (int i = 0; i < audio_frame_size; ++i) {
                int idx = ((read_pos + i) % ring_size) * 2;
                interleaved[i * 2]     = audio_ring_[idx];
                interleaved[i * 2 + 1] = audio_ring_[idx + 1];
            }

            // Convert F32 interleaved → F32 planar
            const uint8_t* in_data[1] = {
                reinterpret_cast<const uint8_t*>(interleaved.data())
            };
            int out_samples = swr_get_out_samples(ctx_->swr, audio_frame_size);

            av_frame_make_writable(ctx_->audio_frame);
            ctx_->audio_frame->nb_samples = audio_frame_size;

            uint8_t* out_data[8] = {};
            for (int ch = 0; ch < ctx_->audio_codec->ch_layout.nb_channels; ++ch)
                out_data[ch] = ctx_->audio_frame->data[ch];

            int converted = swr_convert(ctx_->swr,
                out_data, out_samples,
                in_data, audio_frame_size);

            if (converted > 0) {
                ctx_->audio_frame->nb_samples = converted;
                ctx_->audio_frame->pts = audio_samples_written_;
                audio_samples_written_ += converted;

                int ret = avcodec_send_frame(ctx_->audio_codec, ctx_->audio_frame);
                if (ret < 0) {
                    fprintf(stderr, "[REC] Audio send_frame error\n");
                    break;
                }

                while (avcodec_receive_packet(ctx_->audio_codec, pkt) == 0) {
                    pkt->stream_index = ctx_->audio_stream->index;
                    av_packet_rescale_ts(pkt, ctx_->audio_codec->time_base,
                                          ctx_->audio_stream->time_base);
                    av_interleaved_write_frame(ctx_->fmt, pkt);
                    av_packet_unref(pkt);
                }
            }

            read_pos = (read_pos + audio_frame_size) % ring_size;
            available -= audio_frame_size;
            did_work = true;
        }

        audio_read_pos_.store(read_pos, std::memory_order_release);

        // Sleep briefly if nothing to do
        if (!did_work)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    av_packet_free(&pkt);
#endif
}

} // namespace demod::record
