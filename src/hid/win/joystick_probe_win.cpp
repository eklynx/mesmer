// Developer tool, not part of the plugin and not in ctest (like the other hardware tools); the Windows twin of
// hid/mac/joystick_probe_mac.cpp, but through the plugin's own code: hid::open_joystick (hid/win/joystick_win.cpp)
// and the shared decoder (hid/report_decoder.hpp). Prints the layout Windows' parse gives, then every input report
// as hex with its decoded buttons and hat, until the time runs out. Observe only: the interface is opened shared and
// read-only, so X-Plane keeps working.
//
// Build: part of the Windows CMake build (brunner_joystick_probe).
// Run:   brunner_joystick_probe [seconds]   (default 30)
#include "hid/joystick_device.hpp"
#include "hid/usb_ids.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {
void print_field(const char* name,const hid::BitField& f) {std::printf("  %s: bits %d..%d\n",name,f.first_bit,f.first_bit+f.bit_count-1);}
}
int main(int argc,char** argv) {
    const int seconds=argc>1?std::atoi(argv[1]):30;
    try {
        std::string why;
        auto device=hid::open_joystick(brunner::brunner_vendor,why);
        if (!device) {std::printf("No joystick interface opened: %s\n",why.c_str());return 1;}
        std::printf("Joystick interface %04X:%04X\n",device->vendor(),device->product());
        const auto layout=device->layout();
        if (!layout) {std::printf("No layout: no buttons or hat in Windows' parse.\n");return 1;}
        std::printf("Layout: %zu payload bytes, report ID %s, %zu buttons\n",layout->payload_bytes(),layout->has_report_id?"yes":"no",layout->buttons.size());
        for (std::size_t n=0;n<layout->buttons.size();++n) std::printf("  button %zu: bit %d\n",n+1,layout->buttons[n].first_bit);
        if (layout->hat) {print_field("hat",*layout->hat);std::printf("  hat logical %d..%d\n",layout->hat_logical_min,layout->hat_logical_max);}
        for (const auto& run:layout->padding) print_field("padding",run);
        std::printf("\nListening %d s (shared, read-only). Press the buttons and the hat.\n",seconds);
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        std::vector<std::uint8_t> payload;
        while (std::chrono::steady_clock::now()<end) {
            if (!device->read(payload,100)) continue;
            std::printf("len=%zu:",payload.size());
            for (const auto byte:payload) std::printf(" %02x",byte);
            if (const auto state=hid::decode_report(*layout,payload.data(),payload.size()))
                std::printf("   buttons=0x%02x hat=%d%s",state->buttons,state->hat,state->padding_clear?"":" PADDING SET");
            else std::printf("   (too short)");
            std::printf("\n");
            std::fflush(stdout);
        }
        return 0;
    } catch (const std::exception& e) {
        std::printf("Failed: %s\n",e.what());
        return 1;
    }
}
