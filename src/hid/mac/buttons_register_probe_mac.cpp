// Developer tool, not part of the plugin and not in ctest. READ-ONLY: polls the yoke bridge's digital-inputs register
// (node 2, object 0x60fd, the one the Windows vJoy path reads for the buttons) at the sampling rate and prints every
// change, so that one can see whether the NG's buttons and hat are in it and which bits they are.
// Close X-Plane, CLS2Sim and Simux first: the bridge is opened exclusively.
// Build: clang++ -std=c++17 -I src src/hid/mac/buttons_register_probe_mac.cpp src/hid/hid_select.cpp src/hid/mac/hid_mac.cpp \
//          -framework IOKit -framework CoreFoundation -pthread -o "$CLAUDE_WORK/derived/mesmer/buttons_register_probe"
// Run:   buttons_register_probe [seconds]   (default 30)
#include "brunner/bridge.hpp"
#include <cstdlib>
#include <iostream>

using namespace brunner;

int main(int argc,char** argv) {
    const int seconds=argc>1?std::atoi(argv[1]):30;
    try {
        Bridge bridge(default_usb_ids());
        std::atomic<bool> stop{false};
        std::uint32_t last[3]={0xffffffffu,0xffffffffu,0xffffffffu};
        const auto begin=Clock::now();
        std::uint64_t reads=0,changes=0;
        while (Clock::now()-begin<std::chrono::seconds(seconds)) {
            for (unsigned node=1;node<=2;++node) {
                std::uint32_t value=0;
                try {value=bridge.read(node,Object::digital_inputs,stop,sample_reply_timeout_ms);}
                catch (const ReplyMissed&) {continue;}
                catch (const SdoAbort& e) {if (last[node]!=0xfffffffeu) std::cout<<"node "<<node<<" 0x60fd: controller refused (abort 0x"<<std::hex<<e.code<<std::dec<<")\n";last[node]=0xfffffffeu;continue;}
                ++reads;
                if (value==last[node]) continue;
                ++changes;
                const double t=std::chrono::duration<double>(Clock::now()-begin).count();
                std::printf("%6.2fs node %u 0x60fd = 0x%08X   slots(bits16-31): ",t,node,value);
                bool any=false;
                for (unsigned bit=16;bit<32;++bit) if (value&(1u<<bit)) {std::printf("%u ",bit-15);any=true;}
                std::printf("%s   low half: 0x%04X\n",any?"":"none",value&0xffff);
                std::fflush(stdout);
                last[node]=value;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sample_period_ms));
        }
        std::cout<<reads<<" reads, "<<changes<<" changes\n";
    } catch (const std::exception& e) {
        std::cerr<<"Could not run: "<<e.what()<<'\n';
        return 1;
    }
}
