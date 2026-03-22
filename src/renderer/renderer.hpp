#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Multi-Resolution Software Renderer                     ║
// ║  Dynamic framebuffer + CRT post-processing                        ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/config.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>
#include <string>

namespace demod::renderer {

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const std::string& title = "DeMoDOOM");
    void shutdown();

    // ── Resolution management ────────────────────────────────────────
    int  resolution_index() const { return res_idx_; }
    bool set_resolution(int index);  // Returns false if index invalid
    int  fb_w() const { return fb_w_; }
    int  fb_h() const { return fb_h_; }
    const Resolution& resolution() const { return RESOLUTIONS[res_idx_]; }

    // ── Frame lifecycle ──────────────────────────────────────────────
    void begin_frame();
    void end_frame();
    void toggle_fullscreen();

    // ── Drawing primitives ───────────────────────────────────────────
    void pixel(int x, int y, Color c);
    void blend_pixel(int x, int y, Color c);
    void clear(Color c = palette::BLACK);

    void line(int x0, int y0, int x1, int y1, Color c);
    void hline(int x0, int x1, int y, Color c);
    void vline(int x, int y0, int y1, Color c);

    void rect(int x, int y, int w, int h, Color c);
    void rect_fill(int x, int y, int w, int h, Color c);
    void rect_fill_dithered(int x, int y, int w, int h,
                            Color c1, Color c2);
    void rect_chamfer(int x, int y, int w, int h, Color border, Color fill);
    void rect_rounded(int x, int y, int w, int h, int r,
                      Color border, Color fill);

    void gradient_v(int x, int y, int w, int h, Color top, Color bottom);
    void gradient_h(int x, int y, int w, int h, Color left, Color right);

    // Fill a region with alpha overlay (default 160 = ~63% darkening)
    void dim_region(int x, int y, int w, int h, uint8_t alpha = 160);
    void dim_screen(uint8_t alpha = 160);

    // ── Post-FX ──────────────────────────────────────────────────────
    bool  scanlines_enabled  = true;
    bool  bloom_enabled      = true;
    bool  vignette_enabled   = true;
    bool  barrel_enabled     = false;
    float scanline_intensity = 0.25f;
    float bloom_intensity    = 0.12f;

    // ── Access ───────────────────────────────────────────────────────
    SDL_Window* window() const { return window_; }
    uint32_t*   framebuffer()  { return fb_.data(); }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* sdl_rend_ = nullptr;
    SDL_Texture*  texture_  = nullptr;

    int res_idx_ = DEFAULT_RES_IDX;
    int fb_w_    = RESOLUTIONS[DEFAULT_RES_IDX].width;
    int fb_h_    = RESOLUTIONS[DEFAULT_RES_IDX].height;

    std::vector<uint32_t> fb_;
    std::vector<uint32_t> bloom_buf_;
    std::vector<uint32_t> tmp_buf_;  // Persistent temp for barrel distortion
    std::vector<float>    vignette_map_;  // Precomputed vignette per-pixel
    bool fullscreen_ = false;
    bool vignette_dirty_ = true;

    bool recreate_texture();
    void apply_scanlines();
    void apply_bloom();
    void apply_vignette();
    void apply_barrel();

    static constexpr int BAYER[4][4] = {
        { 0, 8, 2,10},{12, 4,14, 6},{ 3,11, 1, 9},{15, 7,13, 5}};
};

} // namespace demod::renderer
