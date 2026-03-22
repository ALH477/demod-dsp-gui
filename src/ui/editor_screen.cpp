// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Editor Screen                                          ║
// ║  Retro text editor for Faust .dsp files with syntax highlighting.  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui/editor_screen.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unistd.h>
#include <SDL2/SDL.h>

namespace demod::ui {

using namespace demod::palette;
using namespace demod::renderer;

static constexpr int GLYPH_W = 6;   // 5px + 1px spacing
static constexpr int LINE_H  = 8;   // 7px glyph + 1px gap

EditorScreen::EditorScreen() {
    new_file();
}

void EditorScreen::on_enter() {
    cursor_visible_ = true;
    blink_timer_ = 0;
}

void EditorScreen::open_file(const std::string& path) {
    if (load_file(path)) {
        file_path_ = path;
        modified_ = false;
        status_msg_ = "Loaded: " + path;
        status_timer_ = 2.0f;
    } else {
        status_msg_ = "Failed to open: " + path;
        status_timer_ = 3.0f;
    }
}

void EditorScreen::new_file(const std::string& content) {
    lines_.clear();
    cursor_line_ = 0;
    cursor_col_ = 0;
    scroll_ = 0;
    file_path_.clear();
    modified_ = false;

    if (content.empty()) {
        lines_.push_back("");
    } else {
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            // Strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(line);
        }
        if (lines_.empty()) lines_.push_back("");
    }
}

std::string EditorScreen::file_content() const {
    std::string result;
    for (size_t i = 0; i < lines_.size(); ++i) {
        result += lines_[i];
        if (i < lines_.size() - 1) result += "\n";
    }
    return result;
}

bool EditorScreen::load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    lines_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(line);
    }
    if (lines_.empty()) lines_.push_back("");

    cursor_line_ = 0;
    cursor_col_ = 0;
    scroll_ = 0;
    return true;
}

bool EditorScreen::save_file() {
    if (file_path_.empty()) {
        if (on_save_as_) on_save_as_();
        return false;  // Save As will handle it
    }

    std::ofstream f(file_path_);
    if (!f) return false;

    for (size_t i = 0; i < lines_.size(); ++i) {
        f << lines_[i];
        if (i < lines_.size() - 1) f << "\n";
    }

    modified_ = false;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  EDITING
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::insert_char(char c) {
    if (cursor_line_ < 0 || cursor_line_ >= (int)lines_.size()) return;
    auto& line = lines_[cursor_line_];
    if (cursor_col_ > (int)line.size()) cursor_col_ = (int)line.size();
    line.insert(line.begin() + cursor_col_, c);
    cursor_col_++;
    modified_ = true;
    ensure_cursor_visible();
}

void EditorScreen::delete_backward() {
    if (cursor_line_ < 0 || cursor_line_ >= (int)lines_.size()) return;

    if (cursor_col_ > 0) {
        auto& line = lines_[cursor_line_];
        line.erase(line.begin() + cursor_col_ - 1);
        cursor_col_--;
        modified_ = true;
    } else if (cursor_line_ > 0) {
        // Merge with previous line
        cursor_col_ = (int)lines_[cursor_line_ - 1].size();
        lines_[cursor_line_ - 1] += lines_[cursor_line_];
        lines_.erase(lines_.begin() + cursor_line_);
        cursor_line_--;
        modified_ = true;
    }
    ensure_cursor_visible();
}

void EditorScreen::delete_forward() {
    if (cursor_line_ < 0 || cursor_line_ >= (int)lines_.size()) return;
    auto& line = lines_[cursor_line_];

    if (cursor_col_ < (int)line.size()) {
        line.erase(line.begin() + cursor_col_);
        modified_ = true;
    } else if (cursor_line_ < (int)lines_.size() - 1) {
        // Merge with next line
        lines_[cursor_line_] += lines_[cursor_line_ + 1];
        lines_.erase(lines_.begin() + cursor_line_ + 1);
        modified_ = true;
    }
}

void EditorScreen::insert_newline() {
    if (cursor_line_ < 0 || cursor_line_ >= (int)lines_.size()) return;
    auto& line = lines_[cursor_line_];

    std::string rest = line.substr(cursor_col_);
    line = line.substr(0, cursor_col_);

    lines_.insert(lines_.begin() + cursor_line_ + 1, rest);
    cursor_line_++;
    cursor_col_ = 0;
    modified_ = true;
    ensure_cursor_visible();
}

void EditorScreen::move_cursor(int dline, int dcol) {
    if (dline != 0) {
        cursor_line_ = std::clamp(cursor_line_ + dline, 0, (int)lines_.size() - 1);
        // Clamp column to new line length
        cursor_col_ = std::min(cursor_col_, (int)lines_[cursor_line_].size());
        ensure_cursor_visible();
    }
    if (dcol != 0) {
        cursor_col_ += dcol;
        if (cursor_col_ < 0) {
            if (cursor_line_ > 0) {
                cursor_line_--;
                cursor_col_ = (int)lines_[cursor_line_].size();
            } else {
                cursor_col_ = 0;
            }
        } else if (cursor_col_ > (int)lines_[cursor_line_].size()) {
            if (cursor_line_ < (int)lines_.size() - 1) {
                cursor_line_++;
                cursor_col_ = 0;
            } else {
                cursor_col_ = (int)lines_[cursor_line_].size();
            }
        }
        ensure_cursor_visible();
    }
}

void EditorScreen::ensure_cursor_visible() {
    int vis = 25;  // Approximate
    if (cursor_line_ < scroll_) scroll_ = cursor_line_;
    if (cursor_line_ >= scroll_ + vis) scroll_ = cursor_line_ - vis + 1;
    scroll_ = std::max(0, scroll_);
}

// ═══════════════════════════════════════════════════════════════════════
//  LAYOUT HELPERS
// ═══════════════════════════════════════════════════════════════════════

int EditorScreen::gutter_width() const {
    int digits = 1;
    int n = (int)lines_.size();
    while (n >= 10) { n /= 10; digits++; }
    return digits * GLYPH_W + 4;  // 4px padding
}

int EditorScreen::text_area_width(int screen_w) const {
    return screen_w - gutter_width() - 2;
}

int EditorScreen::chars_per_line(int screen_w) const {
    return text_area_width(screen_w) / GLYPH_W;
}

int EditorScreen::visible_lines(int screen_h) const {
    return (screen_h - 14 - 14 - 2) / LINE_H;  // header + footer + margins
}

// ═══════════════════════════════════════════════════════════════════════
//  SYNTAX HIGHLIGHTING
// ═══════════════════════════════════════════════════════════════════════

EditorScreen::TokenType EditorScreen::classify_word(const std::string& word) {
    // Keywords
    if (word == "import" || word == "process" || word == "declare" ||
        word == "with" || word == "letrec" || word == "environment" ||
        word == "case" || word == "seq" || word == "par" || word == "sum" ||
        word == "prod" || word == "int" || word == "float")
        return TokenType::KEYWORD;

    // UI elements
    if (word == "hslider" || word == "vslider" || word == "button" ||
        word == "checkbox" || word == "nentry" || word == "hbargraph" ||
        word == "vbargraph")
        return TokenType::UI_ELEMENT;

    // Number
    if (!word.empty() && (isdigit(word[0]) || word[0] == '.' || word[0] == '-'))
        return TokenType::NUMBER;

    // Operators
    if (word == "=" || word == "+" || word == "-" || word == "*" ||
        word == "/" || word == "<:" || word == ":>" || word == "~" ||
        word == "," || word == ";" || word == "(" || word == ")" ||
        word == "[" || word == "]" || word == "{" || word == "}")
        return TokenType::OPERATOR;

    return TokenType::NORMAL;
}

std::vector<EditorScreen::Token> EditorScreen::tokenize_line(const std::string& line) const {
    std::vector<Token> tokens;

    // Check for comment
    size_t comment_pos = line.find("//");
    if (comment_pos != std::string::npos) {
        // Everything before the comment
        if (comment_pos > 0) {
            // Tokenize the prefix normally
            std::string prefix = line.substr(0, comment_pos);
            // Simple word-based tokenization
            size_t i = 0;
            while (i < prefix.size()) {
                if (isspace(prefix[i])) { i++; continue; }

                if (prefix[i] == '"') {
                    // String
                    size_t end = prefix.find('"', i + 1);
                    if (end == std::string::npos) end = prefix.size();
                    tokens.push_back({(int)i, (int)(end - i + 1), TokenType::STRING});
                    i = end + 1;
                } else if (isdigit(prefix[i]) || prefix[i] == '.' ||
                           (prefix[i] == '-' && i + 1 < prefix.size() && isdigit(prefix[i+1]))) {
                    // Number
                    size_t start = i;
                    while (i < prefix.size() && (isdigit(prefix[i]) || prefix[i] == '.')) i++;
                    tokens.push_back({(int)start, (int)(i - start), TokenType::NUMBER});
                } else if (isalpha(prefix[i]) || prefix[i] == '_') {
                    // Word
                    size_t start = i;
                    while (i < prefix.size() && (isalnum(prefix[i]) || prefix[i] == '_' || prefix[i] == '.')) i++;
                    std::string word = prefix.substr(start, i - start);
                    TokenType type = classify_word(word);
                    tokens.push_back({(int)start, (int)(i - start), type});
                } else {
                    // Operator/punctuation
                    tokens.push_back({(int)i, 1, TokenType::OPERATOR});
                    i++;
                }
            }
        }
        // Comment token
        tokens.push_back({(int)comment_pos, (int)(line.size() - comment_pos), TokenType::COMMENT});
        return tokens;
    }

    // No comment — tokenize normally
    size_t i = 0;
    while (i < line.size()) {
        if (isspace(line[i])) { i++; continue; }

        if (line[i] == '"') {
            size_t end = line.find('"', i + 1);
            if (end == std::string::npos) end = line.size();
            tokens.push_back({(int)i, (int)(end - i + 1), TokenType::STRING});
            i = end + 1;
        } else if (isdigit(line[i]) || line[i] == '.' ||
                   (line[i] == '-' && i + 1 < line.size() && isdigit(line[i+1]))) {
            size_t start = i;
            while (i < line.size() && (isdigit(line[i]) || line[i] == '.')) i++;
            tokens.push_back({(int)start, (int)(i - start), TokenType::NUMBER});
        } else if (isalpha(line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() && (isalnum(line[i]) || line[i] == '_' || line[i] == '.')) i++;
            std::string word = line.substr(start, i - start);
            TokenType type = classify_word(word);
            tokens.push_back({(int)start, (int)(i - start), type});
        } else {
            // Multi-char operators
            if (i + 1 < line.size()) {
                std::string op2 = line.substr(i, 2);
                if (op2 == "<:" || op2 == ":>" || op2 == "//" || op2 == "==" || op2 == "!=") {
                    tokens.push_back({(int)i, 2, TokenType::OPERATOR});
                    i += 2;
                    continue;
                }
            }
            tokens.push_back({(int)i, 1, TokenType::OPERATOR});
            i++;
        }
    }

    return tokens;
}

// ═══════════════════════════════════════════════════════════════════════
//  TEXT INPUT
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::feed_text_input(const std::string& text) {
    for (char c : text) {
        if (c >= 32 && c < 127) {
            insert_char(c);
        }
    }
}

void EditorScreen::set_compile_result(const std::string& msg) {
    status_msg_ = msg;
    status_timer_ = 4.0f;
}

// ═══════════════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::update(const input::InputManager& input, float dt) {
    // Blink cursor
    blink_timer_ += dt;
    if (blink_timer_ >= 0.5f) {
        cursor_visible_ = !cursor_visible_;
        blink_timer_ = 0;
    }

    // Status timer
    if (status_timer_ > 0) {
        status_timer_ -= dt;
        if (status_timer_ <= 0) status_msg_.clear();
    }

    // F7 — toggle AI panel
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    static bool f7_prev = false;
    if (keys[SDL_SCANCODE_F7] && !f7_prev) {
        if (!ai_panel_visible_) {
            // Open panel and start terminal
            ai_panel_visible_ = true;
            ai_panel_focused_ = true;
            if (!terminal_.running()) {
                std::string cwd = file_path_.empty()
                    ? std::string(getenv("HOME") ? getenv("HOME") : "/")
                    : file_path_.substr(0, file_path_.rfind('/'));
                if (cwd.empty()) cwd = "/";
                // Terminal size: 64 cols x 15 rows for bottom panel
                terminal_.start(cwd, 15, 64);
            }
        } else if (ai_panel_focused_) {
            // Focus back to editor
            ai_panel_focused_ = false;
        } else {
            // Close panel
            ai_panel_visible_ = false;
            terminal_.stop();
        }
    }
    f7_prev = keys[SDL_SCANCODE_F7];

    // If terminal is focused, forward input to it
    if (ai_panel_focused_ && terminal_.running()) {
        handle_terminal_input(input);
        return;
    }

    // Special keys via SDL (editor input)

    // Backspace
    if (input.pressed(Action::PARAM_RESET)) {
        delete_backward();
        cursor_visible_ = true;
        blink_timer_ = 0;
        return;
    }

    // Arrow keys
    if (input.pressed(Action::NAV_UP))    { move_cursor(-1, 0); return; }
    if (input.pressed(Action::NAV_DOWN))  { move_cursor(1, 0);  return; }
    if (input.pressed(Action::NAV_LEFT))  { move_cursor(0, -1); return; }
    if (input.pressed(Action::NAV_RIGHT)) { move_cursor(0, 1);  return; }

    // Page Up/Down
    if (input.pressed(Action::NAV_PAGE_UP)) {
        move_cursor(-visible_lines(240), 0);
        return;
    }
    if (input.pressed(Action::NAV_PAGE_DOWN)) {
        move_cursor(visible_lines(240), 0);
        return;
    }

    // Enter
    if (input.pressed(Action::NAV_SELECT)) {
        insert_newline();
        cursor_visible_ = true;
        blink_timer_ = 0;
        return;
    }

    // F5 — Compile
    if (keys[SDL_SCANCODE_F5]) {
        static bool f5_prev = false;
        if (!f5_prev && on_compile_) {
            std::string content = file_content();
            std::string tmp = "/tmp/demod_edit_" +
                              std::to_string(getpid()) + ".dsp";
            std::ofstream f(tmp);
            if (f) {
                f << content;
                f.close();
                on_compile_(tmp);
            } else {
                status_msg_ = "Failed to write temp file";
                status_timer_ = 3.0f;
            }
        }
        f5_prev = true;
    } else {
        // Reset the F5 edge detection
        static bool* f5_ptr = nullptr;
        (void)f5_ptr;
    }

    // F2 — Save
    if (keys[SDL_SCANCODE_F2]) {
        static bool f2_prev = false;
        if (!f2_prev) {
            if (save_file()) {
                status_msg_ = "Saved: " + file_path_;
                status_timer_ = 2.0f;
            }
        }
        f2_prev = true;
    } else {
        static bool* f2_ptr = nullptr;
        (void)f2_ptr;
    }

    // Home/End (via SDL directly)
    if (keys[SDL_SCANCODE_HOME]) {
        cursor_col_ = 0;
        ensure_cursor_visible();
    }
    if (keys[SDL_SCANCODE_END]) {
        cursor_col_ = (int)lines_[cursor_line_].size();
        ensure_cursor_visible();
    }

    // Tab — insert 2 spaces
    if (keys[SDL_SCANCODE_TAB]) {
        static bool tab_prev = false;
        if (!tab_prev) {
            insert_char(' ');
            insert_char(' ');
        }
        tab_prev = true;
    } else {
        static bool* tab_ptr = nullptr;
        (void)tab_ptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  DRAW
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::draw(Renderer& r) {
    int W = r.fb_w(), H = r.fb_h();
    int gw = gutter_width();

    // ── Header bar ───────────────────────────────────────────────────
    r.rect_fill(0, 0, W, 12, DARK_GRAY);
    r.hline(0, W - 1, 12, CYAN_DARK);

    std::string title = "EDITOR";
    if (!file_path_.empty()) {
        std::string fname = file_path_;
        auto pos = fname.rfind('/');
        if (pos != std::string::npos) fname = fname.substr(pos + 1);
        title += " > " + fname;
    }
    if (modified_) title += " *";
    Font::draw_string(r, 4, 2, title, CYAN);

    char line_info[32];
    snprintf(line_info, sizeof(line_info), "Ln %d/%d",
             cursor_line_ + 1, (int)lines_.size());
    Font::draw_right(r, W - 4, 2, line_info, MID_GRAY);

    // ── Gutter background ────────────────────────────────────────────
    r.rect_fill(0, 13, gw, H - 27, {15, 15, 20});

    // ── Text area ────────────────────────────────────────────────────
    int vis = visible_lines(H);
    int text_x = gw + 2;

    for (int vi = 0; vi < vis; ++vi) {
        int line_idx = scroll_ + vi;
        if (line_idx >= (int)lines_.size()) break;

        int ly = 14 + vi * LINE_H;

        // Current line highlight
        if (line_idx == cursor_line_) {
            r.rect_fill(gw, ly, W - gw, LINE_H, MENU_HL);
        }

        // Line number
        char lnum[8];
        snprintf(lnum, sizeof(lnum), "%d", line_idx + 1);
        Font::draw_right(r, gw - 2, ly, lnum, MID_GRAY);

        // Syntax-highlighted text
        const auto& tokens = tokenize_line(lines_[line_idx]);
        for (const auto& tok : tokens) {
            Color col = LIGHT_GRAY;
            switch (tok.type) {
                case TokenType::KEYWORD:    col = CYAN; break;
                case TokenType::UI_ELEMENT: col = GREEN; break;
                case TokenType::COMMENT:    col = MID_GRAY; break;
                case TokenType::STRING:     col = YELLOW; break;
                case TokenType::NUMBER:     col = ORANGE; break;
                case TokenType::OPERATOR:   col = VIOLET_LIGHT; break;
                default: break;
            }
            std::string tok_text = lines_[line_idx].substr(tok.start, tok.len);
            Font::draw_string(r, text_x + tok.start * GLYPH_W, ly, tok_text, col);
        }

        // Show cursor on current line
        if (line_idx == cursor_line_ && cursor_visible_) {
            int cx = text_x + cursor_col_ * GLYPH_W;
            if (cx < W - 2) {
                r.rect_fill(cx, ly, GLYPH_W - 1, LINE_H - 1, WHITE);
                // Invert the character at cursor if there is one
                if (cursor_col_ < (int)lines_[line_idx].size()) {
                    char ch = lines_[line_idx][cursor_col_];
                    char str[2] = { ch, 0 };
                    Font::draw_string(r, cx, ly, str, BLACK);
                }
            }
        }
    }

    // ── AI Panel ─────────────────────────────────────────────────────
    draw_ai_panel(r);

    // ── Footer bar ───────────────────────────────────────────────────
    int footer_y = H - 13;
    r.hline(0, W - 1, footer_y, CYAN_DARK);
    r.rect_fill(0, footer_y + 1, W, 12, DARK_GRAY);

    if (!status_msg_.empty()) {
        // Show compile/save status
        Color sc = (status_msg_.find("OK") != std::string::npos ||
                    status_msg_.find("Saved") != std::string::npos)
                   ? GREEN : RED;
        Font::draw_string(r, 4, footer_y + 2, status_msg_, sc);
    } else {
        Font::draw_string(r, 4, footer_y + 2,
            "F5:Compile  F2:Save  Esc:Back", MID_GRAY);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  TERMINAL INPUT HANDLING
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::handle_terminal_input(const input::InputManager& input) {
    (void)input;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);

    // Special keys
    static bool enter_prev = false, bs_prev = false, tab_prev = false;
    static bool esc_prev = false, up_prev = false, down_prev = false;
    static bool left_prev = false, right_prev = false;
    static bool ctrl_c_prev = false;

    if (keys[SDL_SCANCODE_RETURN] && !enter_prev)   terminal_.send_enter();
    if (keys[SDL_SCANCODE_BACKSPACE] && !bs_prev)    terminal_.send_backspace();
    if (keys[SDL_SCANCODE_TAB] && !tab_prev)         terminal_.send_tab();
    if (keys[SDL_SCANCODE_ESCAPE] && !esc_prev)      terminal_.send_escape();
    if (keys[SDL_SCANCODE_UP] && !up_prev)           terminal_.send_arrow_up();
    if (keys[SDL_SCANCODE_DOWN] && !down_prev)       terminal_.send_arrow_down();
    if (keys[SDL_SCANCODE_LEFT] && !left_prev)       terminal_.send_arrow_left();
    if (keys[SDL_SCANCODE_RIGHT] && !right_prev)     terminal_.send_arrow_right();

    // Ctrl+C
    bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    if (ctrl && keys[SDL_SCANCODE_C] && !ctrl_c_prev) terminal_.send_ctrl_c();

    enter_prev = keys[SDL_SCANCODE_RETURN];
    bs_prev = keys[SDL_SCANCODE_BACKSPACE];
    tab_prev = keys[SDL_SCANCODE_TAB];
    esc_prev = keys[SDL_SCANCODE_ESCAPE];
    up_prev = keys[SDL_SCANCODE_UP];
    down_prev = keys[SDL_SCANCODE_DOWN];
    left_prev = keys[SDL_SCANCODE_LEFT];
    right_prev = keys[SDL_SCANCODE_RIGHT];
    ctrl_c_prev = keys[SDL_SCANCODE_C];
}

// ═══════════════════════════════════════════════════════════════════════
//  AI PANEL RENDERING
// ═══════════════════════════════════════════════════════════════════════

void EditorScreen::draw_ai_panel(Renderer& r) const {
    if (!ai_panel_visible_ || !terminal_.running()) return;

    int W = r.fb_w(), H = r.fb_h();
    (void)W;
    int term_rows = terminal_.rows();
    int term_cols = terminal_.cols();
    int cell_w = GLYPH_W;
    int cell_h = LINE_H;

    // Panel dimensions
    int panel_h = term_rows * cell_h + 2;
    int panel_w = term_cols * cell_w + 4;
    int panel_x = 0;
    int panel_y = H - panel_h - 12;

    // Panel background
    Color border = ai_panel_focused_ ? CYAN : MID_GRAY;
    r.rect_fill(panel_x, panel_y, panel_w, panel_h, {10, 10, 18});
    r.rect(panel_x, panel_y, panel_w, panel_h, border);

    // Panel header
    r.rect_fill(panel_x + 1, panel_y + 1, panel_w - 2, cell_h, DARK_GRAY);
    Font::draw_string(r, panel_x + 4, panel_y + 2, "OpenCode", CYAN);
    if (ai_panel_focused_) {
        Font::draw_right(r, panel_x + panel_w - 4, panel_y + 2, "[FOCUSED]", GREEN);
    }

    // Terminal grid
    int grid_x = panel_x + 2;
    int grid_y = panel_y + cell_h + 2;

    for (int row = 0; row < term_rows; ++row) {
        for (int col = 0; col < term_cols; ++col) {
            const auto& cell = terminal_.get_cell(row, col);

            uint8_t fg_r = cell.fg_r, fg_g = cell.fg_g, fg_b = cell.fg_b;
            uint8_t bg_r = cell.bg_r, bg_g = cell.bg_g, bg_b = cell.bg_b;
            if (cell.reverse) {
                std::swap(fg_r, bg_r);
                std::swap(fg_g, bg_g);
                std::swap(fg_b, bg_b);
            }

            int cx = grid_x + col * cell_w;
            int cy = grid_y + row * cell_h;

            // Background
            r.rect_fill(cx, cy, cell_w, cell_h, {bg_r, bg_g, bg_b});

            // Character
            if (cell.ch >= 32 && cell.ch < 127) {
                Color fg(fg_r, fg_g, fg_b);
                if (cell.bold) {
                    fg = Color(std::min(255, (int)fg_r + 60),
                               std::min(255, (int)fg_g + 60),
                               std::min(255, (int)fg_b + 60));
                }
                char str[2] = {(char)cell.ch, 0};
                Font::draw_string(r, cx, cy, str, fg);
            }

            // Cursor
            if (row == terminal_.cursor_row() && col == terminal_.cursor_col()) {
                r.rect_fill(cx, cy, cell_w, cell_h, {200, 200, 200, 128});
            }

            // Underline
            if (cell.underline) {
                r.hline(cx, cx + cell_w - 1, cy + cell_h - 1, {fg_r, fg_g, fg_b});
            }
        }
    }
}

} // namespace demod::ui
