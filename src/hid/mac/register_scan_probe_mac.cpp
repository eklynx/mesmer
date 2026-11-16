// Developer tool, not part of the plugin and not in ctest. READ-ONLY: polls any CANopen objects of the yoke's two controllers
// (SDO uploads, never a write) at the sampling rate and prints every change, to find where a control lives. Built for the NG's hat:
// by default it watches 0x2088:00 (the original CLS-E's button slots 17-32, bits 0-15) and 0x60fd:00 (slots 1-16, bits 16-31), so the
// known buttons show beside it as a control.
// Close X-Plane, CLS2Sim and Simux first: the bridge is opened exclusively.
// Build: clang++ -std=c++17 -I src src/hid/mac/register_scan_probe_mac.cpp src/hid/hid_select.cpp src/hid/mac/hid_mac.cpp \
//          -framework IOKit -framework CoreFoundation -pthread -o "$CLAUDE_WORK/derived/mesmer/register_scan_probe"
// Run:   register_scan_probe [seconds] [node1|node2] [tight] [index[:sub] ...]   indexes in hex, e.g.  register_scan_probe 45 0x2088 0x60fd 0x2089:1
//        node1 or node2: only that controller is read; tight: no pause between sweeps, so change times line up to a few milliseconds (each read is
//        about 2 ms), which is how the registers' responsiveness is compared.
#include "brunner/bridge.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>

using namespace brunner;

namespace {
struct Target {unsigned index,sub;};
// What an SDO upload answered: the value and how many bytes it had, or an abort.
struct Answer {std::uint32_t value=0;unsigned bytes=0;bool aborted=false;std::uint32_t abort_code=0;};

std::optional<Answer> read_any(Bridge& bridge,unsigned node,const Target& target,std::uint16_t counter,const std::atomic<bool>& stop) {
    Report request=read_request(node,Object::status,counter);
    request[7]=static_cast<std::uint8_t>(target.index);request[8]=static_cast<std::uint8_t>(target.index>>8);request[9]=static_cast<std::uint8_t>(target.sub);
    Answer answer;
    const auto parse=[&](const Report& r) -> std::optional<std::uint32_t> {
        if (r[0]!=0x3f) throw std::runtime_error("Unexpected HID report ID");
        const unsigned kind=r[1]>>4,count=r[1]&15;
        if (kind>2 || count>(kind==0?0u:kind==1?6u:4u)) throw std::runtime_error("Invalid HID frame layout");
        for (unsigned i=0;i<count;++i) {
            const auto* slot=r.data()+4+i*(kind==2?14:10);
            const unsigned header=slot[0]|(slot[1]<<8);
            if ((header&0x7ffu)!=0x580u+node) continue;
            const auto* d=slot+2;
            if (static_cast<unsigned>(d[1]|(d[2]<<8))!=target.index || d[3]!=target.sub) continue;
            if (d[0]==0x80) {answer.aborted=true;answer.abort_code=d[4]|(d[5]<<8)|(d[6]<<16)|(static_cast<std::uint32_t>(d[7])<<24);return 0u;}
            // Expedited upload: the size is in bits 2-3 of the command byte (0x43 four bytes, 0x47 three, 0x4b two, 0x4f one).
            answer.bytes=4-((d[0]>>2)&3);
            answer.value=d[4]|(d[5]<<8)|(d[6]<<16)|(static_cast<std::uint32_t>(d[7])<<24);
            return answer.value;
        }
        return std::nullopt;
    };
    try {bridge.exchange(request,parse,stop,sample_reply_timeout_ms);}
    catch (const ReplyMissed&) {return std::nullopt;}
    return answer;
}
}

int main(int argc,char** argv) {
    const int seconds=argc>1?std::atoi(argv[1]):45;
    std::vector<Target> targets;
    bool node1_only=false,node2_only=false,tight=false;
    for (int i=2;i<argc;++i) {
        if (std::string(argv[i])=="node1") {node1_only=true;continue;}
        if (std::string(argv[i])=="node2") {node2_only=true;continue;}
        if (std::string(argv[i])=="tight") {tight=true;continue;}
        char* end=nullptr;
        const unsigned index=static_cast<unsigned>(std::strtoul(argv[i],&end,16));
        const unsigned sub=(end && *end==':')?static_cast<unsigned>(std::strtoul(end+1,nullptr,0)):0;
        targets.push_back({index,sub});
    }
    if (targets.empty()) targets={{0x2088,0},{0x60fd,0}};
    try {
        Bridge bridge(default_usb_ids());
        std::atomic<bool> stop{false};
        std::map<std::pair<unsigned,unsigned>,std::uint64_t> last;
        std::set<std::pair<unsigned,unsigned>> told_abort;
        std::uint16_t counter=0;
        std::uint64_t reads=0,changes=0;
        const auto begin=Clock::now();
        while (Clock::now()-begin<std::chrono::seconds(seconds)) {
            for (unsigned node=node2_only?2u:1u;node<=(node1_only?1u:2u);++node)
                for (const auto& target:targets) {
                    const auto answer=read_any(bridge,node,target,counter++,stop);
                    if (!answer) continue;
                    ++reads;
                    const auto key=std::make_pair(node,target.index*256+target.sub);
                    if (answer->aborted) {
                        if (told_abort.insert(key).second) std::printf("node %u 0x%04X:%u: the controller refused (SDO abort 0x%08X)\n",node,target.index,target.sub,answer->abort_code);
                        continue;
                    }
                    const std::uint64_t value=answer->value;
                    const auto seen=last.find(key);
                    if (seen!=last.end() && seen->second==value) continue;
                    const bool first=seen==last.end();
                    last[key]=value;
                    if (!first) ++changes;
                    const double t=std::chrono::duration<double>(Clock::now()-begin).count();
                    std::printf("%8.3fs node %u 0x%04X:%u = 0x%08X (%u bytes)  bits set:",t,node,target.index,target.sub,answer->value,answer->bytes);
                    bool any=false;
                    for (unsigned bit=0;bit<32;++bit) if (answer->value&(1u<<bit)) {std::printf(" %u",bit);any=true;}
                    std::printf("%s",any?"":" none");
                    if (target.index==0x60fd) std::printf("   (CAN slots %s)",(answer->value>>16)?"in bits 16-31: slot = bit-15":"none");
                    if (target.index==0x2088) std::printf("   (CLS2Sim slots 17-32: slot = bit+17)");
                    std::printf("\n");
                    std::fflush(stdout);
                }
            if (!tight) std::this_thread::sleep_for(std::chrono::milliseconds(sample_period_ms));
        }
        std::printf("%llu answers, %llu changes\n",static_cast<unsigned long long>(reads),static_cast<unsigned long long>(changes));
    } catch (const std::exception& e) {
        std::fprintf(stderr,"Could not run: %s\n",e.what());
        return 1;
    }
}
