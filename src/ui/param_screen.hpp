#pragma once
#include "ui/screen.hpp"
#include "ui/fx_chain_screen.hpp"
#include "audio/fx_chain.hpp"
#include <vector>

namespace demod::ui {

class ParamScreen : public Screen {
public:
    ParamScreen(audio::FXChainProcessor& fx, FXChainScreen& chain);

    std::string name() const override { return "PARAMS"; }
    std::string help_text() const override {
        return "W/S:Nav  A/D:Adj  R:Rand  BS:Reset  B:Bypass  PgU/D:FX";
    }

    void update(const input::InputManager& input, float dt) override;
    void draw(renderer::Renderer& r) override;

private:
    audio::FXChainProcessor& fx_;
    FXChainScreen&           chain_;

    int   focused_param_  = 0;
    int   scroll_         = 0;
    int   active_fx_      = 0;   // Which FX slot's params we're viewing
    bool  bypass_         = false;
    float repeat_timer_   = 0;

    // Demo params (when no DSP loaded)
    struct DemoParam { std::string label; float value, min, max, init, step; };
    std::vector<DemoParam> demo_params_;
    void init_demo_params();

    int  param_count() const;
    std::string param_label(int i) const;
    float param_normalized(int i) const;
    float param_default_normalized(int i) const;
    void  adjust_param(int i, float delta);
    void  reset_param(int i);
};

} // namespace demod::ui
