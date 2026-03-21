#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Engine                                                 ║
// ║  Main loop: input → update → render with screen stack + overlay    ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "core/config.hpp"
#include "core/preset.hpp"
#include "input/input_manager.hpp"
#include "renderer/renderer.hpp"
#include "audio/audio_engine.hpp"
#include "audio/faust_bridge.hpp"
#include "audio/fx_chain.hpp"
#include "audio/chiptune.hpp"
#include "record/recorder.hpp"

#include "ui/screen.hpp"
#include "ui/main_menu.hpp"
#include "ui/fx_chain_screen.hpp"
#include "ui/param_screen.hpp"
#include "ui/viz_screen.hpp"
#include "ui/settings_screen.hpp"
#include "ui/help_screen.hpp"
#include "ui/file_browser.hpp"
#include "ui/editor_screen.hpp"

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

    // Presets
    bool save_preset(const std::string& name, PresetFormat format);
    bool load_preset(const std::string& filename);
    PresetManager& preset_mgr() { return preset_mgr_; }
    audio::FXChainProcessor& fx_processor() { return fx_processor_; }
    renderer::Renderer& renderer_ref() { return renderer_; }

    // Text input buffer (consumed per-frame by active screen)
    const std::string& text_input() const { return text_input_buffer_; }

    // File browser modal
    void open_file_browser(int target_slot = -1);
    bool file_browser_open() const { return file_browser_open_; }
    int  file_browser_target_slot() const { return file_browser_target_slot_; }

private:
    bool running_ = false;

    // Subsystems
    input::InputManager     input_;
    renderer::Renderer      renderer_;
    audio::AudioEngine      audio_;
    audio::FXChainProcessor fx_processor_;
    audio::ChiptuneSynth    chiptune_;
    PresetManager           preset_mgr_;
    record::Recorder        recorder_;

    // Screens (index-based switching)
    std::vector<std::unique_ptr<ui::Screen>> screens_;
    int current_screen_ = 0;

    // Owned screen pointers for cross-references
    ui::FXChainScreen*  fx_chain_screen_  = nullptr;
    ui::ParamScreen*    param_screen_     = nullptr;
    ui::VizScreen*      viz_screen_       = nullptr;
    ui::SettingsScreen* settings_screen_  = nullptr;
    ui::EditorScreen*   editor_screen_    = nullptr;

    // File browser modal (overlay, not in screen stack)
    ui::FileBrowserScreen file_browser_modal_;

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

    // Text input (SDL_TEXTINPUT buffer, consumed per-frame by screens)
    std::string text_input_buffer_;

    // File browser modal state
    bool file_browser_open_ = false;
    int  file_browser_target_slot_ = -1;

    void process_events();
    void update(float dt);
    void render();
    void draw_debug_overlay();
    void draw_screen_tabs();
    void draw_help_bar();
    void setup_menu_entries();
};

} // namespace demod
