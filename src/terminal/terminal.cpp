// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — Self-contained VT100 Terminal + PTY                   ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "terminal/terminal.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <pty.h>
#include <algorithm>

namespace demod::terminal {

Terminal::Terminal() = default;

Terminal::~Terminal() { stop(); }

bool Terminal::start(const std::string& working_dir, int rows, int cols) {
    if (running_) return false;

    rows_ = rows;
    cols_ = cols;

    // Allocate grid
    grid_.resize(rows_);
    for (auto& row : grid_) {
        row.resize(cols_);
        for (auto& cell : row) {
            cell = Cell{};
        }
    }

    cursor_row_ = 0;
    cursor_col_ = 0;
    parse_state_ = ParseState::NORMAL;
    escape_buf_.clear();

    // Fork with PTY
    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    child_pid_ = forkpty(&pty_fd_, nullptr, nullptr, &ws);
    if (child_pid_ < 0) {
        perror("[TERM] forkpty failed");
        return false;
    }

    if (child_pid_ == 0) {
        // Child
        if (!working_dir.empty()) chdir(working_dir.c_str());
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("LANG", "en_US.UTF-8", 1);
        execlp("opencode", "opencode", nullptr);
        execlp("bash", "bash", "--login", nullptr);
        _exit(127);
    }

    // Non-blocking PTY
    int flags = fcntl(pty_fd_, F_GETFL, 0);
    fcntl(pty_fd_, F_SETFL, flags | O_NONBLOCK);

    running_.store(true, std::memory_order_release);
    io_thread_ = std::thread(&Terminal::io_loop, this);

    fprintf(stderr, "[TERM] Started opencode (PID %d, %dx%d)\n", child_pid_, cols, rows);
    return true;
}

void Terminal::stop() {
    if (!running_) return;
    running_.store(false, std::memory_order_release);
    if (io_thread_.joinable()) io_thread_.join();
    if (child_pid_ > 0) { kill(child_pid_, SIGTERM); waitpid(child_pid_, nullptr, WNOHANG); child_pid_ = -1; }
    if (pty_fd_ >= 0) { close(pty_fd_); pty_fd_ = -1; }
}

const Cell& Terminal::get_cell(int row, int col) const {
    static const Cell empty{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return empty;
    return grid_[row][col];
}

void Terminal::resize(int rows, int cols) {
    rows_ = rows;
    cols_ = cols;
    grid_.resize(rows_);
    for (auto& row : grid_) {
        row.resize(cols_);
        for (size_t i = row.size(); i < (size_t)cols_; ++i)
            row[i] = Cell{};
    }
    cursor_row_ = std::min(cursor_row_, rows_ - 1);
    cursor_col_ = std::min(cursor_col_, cols_ - 1);
    if (pty_fd_ >= 0) {
        struct winsize ws = {(unsigned short)rows, (unsigned short)cols, 0, 0};
        ioctl(pty_fd_, TIOCSWINSZ, &ws);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  VT100 PARSER
// ═══════════════════════════════════════════════════════════════════════

void Terminal::indexed_to_rgb(uint8_t idx, uint8_t& r, uint8_t& g, uint8_t& b) {
    // Standard 16 colors
    static const uint8_t palette[16][3] = {
        {10,10,18}, {200,50,50}, {50,200,50}, {200,200,50},
        {50,50,200}, {200,50,200}, {50,200,200}, {200,200,200},
        {100,100,100}, {255,80,80}, {80,255,80}, {255,255,80},
        {80,80,255}, {255,80,255}, {80,255,255}, {255,255,255},
    };
    if (idx < 16) {
        r = palette[idx][0]; g = palette[idx][1]; b = palette[idx][2];
    } else if (idx < 232) {
        // 6x6x6 color cube
        idx -= 16;
        r = (idx / 36) ? 55 + 40 * (idx / 36) : 0;
        g = ((idx / 6) % 6) ? 55 + 40 * ((idx / 6) % 6) : 0;
        b = (idx % 6) ? 55 + 40 * (idx % 6) : 0;
    } else {
        // Grayscale ramp
        uint8_t v = 8 + 10 * (idx - 232);
        r = g = b = v;
    }
}

void Terminal::process_byte(uint8_t byte) {
    switch (parse_state_) {
        case ParseState::NORMAL:  process_normal(byte); break;
        case ParseState::ESCAPE:  process_escape(byte); break;
        case ParseState::CSI:     process_csi(byte); break;
        case ParseState::OSC:     if (byte == 0x07 || byte == '\\') parse_state_ = ParseState::NORMAL; break;
    }
}

void Terminal::process_normal(uint8_t byte) {
    switch (byte) {
        case 0x1B: // ESC
            parse_state_ = ParseState::ESCAPE;
            escape_buf_.clear();
            break;
        case 0x08: // Backspace
            if (cursor_col_ > 0) cursor_col_--;
            break;
        case 0x09: // Tab
            cursor_col_ = (cursor_col_ + 8) & ~7;
            if (cursor_col_ >= cols_) cursor_col_ = cols_ - 1;
            break;
        case 0x0A: // Line feed
            newline();
            break;
        case 0x0D: // Carriage return
            cursor_col_ = 0;
            break;
        case 0x0E: case 0x0F: // Shift in/out (ignore)
            break;
        default:
            if (byte >= 32) {
                put_char(byte);
            }
            break;
    }
}

void Terminal::process_escape(uint8_t byte) {
    switch (byte) {
        case '[':
            parse_state_ = ParseState::CSI;
            escape_buf_.clear();
            csi_params_.clear();
            break;
        case ']':
            parse_state_ = ParseState::OSC;
            escape_buf_.clear();
            break;
        case '(': case ')': // Character set (ignore next byte)
            break;
        case 'c': // Reset
            clear_screen();
            current_attr_ = Cell{};
            cursor_row_ = cursor_col_ = 0;
            parse_state_ = ParseState::NORMAL;
            break;
        case '7': // Save cursor (ignore)
        case '8': // Restore cursor (ignore)
        case 'D': // Index (same as line feed)
            newline();
            parse_state_ = ParseState::NORMAL;
            break;
        case 'M': // Reverse index
            if (cursor_row_ > 0) cursor_row_--;
            else scroll_up();
            parse_state_ = ParseState::NORMAL;
            break;
        case 'E': // Next line
            newline();
            cursor_col_ = 0;
            parse_state_ = ParseState::NORMAL;
            break;
        default:
            parse_state_ = ParseState::NORMAL;
            break;
    }
}

void Terminal::process_csi(uint8_t byte) {
    if (byte >= '0' && byte <= '9') {
        escape_buf_ += (char)byte;
    } else if (byte == ';') {
        csi_params_.push_back(escape_buf_.empty() ? -1 : std::stoi(escape_buf_));
        escape_buf_.clear();
    } else if (byte == '?' || byte == '>' || byte == '!') {
        // Leader
        escape_buf_ += (char)byte;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        // Final byte
        if (!escape_buf_.empty())
            csi_params_.push_back(std::stoi(escape_buf_));
        escape_buf_ += (char)byte;
        execute_csi();
        parse_state_ = ParseState::NORMAL;
    } else {
        parse_state_ = ParseState::NORMAL;
    }
}

void Terminal::execute_csi() {
    if (escape_buf_.empty()) return;
    char cmd = escape_buf_.back();

    auto param = [this](int idx, int def) -> int {
        if (idx < 0 || idx >= (int)csi_params_.size()) return def;
        return csi_params_[idx] < 0 ? def : csi_params_[idx];
    };

    switch (cmd) {
        case 'A': cursor_row_ = std::max(0, cursor_row_ - param(0, 1)); break; // Up
        case 'B': cursor_row_ = std::min(rows_ - 1, cursor_row_ + param(0, 1)); break; // Down
        case 'C': cursor_col_ = std::min(cols_ - 1, cursor_col_ + param(0, 1)); break; // Right
        case 'D': cursor_col_ = std::max(0, cursor_col_ - param(0, 1)); break; // Left
        case 'H': case 'f': // Cursor position
            cursor_row_ = std::clamp(param(0, 1) - 1, 0, rows_ - 1);
            cursor_col_ = std::clamp(param(1, 1) - 1, 0, cols_ - 1);
            break;
        case 'J': // Erase display
            if (param(0, 0) == 2 || param(0, 0) == 3) clear_screen();
            break;
        case 'K': // Erase line
            clear_to_eol();
            break;
        case 'm': // SGR (Select Graphic Rendition)
            execute_sgr();
            break;
        case 'r': // Set scrolling region (ignore for now)
            break;
        case 'h': // Set mode (ignore)
            break;
        case 'l': // Reset mode (ignore)
            break;
        case 'S': // Scroll up
            for (int i = 0; i < param(0, 1); ++i) scroll_up();
            break;
        case 'T': // Scroll down (ignore)
            break;
        default: break;
    }
}

void Terminal::execute_sgr() {
    if (csi_params_.empty()) csi_params_.push_back(0);

    for (int i = 0; i < (int)csi_params_.size(); ++i) {
        int p = csi_params_[i];
        switch (p) {
            case 0: current_attr_ = Cell{}; break; // Reset
            case 1: current_attr_.bold = true; break;
            case 3: current_attr_.italic = true; break;
            case 4: current_attr_.underline = true; break;
            case 7: current_attr_.reverse = true; break;
            case 22: current_attr_.bold = false; break;
            case 23: current_attr_.italic = false; break;
            case 24: current_attr_.underline = false; break;
            case 27: current_attr_.reverse = false; break;
            case 38: // Extended foreground
                if (i + 1 < (int)csi_params_.size() && csi_params_[i + 1] == 2) {
                    // Truecolor: 38;2;R;G;B
                    if (i + 4 < (int)csi_params_.size()) {
                        current_attr_.fg_r = csi_params_[i + 2];
                        current_attr_.fg_g = csi_params_[i + 3];
                        current_attr_.fg_b = csi_params_[i + 4];
                        i += 4;
                    }
                } else if (i + 1 < (int)csi_params_.size() && csi_params_[i + 1] == 5) {
                    // 256 color: 38;5;N
                    if (i + 2 < (int)csi_params_.size()) {
                        indexed_to_rgb(csi_params_[i + 2],
                            current_attr_.fg_r, current_attr_.fg_g, current_attr_.fg_b);
                        i += 2;
                    }
                }
                break;
            case 48: // Extended background
                if (i + 1 < (int)csi_params_.size() && csi_params_[i + 1] == 2) {
                    if (i + 4 < (int)csi_params_.size()) {
                        current_attr_.bg_r = csi_params_[i + 2];
                        current_attr_.bg_g = csi_params_[i + 3];
                        current_attr_.bg_b = csi_params_[i + 4];
                        i += 4;
                    }
                } else if (i + 1 < (int)csi_params_.size() && csi_params_[i + 1] == 5) {
                    if (i + 2 < (int)csi_params_.size()) {
                        indexed_to_rgb(csi_params_[i + 2],
                            current_attr_.bg_r, current_attr_.bg_g, current_attr_.bg_b);
                        i += 2;
                    }
                }
                break;
            case 39: // Default foreground
                current_attr_.fg_r = 200; current_attr_.fg_g = 200; current_attr_.fg_b = 200;
                break;
            case 49: // Default background
                current_attr_.bg_r = 10; current_attr_.bg_g = 10; current_attr_.bg_b = 18;
                break;
            default:
                if (p >= 30 && p <= 37) { // Standard fg
                    indexed_to_rgb(p - 30, current_attr_.fg_r, current_attr_.fg_g, current_attr_.fg_b);
                } else if (p >= 40 && p <= 47) { // Standard bg
                    indexed_to_rgb(p - 40, current_attr_.bg_r, current_attr_.bg_g, current_attr_.bg_b);
                } else if (p >= 90 && p <= 97) { // Bright fg
                    indexed_to_rgb(p - 90 + 8, current_attr_.fg_r, current_attr_.fg_g, current_attr_.fg_b);
                } else if (p >= 100 && p <= 107) { // Bright bg
                    indexed_to_rgb(p - 100 + 8, current_attr_.bg_r, current_attr_.bg_g, current_attr_.bg_b);
                }
                break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  TERMINAL OPERATIONS
// ═══════════════════════════════════════════════════════════════════════

void Terminal::put_char(uint32_t ch) {
    if (cursor_row_ >= 0 && cursor_row_ < rows_ &&
        cursor_col_ >= 0 && cursor_col_ < cols_) {
        auto& cell = grid_[cursor_row_][cursor_col_];
        cell = current_attr_;
        cell.ch = ch;
    }
    cursor_col_++;
    if (cursor_col_ >= cols_) {
        cursor_col_ = 0;
        newline();
    }
}

void Terminal::newline() {
    cursor_row_++;
    if (cursor_row_ >= rows_) {
        // Scroll
        for (int i = 0; i < rows_ - 1; ++i)
            grid_[i] = std::move(grid_[i + 1]);
        grid_[rows_ - 1].assign(cols_, Cell{});
        cursor_row_ = rows_ - 1;
    }
}

void Terminal::scroll_up() {
    for (int i = 0; i < rows_ - 1; ++i)
        grid_[i] = std::move(grid_[i + 1]);
    grid_[rows_ - 1].assign(cols_, Cell{});
}

void Terminal::clear_screen() {
    for (auto& row : grid_)
        for (auto& cell : row)
            cell = Cell{};
    cursor_row_ = cursor_col_ = 0;
}

void Terminal::clear_to_eol() {
    if (cursor_row_ >= 0 && cursor_row_ < rows_) {
        for (int c = cursor_col_; c < cols_; ++c)
            grid_[cursor_row_][c] = Cell{};
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  KEYBOARD INPUT
// ═══════════════════════════════════════════════════════════════════════

void Terminal::send_char(uint32_t c) {
    char buf[4]; int len = 0;
    if (c < 0x80) { buf[0] = (char)c; len = 1; }
    else if (c < 0x800) { buf[0] = 0xC0|(c>>6); buf[1] = 0x80|(c&0x3F); len = 2; }
    else if (c < 0x10000) { buf[0] = 0xE0|(c>>12); buf[1] = 0x80|((c>>6)&0x3F); buf[2] = 0x80|(c&0x3F); len = 3; }
    else { buf[0]=0xF0|(c>>18); buf[1]=0x80|((c>>12)&0x3F); buf[2]=0x80|((c>>6)&0x3F); buf[3]=0x80|(c&0x3F); len=4; }
    int wp = input_write_.load(std::memory_order_relaxed);
    for (int i = 0; i < len; ++i) { input_buf_[wp % INPUT_BUF_SIZE] = buf[i]; wp = (wp+1) % INPUT_BUF_SIZE; }
    input_write_.store(wp, std::memory_order_release);
}

void Terminal::send_enter()    { send_string("\r"); }
void Terminal::send_backspace(){ send_string("\x7f"); }
void Terminal::send_tab()      { send_string("\t"); }
void Terminal::send_escape()   { send_string("\x1b"); }
void Terminal::send_arrow_up()    { send_string("\x1b[A"); }
void Terminal::send_arrow_down()  { send_string("\x1b[B"); }
void Terminal::send_arrow_right() { send_string("\x1b[C"); }
void Terminal::send_arrow_left()  { send_string("\x1b[D"); }
void Terminal::send_ctrl_c()  { send_string("\x03"); }

void Terminal::send_string(const std::string& s) {
    int wp = input_write_.load(std::memory_order_relaxed);
    for (char c : s) { input_buf_[wp % INPUT_BUF_SIZE] = c; wp = (wp+1) % INPUT_BUF_SIZE; }
    input_write_.store(wp, std::memory_order_release);
}

// ═══════════════════════════════════════════════════════════════════════
//  I/O THREAD
// ═══════════════════════════════════════════════════════════════════════

void Terminal::flush_input() {
    int rp = input_read_.load(std::memory_order_relaxed);
    int wp = input_write_.load(std::memory_order_acquire);
    while (rp != wp) {
        char c = input_buf_[rp % INPUT_BUF_SIZE];
        if (write(pty_fd_, &c, 1) <= 0) break;
        rp = (rp + 1) % INPUT_BUF_SIZE;
    }
    input_read_.store(rp, std::memory_order_release);
}

void Terminal::io_loop() {
    char buf[4096];
    while (running_.load(std::memory_order_relaxed)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pty_fd_, &fds);
        struct timeval tv = {0, 5000}; // 5ms

        int ret = select(pty_fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(pty_fd_, &fds)) {
            ssize_t n = read(pty_fd_, buf, sizeof(buf));
            if (n > 0) {
                for (ssize_t i = 0; i < n; ++i)
                    process_byte((uint8_t)buf[i]);
            } else if (n == 0) {
                running_.store(false, std::memory_order_release);
                break;
            }
        }
        if (pty_fd_ >= 0) flush_input();
    }
}

} // namespace demod::terminal
