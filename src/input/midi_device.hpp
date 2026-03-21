#pragma once
#include "input/device.hpp"
#include <string>
#include <vector>
#include <cstdint>

// Forward declare RtMidi to avoid pulling headers into the header
class RtMidiIn;

namespace demod::input {

class MidiDevice : public InputDevice {
public:
    // port_index: which RtMidi input port to open (-1 = first available)
    explicit MidiDevice(int port_index = -1);
    ~MidiDevice() override;

    std::string name()      const override;
    std::string type_tag()  const override;
    bool        connected() const override;

    bool open()  override;
    void close() override;

    void poll(std::vector<RawEvent>& events_out) override;
    std::vector<Binding> default_bindings() const override;

    // Enumerate available MIDI input ports
    static int port_count();
    static std::string port_name(int index);

private:
    int         port_index_;
    RtMidiIn*   midi_in_    = nullptr;
    bool        connected_  = false;
    std::string port_name_;

    // State cache for edge detection
    uint8_t note_state_[128]  = {};  // 0 or 1
    uint8_t cc_state_[128]    = {};  // 0-127

    // Pitch bend center tracking
    bool pitch_bend_active_   = false;

    // MIDI source ID offsets
    // CC:     1000 + CC number (1000-1127)
    // Notes:  note number directly (0-127, collides with keyboard scancodes
    //         but that's fine — bindings are per-device)
    // PB:     2000 (pitch bend)
    // PC:     3000 + program number
    static constexpr int CC_BASE     = 1000;
    static constexpr int PB_ID       = 2000;
    static constexpr int PC_BASE     = 3000;
};

} // namespace demod::input
