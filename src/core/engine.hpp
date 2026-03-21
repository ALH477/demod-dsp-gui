#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Engine                                                 ║
// ║  Main loop: input → update → render with screen stack + overlay    ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/config.hpp"
#include "input/input_manager.hpp"
#include "renderer/renderer.hpp"
#include "audio/audio_engine.hpp"
#include "audio/faust_bridge.hpp"
#include "audio/fx_chain.hpp"
#include "audio/chiptune.hpp"

#include "ui/screen.hpp"
#include "ui/main_menu.hpp"
#include "ui/fx_chain_screen.hpp"
#include "ui/param_screen.hpp"
#include "ui/viz_screen.hpp"
#include "ui/settings_screen.hpp"
#include "ui/help_screen.hpp"

#include <vector>
#include <memory>
#include <string>

namespace demod {

struct EngineConfig {
    std::string dsp_path;
    int         sample_rate  = AUDIO_RATE;
    int         block_size   = AUDIO_BLOCKSIZE;
    int         resolution   = DEFAULT_RES_IDX;
    bool        fullscreen   = false;
    bool        enable_bci   = false;
};

class Engine {
public:
    Engine();
    ~Engine();

    bool init(const EngineConfig& config);
    void run();
    void quit();

private:
    bool running_ = false;

    // Subsystems
    input::InputManager     input_;
    renderer::Renderer      renderer_;
    audio::AudioEngine      audio_;
    audio::FXChainProcessor fx_processor_;
    audio::ChiptuneSynth    chiptune_;

    // Screens (index-based switching)
    std::vector<std::unique_ptr<ui::Screen>> screens_;
    int current_screen_ = 0;

    // Owned screen pointers for cross-references
    ui::FXChainScreen*  fx_chain_screen_  = nullptr;
    ui::ParamScreen*    param_screen_     = nullptr;
    ui::VizScreen*      viz_screen_       = nullptr;
    ui::SettingsScreen* settings_screen_  = nullptr;

    // Main menu overlay
    ui::MainMenu main_menu_;
    bool         menu_open_ = false;

    // Help overlay
    ui::HelpScreen help_screen_;
    bool           help_open_ = false;

    // Debug overlay
    bool  show_debug_  = false;
    float fps_timer_   = 0;
    int   fps_counter_ = 0;
    int   fps_display_ = 0;

    void process_events();
    void update(float dt);
    void render();
    void draw_debug_overlay();
    void draw_screen_tabs();
    void draw_help_bar();
    void setup_menu_entries();
};

} // namespace demod
