// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — File Browser Screen                                    ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/file_browser.hpp"
#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace demod::ui {

namespace fs = std::filesystem;
using namespace demod::palette;
using namespace demod::renderer;

FileBrowserScreen::FileBrowserScreen() {
    // Default to home directory
    const char* home = std::getenv("HOME");
    current_dir_ = home ? home : "/";
    scan_directory(current_dir_);
}

void FileBrowserScreen::set_start_dir(const std::string& dir) {
    if (fs::exists(dir) && fs::is_directory(dir)) {
        current_dir_ = dir;
        scan_directory(dir);
    }
}

void FileBrowserScreen::set_extensions(const std::vector<std::string>& exts) {
    extensions_ = exts;
    scan_directory(current_dir_);
}

void FileBrowserScreen::scan_directory(const std::string& dir) {
    entries_.clear();
    is_dir_.clear();
    focused_ = 0;
    scroll_ = 0;

    try {
        // Add parent directory entry
        if (dir != "/") {
            entries_.push_back("..");
            is_dir_.push_back(true);
        }

        for (const auto& entry : fs::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();

            // Skip hidden files
            if (!name.empty() && name[0] == '.') continue;

            if (entry.is_directory()) {
                entries_.push_back(name);
                is_dir_.push_back(true);
            } else {
                // Filter by extension
                if (!extensions_.empty()) {
                    std::string ext = entry.path().extension().string();
                    bool match = false;
                    for (const auto& e : extensions_) {
                        if (ext == e) { match = true; break; }
                    }
                    if (!match) continue;
                }
                entries_.push_back(name);
                is_dir_.push_back(false);
            }
        }
    } catch (...) {
        entries_.clear();
        is_dir_.clear();
    }

    // Sort: directories first, then files, alphabetical
    for (size_t i = 1; i < entries_.size(); ++i) {
        for (size_t j = i; j > 1; --j) {
            bool a_dir = is_dir_[j-1], b_dir = is_dir_[j];
            if (a_dir && !b_dir) break;
            if (!a_dir && b_dir) {
                std::swap(entries_[j-1], entries_[j]);
                bool tmp = is_dir_[j]; is_dir_[j] = is_dir_[j-1]; is_dir_[j-1] = tmp;
            } else if (entries_[j] < entries_[j-1]) {
                std::swap(entries_[j-1], entries_[j]);
                bool tmp = is_dir_[j]; is_dir_[j] = is_dir_[j-1]; is_dir_[j-1] = tmp;
            } else {
                break;
            }
        }
    }

    current_dir_ = dir;
}

void FileBrowserScreen::navigate_into(int index) {
    if (index < 0 || index >= (int)entries_.size()) return;

    if (is_dir_[index]) {
        std::string name = entries_[index];
        if (name == "..") {
            navigate_up();
        } else {
            std::string new_dir = current_dir_ + "/" + name;
            scan_directory(new_dir);
        }
    } else {
        // File selected
        std::string path = focused_path();
        if (mode_ == Mode::OPEN && on_select_) {
            on_select_(path);
            is_open_ = false;
        } else if (mode_ == Mode::SAVE) {
            save_name_ = entries_[index];
        }
    }
}

void FileBrowserScreen::navigate_up() {
    if (current_dir_ == "/") return;
    auto pos = current_dir_.rfind('/');
    if (pos != std::string::npos) {
        std::string parent = (pos == 0) ? "/" : current_dir_.substr(0, pos);
        scan_directory(parent);
    }
}

bool FileBrowserScreen::matches_filter(const std::string& name) const {
    if (filter_.empty()) return true;
    // Case-insensitive prefix match
    if (name.size() < filter_.size()) return false;
    for (size_t i = 0; i < filter_.size(); ++i) {
        char a = name[i], b = filter_[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

std::string FileBrowserScreen::focused_path() const {
    if (focused_ < 0 || focused_ >= (int)entries_.size()) return "";
    return current_dir_ + "/" + entries_[focused_];
}

int FileBrowserScreen::visible_entries(int screen_h) const {
    return std::max(1, (screen_h - 40) / 9);
}

void FileBrowserScreen::feed_text_input(const std::string& text) {
    if (mode_ == Mode::SAVE) {
        save_name_ += text;
    } else {
        filter_ += text;
    }
}

void FileBrowserScreen::update(const input::InputManager& input, float dt) {
    (void)dt;
    if (!is_open_) return;

    // Navigation
    if (input.pressed(Action::NAV_DOWN))
        focused_ = std::min(focused_ + 1, (int)entries_.size() - 1);
    if (input.pressed(Action::NAV_UP))
        focused_ = std::max(focused_ - 1, 0);

    // Select / enter directory
    if (input.pressed(Action::NAV_SELECT)) {
        if (mode_ == Mode::SAVE && !save_name_.empty()) {
            // Save with the typed filename
            std::string path = current_dir_ + "/" + save_name_;
            if (on_save_) on_save_(path);
            is_open_ = false;
            return;
        }
        navigate_into(focused_);
        return;
    }

    // Backspace — remove last char from filter or up a directory
    if (input.pressed(Action::PARAM_RESET)) {
        if (mode_ == Mode::SAVE && !save_name_.empty()) {
            save_name_.pop_back();
        } else if (!filter_.empty()) {
            filter_.pop_back();
        } else {
            navigate_up();
        }
    }

    // Esc — up directory or close
    if (input.pressed(Action::NAV_BACK)) {
        if (current_dir_ == "/") {
            is_open_ = false;
        } else {
            navigate_up();
        }
    }

    // Home key — go to home directory
    if (input.pressed(Action::PANIC)) {  // P key as shortcut
        const char* home = std::getenv("HOME");
        if (home) scan_directory(home);
    }

    // Scroll management
    int vis = 15;
    if (focused_ < scroll_) scroll_ = focused_;
    if (focused_ >= scroll_ + vis) scroll_ = focused_ - vis + 1;
}

void FileBrowserScreen::draw(Renderer& r) {
    if (!is_open_) return;

    int W = r.fb_w(), H = r.fb_h();

    // Dim background
    r.dim_screen(180);

    // Panel
    int margin = 4;
    int px = margin, py = margin;
    int pw = W - margin * 2, ph = H - margin * 2;
    r.rect_fill(px, py, pw, ph, MENU_BG);
    r.rect(px, py, pw, ph, CYAN_DARK);

    // Header
    r.rect_fill(px + 1, py + 1, pw - 2, 11, DARK_GRAY);
    std::string title = (mode_ == Mode::SAVE ? "SAVE " : "OPEN ");
    // Truncate long paths
    std::string dir_display = current_dir_;
    if (dir_display.size() > 35)
        dir_display = "..." + dir_display.substr(dir_display.size() - 32);
    title += dir_display;
    Font::draw_string(r, px + 4, py + 3, title, CYAN);

    // Filter / save filename bar
    int bar_y = py + 14;
    r.rect_fill(px + 1, bar_y, pw - 2, 9, {20, 20, 30});
    if (mode_ == Mode::SAVE) {
        Font::draw_string(r, px + 4, bar_y + 1, "Name: " + save_name_, WHITE);
    } else if (!filter_.empty()) {
        Font::draw_string(r, px + 4, bar_y + 1, "Filter: " + filter_, YELLOW);
    } else {
        Font::draw_string(r, px + 4, bar_y + 1, "All files", MID_GRAY);
    }

    // File list
    int list_y = bar_y + 11;
    int line_h = 9;
    int vis = (ph - (list_y - py) - 4) / line_h;

    for (int vi = 0; vi < vis; ++vi) {
        int idx = scroll_ + vi;
        if (idx >= (int)entries_.size()) break;

        if (!matches_filter(entries_[idx])) continue;

        int ly = list_y + vi * line_h;
        bool focused = (idx == focused_);

        // Highlight
        if (focused) {
            r.rect_fill(px + 2, ly, pw - 4, line_h - 1, MENU_HL);
            r.vline(px + 2, ly, ly + line_h - 2, CYAN);
        }

        // Icon
        if (is_dir_[idx]) {
            Font::draw_string(r, px + 6, ly + 1, entries_[idx] + "/", focused ? WHITE : LIGHT_GRAY);
        } else {
            Font::draw_string(r, px + 6, ly + 1, entries_[idx], focused ? WHITE : LIGHT_GRAY);
        }
    }

    // Scroll indicator
    int total = (int)entries_.size();
    if (total > vis) {
        int bar_x = px + pw - 5;
        int bar_h = ph - 24;
        r.vline(bar_x, py + 14, py + 14 + bar_h, MID_GRAY);
        int thumb_h = std::max(3, bar_h * vis / total);
        int thumb_y = py + 14 + (bar_h - thumb_h) * scroll_ / std::max(1, total - vis);
        r.rect_fill(bar_x - 1, thumb_y, 3, thumb_h, CYAN);
    }

    // Footer
    int footer_y = py + ph - 11;
    r.hline(px + 1, px + pw - 2, footer_y, DARK_GRAY);
    Font::draw_string(r, px + 4, footer_y + 2, help_text(), MID_GRAY);
}

} // namespace demod::ui
