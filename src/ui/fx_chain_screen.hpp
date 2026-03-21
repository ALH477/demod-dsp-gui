#pragma once
#include "ui/screen.hpp"
#include "core/config.hpp"
#include <functional>
#include <vector>

namespace demod::audio { class FXChainProcessor; }

namespace demod::ui {

class FXChainScreen : public Screen {
public:
    FXChainScreen();

    std::string name() const override { return "FX CHAIN"; }
    std::string help_text() const override {
        return "W/S:Nav  L:Load  X:Unload  A/D:Wet  B:Bypass  R:Reorder  Enter:Edit";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;

    std::vector<FXSlot>& slots() { return slots_; }

    void set_processor(audio::FXChainProcessor* proc) { processor_ = proc; }

    // Callbacks for load/unload
    void set_on_load(std::function<void(int)> cb) { on_load_ = cb; }
    void set_on_unload(std::function<void(int)> cb) { on_unload_ = cb; }

    int focused_slot() const { return focused_; }

private:
    std::vector<FXSlot> slots_;
    int  focused_     = 0;
    int  scroll_      = 0;
    bool reorder_mode_ = false;
    int  drag_from_   = -1;
    float blink_t_    = 0;

    audio::FXChainProcessor* processor_ = nullptr;

    // Load/unload callbacks
    std::function<void(int)> on_load_;
    std::function<void(int)> on_unload_;

    void init_demo_slots();
    int  visible_slots(int fb_h) const;
    void sync_from_processor();
};

} // namespace demod::ui
