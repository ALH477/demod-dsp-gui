#pragma once
#include "ui/screen.hpp"
#include <functional>
#include <string>
#include <vector>

namespace demod::ui {

class FileBrowserScreen : public Screen {
public:
    enum class Mode { OPEN, SAVE };

    FileBrowserScreen();

    std::string name() const override { return "FILE BROWSER"; }
    std::string help_text() const override {
        return mode_ == Mode::SAVE
            ? "W/S:Navigate  Enter:Save  Esc:Cancel  Typing:Filter/Filename"
            : "W/S:Navigate  Enter:Select  Esc:Up  Typing:Filter";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;

    void set_mode(Mode m) { mode_ = m; }
    void set_start_dir(const std::string& dir);
    void set_extensions(const std::vector<std::string>& exts);

    void set_on_select(std::function<void(const std::string&)> cb) { on_select_ = cb; }
    void set_on_save(std::function<void(const std::string&)> cb) { on_save_ = cb; }

    bool is_open() const { return is_open_; }
    void close() { is_open_ = false; }

    // Used by engine to pass text input each frame
    void feed_text_input(const std::string& text);

private:
    Mode mode_ = Mode::OPEN;
    bool is_open_ = false;

    std::string current_dir_;
    std::vector<std::string> entries_;
    std::vector<bool> is_dir_;
    int focused_ = 0;
    int scroll_ = 0;

    std::vector<std::string> extensions_;  // filter by extension
    std::string filter_;                    // user-typed filter prefix
    std::string save_name_;                 // filename for SAVE mode

    std::function<void(const std::string&)> on_select_;
    std::function<void(const std::string&)> on_save_;

    void scan_directory(const std::string& dir);
    void navigate_into(int index);
    void navigate_up();
    bool matches_filter(const std::string& name) const;
    std::string focused_path() const;

    int visible_entries(int screen_h) const;
};

} // namespace demod::ui
