// Explicit operator-run hardware integration test for Linux, never part of CTest.
// Mirrors hid/mac/smoke_mac.cpp: opens the yoke's bridge exclusively, reads status,
// encoder and position from both controllers for 15 s, then closes it. It
// never asks for force feel or any loading. Like the Windows test it is "read
// only" in that sense; the reader's session start would still disable motors
// it finds already enabled (for example after a power glitch).
// Close X-Plane first: the open takes an exclusive lock (needs the udev rule, docs/dev/linux-build.md).
#include "hid/lin/hid_lin.hpp"
#include "brunner/reader.hpp"
#include <iostream>
int main(int argc,char**) {
    if (argc!=1) {std::cerr<<"No options\n";return 2;}
    std::cout<<"HID devices with a listed bridge id:\n"<<brunner::describe_linux_bridges(brunner::default_usb_ids());
    brunner::Reader reader;reader.start();
    std::uint64_t last=0;
    for (int i=0;i<15;++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto s=reader.snapshot();
        if (!s.connected || s.cycles<=last) {std::cerr<<s.message<<'\n';reader.stop();return 1;}
        last=s.cycles;
        std::cout<<"cycles="<<s.cycles;
        for (const auto& a:s.axes) std::cout<<" firmware="<<a.firmware<<" encoder="<<a.encoder<<" status="<<a.status;
        std::cout<<std::endl;
    }
    reader.stop();
    // After stop() the snapshot may still say how the session went ("Yoke kept its centre ...", reader.cpp), so only
    // the link being closed is checked. (hid/mac/smoke_mac.cpp, never run on hardware, still expects "Disconnected".)
    if (reader.snapshot().connected) {std::cerr<<"Still connected after stop: "<<reader.snapshot().message<<'\n';return 1;}
    std::cout<<"Native read-only hardware test passed; bridge closed\n";
}
