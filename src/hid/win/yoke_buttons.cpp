// Windows implementation of YokeButtons. Contract: hid/yoke_buttons.hpp.
#include "hid/yoke_buttons.hpp"
#include "hid/win/buttons.hpp"
#include "hid/win/vjoy_win.hpp"

namespace brunner {
struct YokeButtons::Impl {
    VJoy vjoy;
    ButtonPresser presser;
    ButtonLog changes;
    std::string told_refusal;
};
YokeButtons::YokeButtons():impl_(std::make_unique<Impl>()) {}
YokeButtons::~YokeButtons()=default;

void YokeButtons::update(const Snapshot& s,Clock::time_point now,const Log& log) {
    auto& m=*impl_;
    // All up whenever they are unknown (decision 9). Each change is logged, quick taps included.
    const auto buttons=live_buttons(s,now);
    for (const auto& line:m.presser.update(m.vjoy,buttons,now)) log(line);
    if (const auto told=m.changes.update(buttons,now);!told.empty()) log(told);
    if (s.button_refusal!=m.told_refusal) {
        if (!s.button_refusal.empty()) log("Yoke buttons not read: "+s.button_refusal+"; trying again every 5 seconds");
        else if (s.buttons) log("Yoke buttons read again");
        m.told_refusal=s.button_refusal;
    }
}
void YokeButtons::close() {
    auto& m=*impl_;
    m.presser.close(m.vjoy);m.changes=ButtonLog{};m.told_refusal.clear();
}
void YokeButtons::unload() {impl_->vjoy.unload();}
}
