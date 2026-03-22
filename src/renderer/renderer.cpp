// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Multi-Resolution Software Renderer                     ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "renderer/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace demod::renderer {

Renderer::Renderer() {
    fb_.resize(MAX_FB_WIDTH * MAX_FB_HEIGHT, 0);
    bloom_buf_.resize(MAX_FB_WIDTH * MAX_FB_HEIGHT, 0);
    tmp_buf_.resize(MAX_FB_WIDTH * MAX_FB_HEIGHT, 0);
}

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(const std::string& title) {
    const auto& res = RESOLUTIONS[res_idx_];
    window_ = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        res.width * res.scale, res.height * res.scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) return false;

    sdl_rend_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_rend_) return false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    return recreate_texture();
}

bool Renderer::recreate_texture() {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = SDL_CreateTexture(sdl_rend_,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        fb_w_, fb_h_);
    return texture_ != nullptr;
}

bool Renderer::set_resolution(int index) {
    if (index < 0 || index >= NUM_RESOLUTIONS) return false;
    res_idx_ = index;
    fb_w_ = RESOLUTIONS[index].width;
    fb_h_ = RESOLUTIONS[index].height;
    vignette_dirty_ = true;
    SDL_SetWindowSize(window_,
        fb_w_ * RESOLUTIONS[index].scale,
        fb_h_ * RESOLUTIONS[index].scale);
    return recreate_texture();
}

void Renderer::shutdown() {
    if (texture_)  { SDL_DestroyTexture(texture_);   texture_  = nullptr; }
    if (sdl_rend_) { SDL_DestroyRenderer(sdl_rend_); sdl_rend_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
}

void Renderer::toggle_fullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_,
        fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Renderer::begin_frame() { clear(palette::BLACK); }

void Renderer::end_frame() {
    if (bloom_enabled)     apply_bloom();
    if (scanlines_enabled) apply_scanlines();
    if (vignette_enabled)  apply_vignette();
    if (barrel_enabled)    apply_barrel();

    SDL_UpdateTexture(texture_, nullptr, fb_.data(), fb_w_ * sizeof(uint32_t));
    SDL_RenderClear(sdl_rend_);
    SDL_RenderCopy(sdl_rend_, texture_, nullptr, nullptr);
    SDL_RenderPresent(sdl_rend_);
}

// ── Primitives ───────────────────────────────────────────────────────

void Renderer::clear(Color c) {
    uint32_t v = c.to_argb();
    int total = fb_w_ * fb_h_;
    uint32_t* p = fb_.data();
    for (int i = 0; i < total; ++i) p[i] = v;
}

void Renderer::pixel(int x, int y, Color c) {
    if (x >= 0 && x < fb_w_ && y >= 0 && y < fb_h_)
        fb_[y * fb_w_ + x] = c.to_argb();
}

void Renderer::blend_pixel(int x, int y, Color c) {
    if (x < 0 || x >= fb_w_ || y < 0 || y >= fb_h_) return;
    if (c.a == 255) { fb_[y * fb_w_ + x] = c.to_argb(); return; }
    if (c.a == 0) return;
    uint32_t dst = fb_[y * fb_w_ + x];
    float a = c.a / 255.0f;
    uint8_t r = uint8_t(c.r * a + ((dst >> 16) & 0xFF) * (1 - a));
    uint8_t g = uint8_t(c.g * a + ((dst >>  8) & 0xFF) * (1 - a));
    uint8_t b = uint8_t(c.b * a + ((dst      ) & 0xFF) * (1 - a));
    fb_[y * fb_w_ + x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

void Renderer::hline(int x0, int x1, int y, Color c) {
    if (y < 0 || y >= fb_h_) return;
    x0 = std::max(0, x0); x1 = std::min(fb_w_ - 1, x1);
    uint32_t v = c.to_argb();
    for (int x = x0; x <= x1; ++x) fb_[y * fb_w_ + x] = v;
}

void Renderer::vline(int x, int y0, int y1, Color c) {
    if (x < 0 || x >= fb_w_) return;
    y0 = std::max(0, y0); y1 = std::min(fb_h_ - 1, y1);
    uint32_t v = c.to_argb();
    for (int y = y0; y <= y1; ++y) fb_[y * fb_w_ + x] = v;
}

void Renderer::line(int x0, int y0, int x1, int y1, Color c) {
    int dx = std::abs(x1-x0), sx = x0<x1?1:-1;
    int dy = -std::abs(y1-y0), sy = y0<y1?1:-1;
    int err = dx + dy;
    while (true) {
        pixel(x0, y0, c);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Renderer::rect(int x, int y, int w, int h, Color c) {
    hline(x, x+w-1, y, c); hline(x, x+w-1, y+h-1, c);
    vline(x, y, y+h-1, c); vline(x+w-1, y, y+h-1, c);
}

void Renderer::rect_fill(int x, int y, int w, int h, Color c) {
    for (int r = y; r < y + h; ++r) hline(x, x + w - 1, r, c);
}

void Renderer::rect_fill_dithered(int x, int y, int w, int h,
                                   Color c1, Color c2) {
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col) {
            int bayer = BAYER[(row-y)&3][(col-x)&3];
            float t = float(row - y) / std::max(1, h - 1);
            pixel(col, row, (bayer < int(t * 16)) ? c2 : c1);
        }
}

void Renderer::rect_chamfer(int x, int y, int w, int h,
                              Color border, Color fill) {
    rect_fill(x+1, y+1, w-2, h-2, fill);
    hline(x+1, x+w-2, y, border); hline(x+1, x+w-2, y+h-1, border);
    vline(x, y+1, y+h-2, border); vline(x+w-1, y+1, y+h-2, border);
}

void Renderer::rect_rounded(int x, int y, int w, int h, int rad,
                              Color border, Color fill) {
    // Simple: just chamfer the corners by `rad` pixels
    rect_fill(x+rad, y+1, w-2*rad, h-2, fill);
    rect_fill(x+1, y+rad, w-2, h-2*rad, fill);
    // Borders
    hline(x+rad, x+w-1-rad, y, border);
    hline(x+rad, x+w-1-rad, y+h-1, border);
    vline(x, y+rad, y+h-1-rad, border);
    vline(x+w-1, y+rad, y+h-1-rad, border);
    // Corner arcs (simple diagonal pixels)
    for (int i = 0; i < rad; ++i) {
        pixel(x+rad-i, y+i, border);
        pixel(x+w-1-rad+i, y+i, border);
        pixel(x+rad-i, y+h-1-i, border);
        pixel(x+w-1-rad+i, y+h-1-i, border);
    }
}

void Renderer::gradient_v(int x, int y, int w, int h,
                           Color top, Color bottom) {
    for (int r = 0; r < h; ++r) {
        float t = float(r) / std::max(1, h - 1);
        hline(x, x + w - 1, y + r, top.lerp(bottom, t));
    }
}

void Renderer::gradient_h(int x, int y, int w, int h,
                           Color left, Color right) {
    for (int c = 0; c < w; ++c) {
        float t = float(c) / std::max(1, w - 1);
        vline(x + c, y, y + h - 1, left.lerp(right, t));
    }
}

void Renderer::dim_region(int x, int y, int w, int h, uint8_t alpha) {
    Color dim = {0, 0, 0, alpha};
    for (int r = y; r < y + h; ++r)
        for (int c = x; c < x + w; ++c)
            blend_pixel(c, r, dim);
}

void Renderer::dim_screen(uint8_t alpha) {
    // Integer-only fast path — darkens entire framebuffer
    uint8_t inv_a = 255 - alpha;
    int total = fb_w_ * fb_h_;
    uint32_t* p = fb_.data();
    for (int i = 0; i < total; ++i) {
        uint32_t px = p[i];
        uint8_t r = uint8_t(((px >> 16) & 0xFF) * inv_a / 255);
        uint8_t g = uint8_t(((px >>  8) & 0xFF) * inv_a / 255);
        uint8_t b = uint8_t(((px      ) & 0xFF) * inv_a / 255);
        p[i] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
    }
}

// ── Post-processing ──────────────────────────────────────────────────

void Renderer::apply_scanlines() {
    float keep = 1.0f - scanline_intensity;
    for (int y = 0; y < fb_h_; y += 2) {
        uint32_t* row = fb_.data() + y * fb_w_;
        for (int x = 0; x < fb_w_; ++x) {
            uint32_t px = row[x];
            uint8_t r = uint8_t(((px>>16)&0xFF) * keep);
            uint8_t g = uint8_t(((px>> 8)&0xFF) * keep);
            uint8_t b = uint8_t(((px    )&0xFF) * keep);
            row[x] = (0xFFu<<24)|(r<<16)|(g<<8)|b;
        }
    }
}

void Renderer::apply_bloom() {
    int total = fb_w_ * fb_h_;
    std::memcpy(bloom_buf_.data(), fb_.data(), total * 4);
    for (int y = 1; y < fb_h_-1; ++y) {
        for (int x = 1; x < fb_w_-1; ++x) {
            uint32_t src = bloom_buf_[y*fb_w_+x];
            uint8_t sr=(src>>16)&0xFF, sg=(src>>8)&0xFF, sb=src&0xFF;
            int luma = (sr*77 + sg*150 + sb*29) >> 8;
            if (luma < 160) continue;
            for (int dy=-1; dy<=1; ++dy)
                for (int dx=-1; dx<=1; ++dx) {
                    if (!dx && !dy) continue;
                    uint32_t& d = fb_[(y+dy)*fb_w_+(x+dx)];
                    uint8_t dr=(d>>16)&0xFF, dg=(d>>8)&0xFF, db=d&0xFF;
                    dr=std::min(255,int(dr+sr*bloom_intensity));
                    dg=std::min(255,int(dg+sg*bloom_intensity));
                    db=std::min(255,int(db+sb*bloom_intensity));
                    d=(0xFFu<<24)|(dr<<16)|(dg<<8)|db;
                }
        }
    }
}

void Renderer::apply_vignette() {
    // Precompute vignette map if resolution changed
    if (vignette_dirty_) {
        vignette_map_.resize(fb_w_ * fb_h_);
        float cx = fb_w_ * 0.5f, cy = fb_h_ * 0.5f;
        for (int y = 0; y < fb_h_; ++y) {
            for (int x = 0; x < fb_w_; ++x) {
                float dx = (x - cx) / cx;
                float dy = (y - cy) / cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float vig = std::max(0.3f, 1.0f - std::pow(dist * 0.7f, 2.5f));
                vignette_map_[y * fb_w_ + x] = vig;
            }
        }
        vignette_dirty_ = false;
    }

    // Apply precomputed vignette
    int total = fb_w_ * fb_h_;
    uint32_t* p = fb_.data();
    const float* v = vignette_map_.data();
    for (int i = 0; i < total; ++i) {
        uint32_t px = p[i];
        float vig = v[i];
        uint8_t r = uint8_t(((px >> 16) & 0xFF) * vig);
        uint8_t g = uint8_t(((px >>  8) & 0xFF) * vig);
        uint8_t b = uint8_t(((px      ) & 0xFF) * vig);
        p[i] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
    }
}

void Renderer::apply_barrel() {
    int total = fb_w_ * fb_h_;
    std::memcpy(tmp_buf_.data(), fb_.data(), total * 4);
    std::memset(fb_.data(), 0, total * 4);
    float k = 0.15f;
    for (int y = 0; y < fb_h_; ++y) {
        for (int x = 0; x < fb_w_; ++x) {
            float nx = 2.f * x / fb_w_ - 1;
            float ny = 2.f * y / fb_h_ - 1;
            float r2 = nx * nx + ny * ny;
            float f = 1.0f + k * r2;
            int px = int((nx * f + 1) * 0.5f * fb_w_ + 0.5f);
            int py = int((ny * f + 1) * 0.5f * fb_h_ + 0.5f);
            if (px >= 0 && px < fb_w_ && py >= 0 && py < fb_h_)
                fb_[y * fb_w_ + x] = tmp_buf_[py * fb_w_ + px];
        }
    }
}

} // namespace demod::renderer
