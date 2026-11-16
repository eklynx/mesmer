// Explicit operator-run hardware query for Linux, never part of CTest. Opens the yoke's bridge
// exclusively with the NG's preset treated as NOT verified and the owner's toggle off, which is
// read-only mode (docs/dev/read-only-mode.md): nothing is written. It serves the register survey once
// and prints the force mode (0x2101:01) of both controllers with the survey JSON.
// Close X-Plane first: the open takes an exclusive lock (needs the udev rule, docs/dev/linux-build.md).
#include "brunner/reader.hpp"
#include "brunner/yoke_preset.hpp"
#include "hid/lin/hid_lin.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char**) {
    if (argc!=1) {std::cerr<<"No options\n";return 2;}
    std::cout<<"HID devices with a listed bridge id:\n"<<brunner::describe_linux_bridges(brunner::default_usb_ids());
    auto presets=brunner::builtin_yoke_presets();
    for (auto& p:presets) {p.verified=false;p.verified_firmware.clear();}   // read-only whatever the shipped files say
    brunner::Reader reader;
    reader.set_presets(presets);
    reader.set_allow_unverified(false);
    reader.start();
    bool asked=false;
    for (int i=0;i<30;++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto s=reader.snapshot();
        if (!asked && s.connected && s.read_only && s.cycles>2) asked=reader.request_survey();
        if (s.surveys>0) {
            std::cout<<"model="<<s.model<<" firmware="<<s.axes[0].firmware<<"/"<<s.axes[1].firmware<<" status="<<s.axes[0].status<<"/"<<s.axes[1].status<<"\n";
            std::cout<<s.survey<<"\n";
            reader.stop();
            return 0;
        }
    }
    const auto s=reader.snapshot();
    std::cerr<<"No survey: connected="<<s.connected<<" read_only="<<s.read_only<<" message="<<s.message<<"\n";
    reader.stop();
    return 1;
}
