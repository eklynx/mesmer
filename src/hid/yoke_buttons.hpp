// SOURCE OF TRUTH for the yoke's buttons on a virtual joystick: how the plugin and the
// reader reach them without knowing the platform. Only Windows has them (Brunner's vJoy
// driver); the logic is in hid/win/buttons.hpp, the vJoy feeder in hid/win/vjoy_win.hpp,
// the owner of both in hid/win/yoke_buttons.cpp. Everywhere else every call below does
// nothing (hid/hid_select.cpp), so no caller has a platform branch. Pitch, roll and force
// feel never go through here. The NG's own HID joystick is X-Plane's, not ours.
//
// Thread: YokeButtons runs on X-Plane's thread only. sample_buttons runs on the reader's.
#pragma once
#include "brunner/core.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#ifdef _WIN32
#include "hid/win/buttons.hpp"
#endif

namespace brunner {
#ifndef _WIN32
// The reader's read of node 2's button word (hid/win/buttons.hpp): not made off Windows.
template<class Link> void sample_buttons(Link&,Snapshot&,const std::atomic<bool>&,Clock::time_point) {}
#endif

// The yoke's buttons 1 to 16 on vJoy device 1.
//  Happy path: update() once per flight-loop frame takes the device when it can, lets every
//    button up, then presses and lets up the buttons as the snapshot's buttons change (only
//    changes are sent); every change and every refusal reaches `log` as a line for Log.txt.
//    All buttons are up whenever the snapshot's buttons are unknown (no link, or readings
//    older than 250 ms).
//  Error cases: never thrown. No vJoy, vJoy switched off, no buttons on device 1, or another
//    program holding it: said once in `log` ("Yoke buttons off: <why>; trying again every 5
//    seconds") and tried again every 5 seconds. A button the driver refuses lets all up and
//    gives the device back. The yoke flies exactly as before in every case.
class YokeButtons {
 public:
    using Log=std::function<void(const std::string&)>;
    YokeButtons();
    ~YokeButtons();
    void update(const Snapshot& snapshot,Clock::time_point now,const Log& log);
    // All buttons up and the device given back, as when the plugin is disabled.
    void close();
    // Frees the driver's DLL: the plugin's XPluginStop, never a destructor.
    void unload();
 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
