// Explicit operator-run hardware integration test for macOS, never part of CTest.
// Mirrors smoke_win.cpp: opens the yoke's bridge exclusively, reads status,
// encoder and position from both controllers for 15 s, then closes it. It
// never asks for force feel or any loading. Like the Windows test it is "read
// only" in that sense; the reader's session start would still disable motors
// it finds already enabled (for example after a power glitch).
// Close X-Plane, CLS2Sim and Simux first: the open is exclusive.
#include "hid_mac.hpp"
#include "reader.hpp"
#include <iostream>
int main(int argc,char**) {
    if (argc!=1) {std::cerr<<"No options\n";return 2;}
    std::cout<<"HID devices with a listed bridge id:\n"<<brunner::describe_mac_bridges();
    brunner::Reader reader;reader.start();
    std::uint64_t last=0;
    for (int i=0;i<15;++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto s=reader.snapshot();
        if (!s.connected || s.cycles<=last) {std::cerr<<s.message<<'\n';reader.stop();return 1;}
        last=s.cycles;
        std::cout<<"cycles="<<s.cycles;
        for (const auto& a:s.axes) std::cout<<" firmware="<<a.firmware<<" encoder="<<a.encoder<<" control="<<a.position<<" status="<<a.status;
        std::cout<<std::endl;
    }
    reader.stop();
    if (reader.snapshot().connected || reader.snapshot().message!="Disconnected") return 1;
    std::cout<<"Native read-only hardware test passed; bridge closed\n";
}
