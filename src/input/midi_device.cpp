// ╔══════════════════════════════════════════════════════════════════════╗
// ║  DeMoDOOM — MIDI Device (RtMidi)                                   ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "input/midi_device.hpp"
#include <cstdio>
#include <cstring>
#include <SDL2/SDL.h>

#ifdef HAVE_RTMIDI
#include <RtMidi.h>
#endif

namespace demod::input {

MidiDevice::MidiDevice(int port_index) : port_index_(port_index) {}

MidiDevice::~MidiDevice() { close(); }

std::string MidiDevice::name() const {
    if (!port_name_.empty()) return port_name_;
    return "MIDI";
}

std::string MidiDevice::type_tag() const { return "midi"; }

bool MidiDevice::connected() const { return connected_; }

bool MidiDevice::open() {
#ifdef HAVE_RTMIDI
    if (midi_in_) return true;

    try {
        midi_in_ = new RtMidiIn(RtMidi::UNSPECIFIED, "DeMoDOOM");

        unsigned int n_ports = midi_in_->getPortCount();
        if (n_ports == 0) {
            fprintf(stderr, "[MIDI] No input ports available\n");
            delete midi_in_;
            midi_in_ = nullptr;
            return false;
        }

        int port = port_index_;
        if (port < 0 || port >= (int)n_ports) port = 0;

        port_name_ = midi_in_->getPortName(port);
        midi_in_->openPort(port);
        midi_in_->ignoreTypes(false, false, false);  // Don't ignore any messages
        connected_ = true;

        fprintf(stderr, "[MIDI] Opened: %s (port %d/%d)\n",
                port_name_.c_str(), port, n_ports);
        return true;
    } catch (const RtMidiError& e) {
        fprintf(stderr, "[MIDI] Open failed: %s\n", e.what());
        delete midi_in_;
        midi_in_ = nullptr;
        return false;
    }
#else
    (void)port_index_;
    fprintf(stderr, "[MIDI] Not available (RtMidi not linked)\n");
    return false;
#endif
}

void MidiDevice::close() {
#ifdef HAVE_RTMIDI
    if (midi_in_) {
        if (midi_in_->isPortOpen())
            midi_in_->closePort();
        delete midi_in_;
        midi_in_ = nullptr;
    }
#endif
    connected_ = false;
}

void MidiDevice::poll(std::vector<RawEvent>& events_out) {
#ifdef HAVE_RTMIDI
    if (!midi_in_ || !connected_) return;

    uint64_t now = 0;
    std::vector<unsigned char> message;

    while (true) {
        message.clear();
        midi_in_->getMessage(&message);
        if (message.empty()) break;

        now = SDL_GetPerformanceCounter() * 1000000ULL /
              SDL_GetPerformanceFrequency();

        unsigned char status = message[0];
        unsigned char type   = status & 0xF0;
        // unsigned char ch  = status & 0x0F;  // channel (reserved for future per-channel bindings)

        switch (type) {
        case 0x90: // Note On
            if (message.size() >= 3) {
                unsigned char note = message[1];
                unsigned char vel  = message[2];
                if (vel > 0) {
                    note_state_[note] = 1;
                    events_out.push_back({
                        RawEvent::Type::BUTTON_DOWN,
                        (int)note,
                        float(vel) / 127.0f,
                        now
                    });
                } else {
                    // Velocity 0 = note off
                    note_state_[note] = 0;
                    events_out.push_back({
                        RawEvent::Type::BUTTON_UP,
                        (int)note,
                        0.0f,
                        now
                    });
                }
            }
            break;

        case 0x80: // Note Off
            if (message.size() >= 3) {
                unsigned char note = message[1];
                note_state_[note] = 0;
                events_out.push_back({
                    RawEvent::Type::BUTTON_UP,
                    (int)note,
                    0.0f,
                    now
                });
            }
            break;

        case 0xB0: // Control Change
            if (message.size() >= 3) {
                unsigned char cc  = message[1];
                unsigned char val = message[2];
                cc_state_[cc] = val;
                events_out.push_back({
                    RawEvent::Type::AXIS_MOVE,
                    CC_BASE + (int)cc,
                    float(val) / 127.0f,
                    now
                });
            }
            break;

        case 0xE0: // Pitch Bend
            if (message.size() >= 3) {
                int lsb = message[1];
                int msb = message[2];
                int raw = (msb << 7) | lsb;         // 0-16383
                float norm = (float(raw) - 8192.0f) / 8192.0f;  // -1.0 to 1.0
                pitch_bend_active_ = (raw != 8192);
                events_out.push_back({
                    RawEvent::Type::AXIS_MOVE,
                    PB_ID,
                    norm,
                    now
                });
            }
            break;

        case 0xC0: // Program Change
            if (message.size() >= 2) {
                unsigned char prog = message[1];
                events_out.push_back({
                    RawEvent::Type::BUTTON_DOWN,
                    PC_BASE + (int)prog,
                    1.0f,
                    now
                });
            }
            break;

        default:
            // SysEx, aftertouch, etc. — ignored
            break;
        }
    }
#else
    (void)events_out;
#endif
}

int MidiDevice::port_count() {
#ifdef HAVE_RTMIDI
    try {
        RtMidiIn tmp(RtMidi::UNSPECIFIED, "");
        return (int)tmp.getPortCount();
    } catch (...) {}
#endif
    return 0;
}

std::string MidiDevice::port_name(int index) {
#ifdef HAVE_RTMIDI
    try {
        RtMidiIn tmp(RtMidi::UNSPECIFIED, "");
        if (index >= 0 && index < (int)tmp.getPortCount())
            return tmp.getPortName(index);
    } catch (...) {}
#endif
    (void)index;
    return "";
}

std::vector<Binding> MidiDevice::default_bindings() const {
    std::vector<Binding> b;

    // CC bindings — common MIDI controller mappings
    // Source IDs: CC_BASE + CC number
    b.push_back({ CC_BASE + 1,  Action::AXIS_X,  0.0f, 1.0f, true });  // CC1  Mod wheel
    b.push_back({ CC_BASE + 7,  Action::AXIS_Y,  0.0f, 1.0f, true });  // CC7  Volume
    b.push_back({ CC_BASE + 10, Action::AXIS_Z,  0.0f, 1.0f, true });  // CC10 Pan
    b.push_back({ CC_BASE + 11, Action::AXIS_W,  0.0f, 1.0f, true });  // CC11 Expression
    b.push_back({ CC_BASE + 64, Action::TRANSPORT_PLAY, 0.0f, 1.0f, false }); // CC64 Sustain → Play

    // Pitch bend
    b.push_back({ PB_ID, Action::AXIS_X, 0.0f, 1.0f, true });

    // Note bindings (middle C = note 60)
    // These map individual notes to actions for standalone use
    b.push_back({ 60, Action::NAV_SELECT,    0.0f, 1.0f, false });  // C4  → Select
    b.push_back({ 62, Action::NAV_UP,        0.0f, 1.0f, false });  // D4  → Up
    b.push_back({ 64, Action::NAV_DOWN,      0.0f, 1.0f, false });  // E4  → Down
    b.push_back({ 65, Action::NAV_LEFT,      0.0f, 1.0f, false });  // F4  → Left
    b.push_back({ 67, Action::NAV_RIGHT,     0.0f, 1.0f, false });  // G4  → Right
    b.push_back({ 72, Action::MENU_OPEN,     0.0f, 1.0f, false });  // C5  → Menu
    b.push_back({ 74, Action::PARAM_INC,     0.0f, 1.0f, false });  // D5  → Param +
    b.push_back({ 71, Action::PARAM_DEC,     0.0f, 1.0f, false });  // B4  → Param -
    b.push_back({ 76, Action::SCREEN_NEXT,   0.0f, 1.0f, false });  // E5  → Next screen
    b.push_back({ 79, Action::BYPASS_TOGGLE, 0.0f, 1.0f, false });  // G5  → Bypass
    b.push_back({ 81, Action::QUIT,          0.0f, 1.0f, false });  // A5  → Quit

    return b;
}

} // namespace demod::input
