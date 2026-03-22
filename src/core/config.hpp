#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Core Configuration                                     ║
// ║  Multi-resolution framebuffer, palette, FX chain types             ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace demod {

// ── Resolution presets ───────────────────────────────────────────────
struct Resolution {
    int         width;
    int         height;
    int         scale;
    const char* label;
};

constexpr Resolution RESOLUTIONS[] = {
    { 320,  200,  4, "320x200  (CGA)"   },
    { 384,  240,  3, "384x240  (DOOM)"  },
    { 480,  270,  3, "480x270  (Wide)"  },
    { 640,  360,  2, "640x360  (HD/4)"  },
    { 768,  480,  2, "768x480  (Crisp)" },
    { 800,  600,  2, "800x600  (SVGA)"  },
    { 960,  540,  2, "960x540  (HD/2)"  },
    { 1024, 576,  2, "1024x576 (WXGA)" },
    { 1280, 720,  1, "1280x720 (HD)"    },
    { 1600, 900,  1, "1600x900 (HD+)"   },
    { 1920, 1080, 1, "1920x1080(FHD)"   },
    { 2560, 1440, 1, "2560x1440(QHD)"   },
    { 3840, 2160, 1, "3840x2160(4K)"    },
};
constexpr int NUM_RESOLUTIONS = sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]);
constexpr int DEFAULT_RES_IDX = 1;

constexpr int MAX_FB_WIDTH  = 3840;
constexpr int MAX_FB_HEIGHT = 2160;

// ── Timing ───────────────────────────────────────────────────────────
constexpr int    TARGET_FPS      = 60;
constexpr double FRAME_TIME_MS   = 1000.0 / TARGET_FPS;
constexpr int    AUDIO_RATE      = 48000;
constexpr int    AUDIO_BLOCKSIZE = 256;

// ── Color ────────────────────────────────────────────────────────────
struct Color {
    uint8_t r, g, b, a;
    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
    constexpr uint32_t to_argb() const {
        return (uint32_t(a) << 24) | (uint32_t(r) << 16) |
               (uint32_t(g) << 8)  | uint32_t(b);
    }
    constexpr Color lerp(const Color& to, float t) const {
        auto mix = [](uint8_t a_, uint8_t b_, float t_) -> uint8_t {
            int result = int(a_) + int(int(b_) - int(a_)) * t_;
            return uint8_t(result < 0 ? 0 : (result > 255 ? 255 : result));
        };
        return { mix(r, to.r, t), mix(g, to.g, t),
                 mix(b, to.b, t), mix(a, to.a, t) };
    }
    constexpr Color with_alpha(uint8_t a_) const { return {r,g,b,a_}; }
};

namespace palette {
    constexpr Color BLACK       {  10,  10,  10 };
    constexpr Color DARK_BG     {  16,  16,  20 };
    constexpr Color DARK_GRAY   {  28,  28,  32 };
    constexpr Color MID_GRAY    {  55,  55,  62 };
    constexpr Color LIGHT_GRAY  { 120, 120, 130 };
    constexpr Color OFF_WHITE   { 180, 180, 190 };
    constexpr Color WHITE       { 220, 220, 225 };
    constexpr Color CYAN_DARK   {   0,  60,  52 };
    constexpr Color CYAN_MID    {   0, 160, 135 };
    constexpr Color CYAN        {   0, 255, 212 };
    constexpr Color CYAN_LIGHT  { 120, 255, 230 };
    constexpr Color CYAN_WHITE  { 200, 255, 245 };
    constexpr Color VIOLET_DARK {  45,   0,  65 };
    constexpr Color VIOLET_MID  {  90,   0, 160 };
    constexpr Color VIOLET      { 139,   0, 255 };
    constexpr Color VIOLET_LIGHT{ 175,  80, 255 };
    constexpr Color VIOLET_WHITE{ 210, 160, 255 };
    constexpr Color RED         { 255,  40,  40 };
    constexpr Color RED_DARK    { 120,  15,  15 };
    constexpr Color ORANGE      { 255, 140,   0 };
    constexpr Color YELLOW      { 255, 220,   0 };
    constexpr Color GREEN       {   0, 200,  60 };
    constexpr Color GREEN_DARK  {   0,  80,  25 };
    constexpr Color GLOW_CYAN   {   0, 255, 212, 30 };
    constexpr Color GLOW_VIOLET { 139,   0, 255, 20 };
    constexpr Color MENU_BG     {  12,  12,  16 };
    constexpr Color MENU_BORDER {  35,  35,  42 };
    constexpr Color MENU_HL     {  20,  50,  45 };
}

// ── Input actions ────────────────────────────────────────────────────
enum class Action : uint16_t {
    NONE = 0,
    NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
    NAV_SELECT, NAV_BACK,
    NAV_PAGE_UP, NAV_PAGE_DOWN,
    NAV_TAB_NEXT, NAV_TAB_PREV,
    PARAM_INC, PARAM_DEC, PARAM_INC_FINE, PARAM_DEC_FINE,
    PARAM_RESET, PARAM_RANDOMIZE,
    TRANSPORT_PLAY, TRANSPORT_STOP,
    BYPASS_TOGGLE, PANIC,
    AXIS_X, AXIS_Y, AXIS_Z, AXIS_W,
    SCREEN_NEXT, SCREEN_PREV,
    FULLSCREEN_TOGGLE, QUIT,
    MENU_OPEN,
    ACTION_COUNT
};

// ── DSP parameter descriptor ─────────────────────────────────────────
struct ParamDescriptor {
    int         index;
    std::string path;
    std::string label;
    float       min, max, init, step;
    enum class Type { SLIDER, BUTTON, CHECKBOX, NENTRY } type;
};

// ── FX slot ──────────────────────────────────────────────────────────
constexpr int MAX_FX_SLOTS = 12;

struct FXSlot {
    int         id        = -1;
    std::string name;
    std::string dsp_path;
    bool        bypassed  = false;
    bool        loaded    = false;
    bool        collapsed = false;  // UI: collapsed in chain view
    float       wet_mix   = 1.0f;
    Color       color     = palette::CYAN;
};

// ── Visualization mode ───────────────────────────────────────────────
enum class VizMode : int {
    SCOPE = 0, SPECTRUM, WATERFALL, LISSAJOUS, PHASE_METER, VIZ_COUNT
};
inline const char* viz_mode_name(VizMode m) {
    constexpr const char* N[] = {
        "SCOPE","SPECTRUM","WATERFALL","LISSAJOUS","PHASE"};
    int i = int(m);
    return (i >= 0 && i < int(VizMode::VIZ_COUNT)) ? N[i] : "???";
}

// ── FX palette: 12 slot colors ───────────────────────────────────────
constexpr Color FX_COLORS[MAX_FX_SLOTS] = {
    {  0,255,212,255},{139,  0,255,255},{255,140,  0,255},{  0,200, 60,255},
    {255, 40, 40,255},{255,220,  0,255},{ 80,180,255,255},{255, 80,180,255},
    {180,255, 80,255},{255,180, 80,255},{ 80,255,180,255},{180, 80,255,255},
};

} // namespace demod

// Hash specialization so Action works as unordered_map key
namespace std {
    template<> struct hash<demod::Action> {
        size_t operator()(demod::Action a) const noexcept {
            return hash<uint16_t>{}(static_cast<uint16_t>(a));
        }
    };
}
