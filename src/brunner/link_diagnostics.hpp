// SOURCE OF TRUTH for the facts kept to explain a stalled yoke link. Specification: docs/dev/hid-bridge.md,
// "When the link stalls". Written by brunner/bridge.hpp as it exchanges requests, read by brunner/reader.cpp when a
// session ends in a transport error; the text goes to Log.txt (and the recording, if debug_log is on) once per loss.
#pragma once
#include "brunner/core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace brunner {
// What a request in flight was, from its bytes (an SDO request to one of the two controllers): "node 2 read of trim
// position (0x2116:0)" or "node 1 write of spring strength (0x2114:0) = 40". Byte 4 is the node, byte 6 the SDO command
// (0x40 upload = a read, 0x2b / 0x23 a write of two / four bytes), bytes 7-9 the object and sub-index and 10-13 the value
// of a write. The names are the registers' names in brunner/reference.hpp, loading.hpp, vibration.hpp and core.hpp.
inline std::string request_text(const std::array<std::uint8_t,14>& r) {
    const unsigned index=r[7]|(r[8]<<8),sub=r[9];
    const char* name=nullptr;
    switch (index) {
    case 0x3018: name="firmware version"; break;
    case 0x6041: name="status word (sampling)"; break;
    case 0x6064: name="encoder position (sampling)"; break;
    case 0x60fd: name="digital inputs (buttons)"; break;
    case 0x6040: name="control word"; break;
    case 0x2101: name=sub==1?"force mode":sub==4?"centre mode":"force-mode register"; break;
    case 0x2114: name="spring strength"; break;
    case 0x211b: name="force limit"; break;
    case 0x6072: name="torque limit"; break;
    case 0x6071: name="target torque"; break;
    case 0x2115: name="trim offset"; break;
    case 0x2116: name="trim position"; break;
    case 0x2117: name="centre position"; break;
    case 0x211a: name="constant force (bias)"; break;
    case 0x211f: name="constant-force cut-off"; break;
    case 0x2100: name=sub==1?"drag gain numerator":sub==2?"drag gain denominator":"drag gain register"; break;
    case 0x2102: name="move-back gain"; break;
    case 0x2113: name="spring curve point"; break;
    case 0x2108: name="buffet slow force"; break;
    case 0x2109: name="buffet slow time"; break;
    case 0x2099: name="buffet fast force"; break;
    case 0x209a: name="buffet fast time"; break;
    default: break;
    }
    char id[40];
    std::snprintf(id,sizeof(id),"0x%04X:%u",index,sub);
    std::string text="node "+std::to_string(static_cast<unsigned>(r[4]))+" ";
    const bool write=r[6]==0x2b || r[6]==0x23;
    text+=write?"write of ":r[6]==0x40?"read of ":"request (SDO command "+std::to_string(static_cast<unsigned>(r[6]))+") for ";
    text+=std::string(name?name:"unnamed register")+(index==0x2113?" "+std::to_string(sub):"")+" ("+id+")";
    if (write) {
        std::uint32_t value=r[10]|(r[11]<<8);
        if (r[6]==0x23) value|=(static_cast<std::uint32_t>(r[12])<<16)|(static_cast<std::uint32_t>(r[13])<<24);
        text+=" = "+std::to_string(r[6]==0x23?static_cast<std::int64_t>(static_cast<std::int32_t>(value)):static_cast<std::int64_t>(value));
    }
    return text;
}
// What the bridge was doing, kept so that a stall can be explained afterwards (the plugin logs describe() once
// per lost link, with debug_log on or off). Written by the reader thread as it exchanges requests and read on the
// same thread when the session ends, so it needs no lock. Cheap: a few stores per exchange, no formatting.
// Specification: docs/dev/hid-bridge.md, "When the link stalls".
struct LinkDiagnostics {
    enum class Stage {idle,writing,waiting} stage=Stage::idle;
    Clock::time_point session_start{},last_write{},last_reply{};
    std::uint64_t exchanges=0,replies=0,unmatched_total=0,retries=0;   // retries: loading writes and non-sampling reads sent again for want of a reply
    std::uint32_t unmatched_in_exchange=0;   // reports that arrived but were not the reply, in the current exchange
    std::array<std::uint8_t,14> request_head{};   // the first bytes of the request in flight (enough for request_text)
    std::string os_account;                       // HidDevice::describe(), taken at the moment a transport error was thrown
    std::string at_error;                         // describe() taken at that same moment: the later emergency stop commands reuse the fields above
    // How long replies took (request written to matching reply read), this session: the count in each bucket
    // (under 1, 2, 5, 10, 25, 50, 100 and 250 ms, and over) and the slowest, in microseconds. Evidence for the
    // reply timeout in Bridge::exchange.
    static constexpr std::array<int,8> bucket_ms{1,2,5,10,25,50,100,250};
    std::array<std::uint64_t,9> latency{};
    std::int64_t slowest_us=0;
    void note_latency(Clock::time_point sent,Clock::time_point got) {
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(got-sent).count();
        slowest_us=std::max<std::int64_t>(slowest_us,us);
        std::size_t at=0;
        while (at<bucket_ms.size() && us>=static_cast<std::int64_t>(bucket_ms[at])*1000) ++at;
        ++latency[at];
    }
    std::string latency_text() const {
        std::string text;
        for (std::size_t i=0;i<latency.size();++i) {
            if (!latency[i]) continue;
            text+=(text.empty()?"":", ")+std::string(i<bucket_ms.size()?"<"+std::to_string(bucket_ms[i])+" ms: ":">=250 ms: ")+std::to_string(latency[i]);
        }
        return (text.empty()?"no replies":text)+"; slowest "+std::to_string(slowest_us/1000)+"."+std::to_string((slowest_us%1000)/100)+" ms";
    }
    void begin_session(Clock::time_point now) {*this=LinkDiagnostics{};session_start=now;last_reply=now;}
    // One line: where the failure was, what was in flight (its kind and register by name, then its bytes), and how long each side has been quiet.
    std::string describe(Clock::time_point now,const std::string& error) const {
        const auto ms=[&](Clock::time_point from) {
            return from==Clock::time_point{}?std::string("never"):std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now-from).count())+" ms ago";
        };
        std::string head;
        for (const auto byte:request_head) {char hex[4];std::snprintf(hex,sizeof(hex),"%02x",static_cast<unsigned>(byte));head+=(head.empty()?"":" ")+std::string(hex);}
        const char* where=stage==Stage::writing?"while writing a request":stage==Stage::waiting?"while waiting for the reply":"between requests";
        return "Yoke link stall: "+error+" "+where+"; request "+request_text(request_head)+" ["+head+"] sent "+ms(last_write)+", last good reply "+ms(last_reply)
            +", "+std::to_string(unmatched_in_exchange)+" other reports arrived since it was sent; "+std::to_string(exchanges)+" requests and "
            +std::to_string(replies)+" replies this session ("+std::to_string(unmatched_total)+" unmatched, "+std::to_string(retries)+" retries; reply times "+latency_text()+"), session "
            +std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now-session_start).count())+" s old"+(os_account.empty()?"":"; "+os_account);
    }
};
}
