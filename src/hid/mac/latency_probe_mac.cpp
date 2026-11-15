// Developer tool, not part of the plugin and not in ctest. Measures how long the yoke's bridge takes to answer
// read requests, for choosing the reply timeout in Bridge::exchange (brunner/bridge.hpp). READ-ONLY: it sends only
// SDO uploads of status and encoder (the reads a sampling cycle makes), never a write or a motor command.
// Close X-Plane, CLS2Sim and Simux first: the bridge is opened exclusively.
// Build: clang++ -std=c++17 -I src src/hid/mac/latency_probe_mac.cpp src/hid/hid_select.cpp src/hid/mac/hid_mac.cpp \
//          -framework IOKit -framework CoreFoundation -pthread -o "$CLAUDE_WORK/derived/mesmer/latency_probe"
// Run:   latency_probe [seconds per mode]   (default 30)
#include "brunner/bridge.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace brunner;

int main(int argc,char** argv) {
    const int seconds=argc>1?std::atoi(argv[1]):30;
    try {
        Bridge bridge(default_usb_ids());
        for (const bool paced:{false,true}) {
            LinkDiagnostics diag;
            bridge.set_diagnostics(&diag);
            std::atomic<bool> stop{false};
            std::uint64_t requests=0,missed=0;
            std::vector<double> latencies_ms;
            const auto end=Clock::now()+std::chrono::seconds(seconds);
            while (Clock::now()<end) {
                for (unsigned node=1;node<=2;++node)
                    for (const auto object:{Object::status,Object::encoder}) {
                        ++requests;
                        const auto before=diag.replies;
                        const auto sent=Clock::now();
                        try {bridge.read(node,object,stop);}
                        catch (const ReplyMissed& e) {++missed;std::cout<<"  missed: "<<e.what()<<'\n';}
                        if (diag.replies>before) latencies_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-sent).count());
                    }
                if (paced) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::sort(latencies_ms.begin(),latencies_ms.end());
            const auto at=[&](double q) {return latencies_ms.empty()?0.0:latencies_ms[std::min(latencies_ms.size()-1,static_cast<std::size_t>(q*latencies_ms.size()))];};
            std::cout<<(paced?"PACED (20 ms between cycles)":"FLAT OUT")<<": "<<requests<<" requests in "<<seconds<<" s, "<<missed<<" missed\n"
                     <<"  p50 "<<at(0.50)<<" ms, p90 "<<at(0.90)<<" ms, p99 "<<at(0.99)<<" ms, p99.9 "<<at(0.999)<<" ms, max "<<(latencies_ms.empty()?0.0:latencies_ms.back())<<" ms\n"
                     <<"  buckets: "<<diag.latency_text()<<'\n';
        }
    } catch (const std::exception& e) {
        std::cerr<<"Could not run: "<<e.what()<<'\n';
        return 1;
    }
}
