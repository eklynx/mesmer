#pragma once
// X-Plane's stalled shares, slewed before the models take them. Its panel
// flags flip a whole section at once, so a share read straight would step
// the hinge moment's kept share from one frame to the next; the model and
// its vectors are unchanged, only what reaches them moves at no more than
// full scale (0 to 1) in stall_slew_full_scale_s. The recorder keeps the raw
// share; the workbench's state shows both. Portable: no X-Plane headers.
#include "force/flight.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace brunner {

inline constexpr double stall_slew_full_scale_s=0.25;

// One share. It starts at its first finite reading, then moves toward each
// reading by at most dt/stall_slew_full_scale_s, dt from the flight clock
// (seconds; a paused clock holds it). A non-finite reading, or a frame
// without one, holds the share and counts the clock, so the gap banks no
// time: when the reading returns the share moves on from where it was, at
// the same pace, rather than stepping to it. Before the first reading a
// non-finite one passes through. A non-finite clock, or one that runs
// backwards (a new flight), forgets the share: it restarts at the next
// finite reading. reset() forgets it outright.
struct ShareSlew {
    double value=std::numeric_limits<double>::quiet_NaN(),clock_s=std::numeric_limits<double>::quiet_NaN();
    double at(double raw,double now_s) {
        const bool restart=!std::isfinite(value) || !std::isfinite(clock_s) || !std::isfinite(now_s) || now_s<clock_s;
        if (!std::isfinite(raw)) {
            if (restart) value=std::numeric_limits<double>::quiet_NaN();
            clock_s=now_s;
            return std::isfinite(value)?value:raw;
        }
        const double target=std::clamp(raw,0.0,1.0);
        if (restart) value=target;
        else {
            const double most=(now_s-clock_s)/stall_slew_full_scale_s;
            value+=std::clamp(target-value,-most,most);
        }
        clock_s=now_s;
        return value;
    }
    void reset() {*this=ShareSlew{};}
};

// The tail's and each aileron's share, called once per X-Plane frame with the
// frame's sample: the same sample with its stalled shares slewed, for the
// models. A surface missing from the sample (a frame X-Plane's panels could
// not be read) holds its share as a non-finite reading does, so a one-frame
// gap does not step the share when the surface returns.
struct StallSlews {
    ShareSlew tail,left,right;
    FlightSample slewed(const FlightSample& raw) {
        constexpr double gap=std::numeric_limits<double>::quiet_NaN();
        auto s=raw;
        if (s.tail) s.tail->stalled=tail.at(s.tail->stalled,s.physics_time);
        else tail.at(gap,s.physics_time);
        if (s.ailerons) {
            s.ailerons->left.stalled=left.at(s.ailerons->left.stalled,s.physics_time);
            s.ailerons->right.stalled=right.at(s.ailerons->right.stalled,s.physics_time);
        } else {left.at(gap,s.physics_time);right.at(gap,s.physics_time);}
        return s;
    }
};

} // namespace brunner
