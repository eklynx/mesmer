#pragma once
#include "brunner/core.hpp"

namespace brunner {
// Simulator-only input lease. Never authorizes a device write or motor start.
class DiagnosticTrim {
    int target_=0;
    Clock::time_point until_{};
public:
    int target() const {return target_;}
    bool set_target(int value) {
        if (value < -500 || value > 500) return false;
        target_=value;return true;
    }
    bool renew(int milliseconds,bool hardware_idle,Clock::time_point now) {
        if (milliseconds==0) {until_={};return true;}
        if (!hardware_idle || milliseconds<100 || milliseconds>5000) return false;
        until_=now+std::chrono::milliseconds(milliseconds);return true;
    }
    bool active(bool hardware_idle,Clock::time_point now) {
        if (!hardware_idle || now>=until_) {until_={};return false;}
        return true;
    }
};
}
