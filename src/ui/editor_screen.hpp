#pragma once
#include "ui/screen.hpp"
#include <functional>
#include <string>
#include <vector>

namespace demod::ui {

class EditorScreen : public Screen {
public:
    EditorScreen();

    std::string name() const override { return "EDITOR"; }
    std::string help_text() const override {
        return "Typing | F5:Compile | F2:Save | Esc:Back";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;
    void on_enter() override;

    void open_file(const std::string& path);
    void new_file(const std::string& content = "");
    bool has_file() const { return !file_path_.empty(); }
    std::string file_path() const { return file_path_; }
    std::string file_content() const;

    void set_on_compile(std::function<bool(const std::string&)> cb) { on_compile_ = cb; }
    void set_on_save_as(std::function<void()> cb) { on_save_as_ = cb; }

    void set_file_path(const std::string& path) { file_path_ = path; }

    // Used by engine to pass text input each frame
    void feed_text_input(const std::string& text);

    // Receive compile result from engine
    void set_compile_result(const std::string& msg);

private:
    std::vector<std::string> lines_;
    int cursor_line_ = 0;
    int cursor_col_ = 0;
    int scroll_ = 0;
    float blink_timer_ = 0;
    bool cursor_visible_ = true;

    std::string file_path_;
    bool modified_ = false;

    std::string status_msg_;
    float status_timer_ = 0;

    std::function<bool(const std::string&)> on_compile_;
    std::function<void()> on_save_as_;

    // Editing
    void insert_char(char c);
    void delete_backward();
    void delete_forward();
    void insert_newline();
    void move_cursor(int dline, int dcol);
    void ensure_cursor_visible();

    // Syntax highlighting
    enum class TokenType { NORMAL, KEYWORD, COMMENT, STRING, NUMBER, OPERATOR, UI_ELEMENT };
    struct Token { int start; int len; TokenType type; };
    std::vector<Token> tokenize_line(const std::string& line) const;
    static TokenType classify_word(const std::string& word);

    // Layout helpers
    int visible_lines(int screen_h) const;
    int gutter_width() const;
    int text_area_width(int screen_w) const;
    int chars_per_line(int screen_w) const;

    // File I/O
    bool save_file();
    bool load_file(const std::string& path);
};

} // namespace demod::ui
