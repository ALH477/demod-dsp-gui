#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Embedded Terminal (self-contained VT100 + PTY)         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <vector>

namespace demod::terminal {

struct Cell {
    uint32_t ch = ' ';
    uint8_t fg_r = 200, fg_g = 200, fg_b = 200;
    uint8_t bg_r = 10,  bg_g = 10,  bg_b = 18;
    bool bold = false;
    bool underline = false;
    bool reverse = false;
    bool italic = false;
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    bool start(const std::string& working_dir, int rows, int cols);
    void stop();
    bool running() const { return running_.load(); }

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // Thread-safe cell access
    const Cell& get_cell(int row, int col) const;

    int cursor_row() const { return cursor_row_; }
    int cursor_col() const { return cursor_col_; }

    // Keyboard input
    void send_char(uint32_t c);
    void send_enter();
    void send_backspace();
    void send_tab();
    void send_escape();
    void send_arrow_up();
    void send_arrow_down();
    void send_arrow_left();
    void send_arrow_right();
    void send_ctrl_c();
    void send_string(const std::string& s);

    void resize(int rows, int cols);

private:
    // Terminal grid
    int rows_ = 24, cols_ = 80;
    std::vector<std::vector<Cell>> grid_;
    int cursor_row_ = 0, cursor_col_ = 0;
    bool cursor_visible_ = true;

    // Current text attributes
    Cell current_attr_;

    // PTY
    int pty_fd_ = -1;
    pid_t child_pid_ = -1;

    // I/O thread
    std::thread io_thread_;
    std::atomic<bool> running_{false};

    // Input buffer
    static constexpr int INPUT_BUF_SIZE = 4096;
    std::array<char, INPUT_BUF_SIZE> input_buf_{};
    std::atomic<int> input_write_{0};
    std::atomic<int> input_read_{0};

    void io_loop();
    void flush_input();

    // VT100 parser
    enum class ParseState { NORMAL, ESCAPE, CSI, OSC };
    ParseState parse_state_ = ParseState::NORMAL;
    std::string escape_buf_;
    std::vector<int> csi_params_;

    void process_byte(uint8_t byte);
    void process_normal(uint8_t byte);
    void process_escape(uint8_t byte);
    void process_csi(uint8_t byte);
    void execute_csi();
    void execute_sgr();

    // Terminal operations
    void scroll_up();
    void clear_screen();
    void clear_line();
    void clear_to_eol();
    void put_char(uint32_t ch);
    void newline();
    void carriage_return();

    // 256-color palette
    static void indexed_to_rgb(uint8_t idx, uint8_t& r, uint8_t& g, uint8_t& b);
};

} // namespace demod::terminal
