#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Settings Screen                                        ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/screen.hpp"
#include "audio/audio_engine.hpp"
#include "audio/chiptune.hpp"
#include "input/input_manager.hpp"
#include "core/preset.hpp"
#include <functional>

namespace demod::ui {

class SettingsScreen : public Screen {
public:
    SettingsScreen(renderer::Renderer& rend,
                   audio::AudioEngine& audio,
                   audio::ChiptuneSynth& chiptune,
                   input::InputManager& input_mgr);

    void set_preset_callbacks(
        PresetManager* mgr,
        std::function<bool(const std::string&, PresetFormat)> save_cb,
        std::function<bool(const std::string&)> load_cb);

    std::string name() const override { return "SETTINGS"; }
    std::string help_text() const override {
        return "U/D:Nav  L/R:Change  Enter:Apply  [/]:Section";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;

    // Public so skip_to_editable can use it in its signature
    struct SettingItem {
        std::string label;
        std::string value;
        bool        editable = true;
        bool        is_toggle = false;
        bool        toggle_val = false;
    };

private:
    renderer::Renderer&   rend_;
    audio::AudioEngine&   audio_;
    audio::ChiptuneSynth& chiptune_;
    input::InputManager&  input_mgr_;

    enum class Section { DISPLAY, AUDIO, POST_FX, PRESETS, INPUT, ABOUT, SECTION_COUNT };
    Section section_  = Section::DISPLAY;
    int     focused_  = 0;
    int     scroll_   = 0;

    // Chiptune state
    bool  chiptune_enabled_ = true;
    float chiptune_volume_  = 0.35f;

    // Pending resolution change
    int pending_res_ = -1;

    // Preset state
    PresetManager* preset_mgr_ = nullptr;
    std::function<bool(const std::string&, PresetFormat)> preset_save_cb_;
    std::function<bool(const std::string&)> preset_load_cb_;
    int preset_format_idx_ = 0;
    std::vector<std::string> preset_list_;
    int preset_selected_ = 0;

    // Key rebind state
    bool rebind_mode_ = false;
    int  rebind_target_ = -1;  // Which action entry to rebind
    Action rebind_action_ = Action::NONE;

    std::vector<SettingItem> current_items() const;
    void apply_change(int item_index, int direction);
    bool skip_to_editable(int dir, const std::vector<SettingItem>& items);
    int  section_count() const { return int(Section::SECTION_COUNT); }
    const char* section_name(Section s) const;
};

} // namespace demod::ui
