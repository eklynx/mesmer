// Tests for read-only mode (brunner/readonly.hpp, spec docs/read-only-mode.md): firmware
// the plugin does not drive. The point of most of them is the guarantee: on such
// firmware nothing but read requests is ever sent to a controller. No hardware.
#include "brunner/bridge.hpp"
#include "config.hpp"
#include "brunner/owner_state.hpp"
#include "brunner/readonly.hpp"
#include "brunner/reader.hpp"
#include "brunner/yoke.hpp"
#include <cmath>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <type_traits>

using namespace brunner;
// The tests give the reader their own devices; the real one is never opened here.
std::unique_ptr<HidDevice> brunner::open_bridge(const std::vector<UsbId>&,const std::vector<std::uint16_t>&) {throw BridgeAbsent{};}
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(x) check_at((x),__LINE__)
bool near(float a,float b) {return std::fabs(a-b)<1e-4f;}

// ---- The guarantee, at the type level: ReadOnlyBridge has no way to write.
template<class T,class=void> struct can_load_write:std::false_type {};
template<class T> struct can_load_write<T,std::void_t<decltype(std::declval<T&>().load_write(1u,RegisterWrite{},std::declval<const std::atomic<bool>&>()))>>:std::true_type {};
template<class T,class=void> struct can_ref_write:std::false_type {};
template<class T> struct can_ref_write<T,std::void_t<decltype(std::declval<T&>().ref_write(1u,RegisterWrite{},std::declval<const std::atomic<bool>&>()))>>:std::true_type {};
struct NoParse {std::optional<std::uint32_t> operator()(const Report&) const {return std::nullopt;}};
template<class T,class=void> struct can_exchange:std::false_type {};
template<class T> struct can_exchange<T,std::void_t<decltype(std::declval<T&>().exchange(Report{},NoParse{},std::declval<const std::atomic<bool>&>()))>>:std::true_type {};
static_assert(can_load_write<Bridge>::value && can_ref_write<Bridge>::value && can_exchange<Bridge>::value,"the detector must see Bridge's write members");
static_assert(!can_load_write<ReadOnlyBridge>::value && !can_ref_write<ReadOnlyBridge>::value && !can_exchange<ReadOnlyBridge>::value,
    "ReadOnlyBridge must have no write members");

// A controller pair. Answers SDO uploads from a table of registers and takes SDO writes; any
// register not in the table aborts, as one a controller lacks would. It logs every report
// written, and separately the ones that were writes. Defaults mimic the CLS-E NG.
struct ControllerFake : HidDevice {
    UsbId usb;
    std::vector<Report>* sent;      // every report written
    std::vector<Report>* writes;    // the SDO writes among them
    std::deque<Report> replies;
    std::map<std::uint32_t,std::uint32_t> regs;
    std::uint16_t firmware[3]={0,642,642};   // by node
    bool refuse_writes=false;
    int encoder=100;
    // A model that idles with its motors enabled (the NG): status 0x1427 until a shutdown
    // controlword (low nibble 6) is written, then 0x0421. Set again, it is enabled again.
    std::atomic<bool> enabled{false};
    bool tracks_state=false;      // false: the status stays 0x0010, as the older tests expect
    static std::uint32_t key(unsigned node,unsigned index,unsigned sub) {return (node<<24)|(index<<8)|sub;}
    explicit ControllerFake(std::vector<Report>* log,std::vector<Report>* write_log,UsbId id={0x25bb,0x008b}):usb(id),sent(log),writes(write_log) {
        const bool cls_e=id.product==0x0081;
        for (unsigned node=1;node<=2;++node) {
            const std::uint32_t limit=cls_e?4500:(node==1?2150:2250);
            regs[key(node,0x211b,0)]=limit;regs[key(node,0x6072,0)]=limit;
            regs[key(node,0x2114,0)]=cls_e?100:(node==1?180:160);
            regs[key(node,0x2101,1)]=cls_e?0x2205:0x2605;      // resting force mode with the reference request set
            regs[key(node,0x6040,0)]=cls_e?0x0006:0x006f;
        }
        if (cls_e) firmware[1]=firmware[2]=367;
    }
    // The rest of what a session reads, as the NG's register survey showed it (2026-11-12). Off by
    // default: the survey tests rely on a controller that lacks some registers.
    void surveyed_registers() {
        for (unsigned node=1;node<=2;++node) {
            static const unsigned curve[9]={50,150,250,350,450,550,650,800,950};
            for (unsigned i=1;i<=18;++i) {regs[key(node,0x2113,i)]=i<=9?curve[i-1]:0;regs[key(node,0x20a1,i)]=0;}
            regs[key(node,0x2099,0)]=0;regs[key(node,0x209a,0)]=100;regs[key(node,0x2100,1)]=1;regs[key(node,0x2100,2)]=node==1?6:3;
            regs[key(node,0x2101,4)]=0;regs[key(node,0x2102,0)]=200;regs[key(node,0x2108,0)]=0;regs[key(node,0x2109,0)]=100;
            regs[key(node,0x2115,0)]=0;regs[key(node,0x2116,0)]=0;regs[key(node,0x2117,0)]=0;regs[key(node,0x211a,0)]=0;
            regs[key(node,0x211f,0)]=40;regs[key(node,0x6071,0)]=25;
        }
    }
    UsbId id() const override {return usb;}
    static unsigned width_of(unsigned index) {
        switch (index) {case 0x20a1:case 0x2102:case 0x2115:case 0x2116:case 0x2117:case 0x6064:case 0x2106:return 4;default:return 2;}
    }
    void write(const Report& r,int) override {
        sent->push_back(r);
        const unsigned node=r[4],index=r[7]|(r[8]<<8),sub=r[9];
        const bool is_write=r[6]==0x2b || r[6]==0x23;
        Report reply=r;
        reply[1]=0x11;reply[4]=static_cast<std::uint8_t>(0x80+node);reply[5]=0x85;
        const auto abort_with=[&](std::uint32_t code) {
            reply[6]=0x80;
            for (unsigned i=0;i<4;++i) reply[10+i]=static_cast<std::uint8_t>(code>>(i*8));
        };
        if (is_write) {
            writes->push_back(r);
            if (refuse_writes) abort_with(0x06010002u);
            else {
                std::uint32_t value=0;
                for (unsigned i=0;i<(r[6]==0x2b?2u:4u);++i) value|=static_cast<std::uint32_t>(r[10+i])<<(i*8);
                regs[key(node,index,sub)]=value;
                if (index==0x6040 && (value&0x87)==0x06) enabled=false;   // CiA-402 shutdown: bits 1,2 set, 0 and 7 clear
                reply[6]=0x60;for (unsigned i=0;i<4;++i) reply[10+i]=0;
            }
        } else {
            std::uint32_t value=0;bool known=true;
            switch (index) {
            case 0x3018:value=firmware[node];break;
            case 0x6041:value=tracks_state?(enabled?0x1427u:0x0421u):0x0010u;break;
            case 0x6064:value=static_cast<std::uint32_t>(encoder++);break;
            case 0x2106:value=0;break;
            default: {
                const auto found=regs.find(key(node,index,sub));
                if (found==regs.end()) known=false; else value=found->second;
            }
            }
            if (known) {
                reply[6]=width_of(index)==2?0x4b:0x43;
                for (unsigned i=0;i<4;++i) reply[10+i]=static_cast<std::uint8_t>(value>>(i*8));
            } else abort_with(0x06020000u);
        }
        replies.push_back(reply);
    }
    void read(Report& r,int timeout_ms) override {
        if (replies.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms,20)));
            throw TransportError("HID I/O timed out");
        }
        r=replies.front();replies.pop_front();
    }
    void flush() override {replies.clear();}
};

template<class Predicate> bool wait_for(Predicate done,int ms=3000) {
    const auto end=Clock::now()+std::chrono::milliseconds(ms);
    while (Clock::now()<end) {if (done()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    return done();
}
// A reader on a fake controller, with the logs the tests read after it has stopped.
struct Run {
    std::vector<Report> sent,writes;
    Reader reader;
    explicit Run(UsbId id={0x25bb,0x008b},std::function<void(ControllerFake&)> tweak={}) {
        reader.set_device_factory([this,id,tweak] {auto d=std::make_unique<ControllerFake>(&sent,&writes,id);if (tweak) tweak(*d);return d;});
    }
    bool ready() {const auto s=reader.snapshot();return !s.read_only && s.connected && s.cycles>2;}
};
std::uint32_t le(const Report& r,unsigned from,unsigned bytes) {
    std::uint32_t v=0;for (unsigned i=0;i<bytes;++i) v|=static_cast<std::uint32_t>(r[from+i])<<(i*8);return v;
}

// The rule is the model's, never the firmware's.
void the_gate_rule() {
    const auto cls_e=builtin_cls_e(),ng=builtin_cls_e_ng();
    check(!read_only_reason(cls_e,false) && !read_only_reason(cls_e,true));          // verified: always driven
    const auto why=read_only_reason(ng,false);
    check(why && why->find("Brunner CLS-E NG")!=std::string::npos && why->find("Allow unverified yoke models")!=std::string::npos && why->find(".ini")==std::string::npos);
    check(!read_only_reason(ng,true));                                               // acknowledged
    auto verified_ng=ng;verified_ng.verified=true;
    check(!read_only_reason(verified_ng,false));                                     // a preset that says verified needs no key
    // The firmware is only a note.
    check(firmware_note(cls_e,{{367,367}}).empty());
    check(firmware_note(cls_e,{{367,368}}).find("368")!=std::string::npos && firmware_note(cls_e,{{367,368}}).find("367")!=std::string::npos);
    check(firmware_note(ng,{{642,642}}).find("642")!=std::string::npos && firmware_note(ng,{{642,642}}).find("none")!=std::string::npos);
}

// An unverified model without the owner's acknowledgement: read-only, and nothing is ever written.
void unverified_model_is_read_only_and_never_written() {
    Run run;
    run.reader.start();
    check(wait_for([&] {const auto s=run.reader.snapshot();return s.read_only && s.connected && s.cycles>2;}));
    const auto s=run.reader.snapshot();
    check(s.axes[0].firmware==642 && s.axes[1].firmware==642);
    check(s.usb_vendor==0x25bb && s.usb_product==0x008b && s.model=="cls-e-ng" && s.model_title=="Brunner CLS-E NG");   // the model follows from the device
    check(s.message.find("not verified")!=std::string::npos && s.message.find("read-only")!=std::string::npos);
    check(s.yoke_note.find("642")!=std::string::npos);
    check(!s.loading_active && !s.reference_active && !s.session_reference && !s.loading_fault && !s.stop_unconfirmed);
    // Everything that could write is refused...
    check(!run.reader.request_reference_test() && !run.reader.request_loading_test() && !run.reader.request_flight());
    run.reader.request_loading_recovery();run.reader.request_loading_inspection();
    // ...and a survey is served, tolerating registers the controller lacks.
    check(run.reader.request_survey());
    check(wait_for([&] {return run.reader.snapshot().surveys==1;}));
    const auto survey=run.reader.snapshot().survey;
    check(survey.find("\"node1\"")!=std::string::npos && survey.find("\"node2\"")!=std::string::npos && survey.find("\"firmware\":642")!=std::string::npos);
    check(survey.find("\"0x2114.0\":180")!=std::string::npos && survey.find("\"0x2114.0\":160")!=std::string::npos);   // registers it has
    check(survey.find("\"0x2113.1\":\"unsupported 0x6020000\"")!=std::string::npos);                                    // one it lacks
    run.reader.stop();
    check(!run.sent.empty() && run.writes.empty());
    for (const auto& r:run.sent) check(r[0]==0x3f && r[6]==0x40);          // every report is an SDO upload: a read
    check(!run.reader.request_survey());
}
// Acknowledged: the NG is driven, but its first writes are one setting written back
// unchanged, node by node, before anything else is sent. The drive is the NG's own.
void an_acknowledged_model_starts_with_a_no_change_write() {
    ScopedDrive restore(cls_e_drive());
    Run run;
    run.reader.set_allow_unverified(true);
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    const auto s=run.reader.snapshot();
    check(s.model=="cls-e-ng" && !s.read_only && s.yoke_note.find("642")!=std::string::npos);
    // The active drive is the NG's, built from its live limit registers (2150 / 2250) and the preset.
    check(active_drive().model=="cls-e-ng" && active_drive().roll_limit_register==2150 && active_drive().pitch_limit_register==2250);
    check(active_drive().limit_units==2150 && active_drive().roll_travel_counts==7350 && active_drive().target_torque==25);
    run.reader.stop();
    check(run.writes.size()==2);
    for (unsigned i=0;i<2;++i) {
        const auto& w=run.writes[i];
        check(w[6]==0x2b && (w[7]|(w[8]<<8))==0x2114 && w[4]==i+1);                     // the spring strength, node 1 then node 2
        check(le(w,10,2)==(i==0?180u:160u));                                           // the value it already had
    }
    // Before the first write nothing but uploads were sent.
    std::size_t first_write=0;
    while (first_write<run.sent.size() && run.sent[first_write][6]==0x40) ++first_write;
    check(first_write<run.sent.size() && run.sent[first_write]==run.writes[0]);
}
// A write the controller refuses ends the session at once; nothing else is written.
void a_refused_test_write_ends_the_session() {
    ScopedDrive restore(cls_e_drive());
    Run run({0x25bb,0x008b},[](ControllerFake& f) {f.refuse_writes=true;});
    run.reader.set_allow_unverified(true);
    run.reader.start();
    check(wait_for([&] {const auto s=run.reader.snapshot();return s.loading_fault && s.message.find("test write")!=std::string::npos;}));
    run.reader.stop();
    check(!run.writes.empty());
    for (const auto& w:run.writes) check((w[7]|(w[8]<<8))==0x2114);                     // only the test write was attempted
}
// A verified model at a firmware its preset was not verified on: driven, with a note only.
void a_verified_model_on_other_firmware_is_noted_not_gated() {
    ScopedDrive restore(cls_e_drive());
    Run run({0x25bb,0x0081},[](ControllerFake& f) {f.firmware[2]=368;});
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    const auto s=run.reader.snapshot();
    check(s.model=="cls-e" && !s.read_only && s.yoke_note.find("368")!=std::string::npos && s.yoke_note.find("367")!=std::string::npos);
    check(active_drive().limit_units==4500 && active_drive().target_torque==50);
    run.reader.stop();
    check(run.writes.empty());   // verified: no test write, and nothing to stop or recover
}
// A preset edited to say verified needs no acknowledgement; the ids come from the presets.
void presets_decide_the_ids_and_the_gate() {
    ScopedDrive restore(cls_e_drive());
    auto presets=builtin_yoke_presets();
    presets[1].verified=true;
    Run run;
    run.reader.set_presets(presets);
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    check(run.reader.snapshot().model=="cls-e-ng");
    run.reader.stop();
    check(run.writes.empty());
    // A device whose id no preset names is refused before anything is read but its firmware.
    Run stranger({0x1234,0x5678});
    stranger.reader.start();
    check(wait_for([&] {const auto s=stranger.reader.snapshot();return s.loading_fault && s.message.find("No yoke preset")!=std::string::npos;}));
    stranger.reader.stop();
    check(stranger.writes.empty());
    for (const auto& r:stranger.sent) check(r[6]==0x40);
}
// One unverified model, firmware 367 on one axis and 642 on the other: still the model's rule.
void mixed_firmware_is_the_models_rule() {
    Run run({0x25bb,0x008b},[](ControllerFake& f) {f.firmware[1]=367;});
    run.reader.start();
    check(wait_for([&] {const auto s=run.reader.snapshot();return s.read_only && s.cycles>1;}));
    check(run.reader.snapshot().axes[0].firmware==367 && run.reader.snapshot().axes[1].firmware==642);
    run.reader.stop();
    check(run.writes.empty());
}

template<class Fn> bool throws_invalid(Fn f) {try {f();} catch (const std::invalid_argument&) {return true;} catch (...) {} return false;}

void travel_learning() {
    TravelLearner t;
    check(!t.ready() && t.percent()==0 && t.span()==0);
    check(t.observe(0) && !t.ready());
    check(!t.observe(0));                                   // nothing new
    check(t.observe(2000) && t.percent()==50 && !t.ready());
    check(throws_invalid([&] {(void)t.ratio(0,false);}));   // not learned yet: no guessing
    check(t.observe(-2000) && t.ready() && t.percent()==100);
    check(near(t.ratio(0,false),0.0f) && near(t.ratio(2000,false),1.0f) && near(t.ratio(-2000,false),-1.0f) && near(t.ratio(1000,false),0.5f));
    check(near(t.ratio(9000,false),1.0f) && near(t.ratio(-9000,false),-1.0f)); // beyond what was seen: clamped
    check(near(t.ratio(2000,true),-1.0f) && near(t.ratio(-1000,true),0.5f));    // reversed
    // The zero is the middle of the travel, wherever the encoder's own zero is.
    TravelLearner off;off.observe(1000);off.observe(9000);
    check(near(off.ratio(5000,false),0.0f) && near(off.ratio(9000,false),1.0f) && near(off.ratio(1000,false),-1.0f));
    // The threshold is exact.
    TravelLearner edge;edge.observe(0);edge.observe(static_cast<std::int32_t>(read_only_min_span)-1);check(!edge.ready());
    edge.observe(static_cast<std::int32_t>(read_only_min_span));check(edge.ready());
    TravelLearner wide;wide.observe(INT32_MIN);wide.observe(INT32_MAX);check(wide.ready() && wide.percent()==100); // no overflow
}

Snapshot read_only_snapshot(Clock::time_point now,std::int32_t roll,std::int32_t pitch) {
    Snapshot s;s.connected=true;s.read_only=true;s.cycles=1;s.sampled=now;
    s.axes[0].firmware=642;s.axes[1].firmware=642;s.axes[0].encoder=roll;s.axes[1].encoder=pitch;
    s.axes[0].status=s.axes[1].status=0x1427; // what the NG reports at rest, referenced
    return s;
}
void input_from_the_learned_travel() {
    const auto now=Clock::now();
    const std::array<bool,2> forward{{false,false}},pitch_reversed{{false,true}};
    Travel travel;
    auto step=[&](std::int32_t roll,std::int32_t pitch,const std::array<bool,2>& reverse=std::array<bool,2>{{false,false}}) {
        return read_only_flight_input(read_only_snapshot(now,roll,pitch),travel,reverse,now);
    };
    auto first=step(0,0,forward);
    check(!first.input.ready && first.widened);
    check(first.input.reason.find("move the yoke to all four stops")!=std::string::npos && first.input.reason.find("roll 0%")!=std::string::npos);
    step(-3000,0);step(3000,1000);
    auto partial=step(3000,1000);
    check(!partial.input.ready && !partial.widened);
    check(partial.input.reason.find("roll 100%")!=std::string::npos && partial.input.reason.find("pitch 25%")!=std::string::npos);
    step(3000,-2500);step(3000,2500);                       // pitch span 5000: both learned
    auto ready=step(1500,-2500);
    check(ready.input.ready && near(ready.input.roll,0.5f) && near(ready.input.pitch,-1.0f));
    check(ready.input.reason.find("Read-only input")==0 && ready.input.reason.find("no force feel")!=std::string::npos && ready.input.reason.find("irmware")==std::string::npos);
    auto flipped=step(1500,-2500,pitch_reversed);
    check(near(flipped.input.roll,0.5f) && near(flipped.input.pitch,1.0f));
    // Stale data, a disconnect, or a yoke that is not read-only never give input, and are not learned from.
    check(!read_only_flight_input(read_only_snapshot(now,0,0),travel,forward,now+std::chrono::milliseconds(300)).input.ready);
    Travel fresh;
    auto gone=read_only_snapshot(now,0,0);gone.connected=false;
    check(read_only_flight_input(gone,fresh,forward,now).input.reason=="No complete yoke sample" && !fresh[0].seen);
    auto driven=read_only_snapshot(now,0,0);driven.read_only=false;
    check(read_only_flight_input(driven,fresh,forward,now).input.reason=="No complete yoke sample" && !fresh[0].seen);
}
void the_remembered_travel() {
    Travel t;
    t[0].low=-9000;t[0].high=9100;t[0].seen=true;t[1].low=-7000;t[1].high=7200;t[1].seen=true;
    const UsbId ng{0x25bb,0x008b};
    const auto text=serialize_travel(ng,t);
    const auto back=parse_travel(text,ng);
    check(back && (*back)[0].low==-9000 && (*back)[0].high==9100 && (*back)[1].low==-7000 && (*back)[1].high==7200 && (*back)[0].seen && (*back)[1].seen);
    // The range belongs to a model: another USB id ignores it. A firmware update changes nothing here,
    // because firmware is not in the file at all.
    check(!parse_travel(text,{0x25bb,0x0081}) && !parse_travel(text,{0x25bb,0x00ff}) && !parse_travel(text,{0x1234,0x008b}));
    check(text.find("firmware")==std::string::npos);
    // The version: only 3 is read. The stretched file written during the NG's homing sweep (no
    // version), the firmware-keyed version 2 and any other version are ignored.
    const std::string body="\"usb\":[9659,139],\"roll\":{\"low\":-7637,\"high\":7985},\"pitch\":{\"low\":-5458,\"high\":5712}}";
    check(parse_travel("{\"version\":3,"+body,ng).has_value());
    for (const auto* version:{"","\"version\":1,","\"version\":2,","\"version\":4,","\"version\":\"3\",","\"version\":3.5,","\"version\":null,"})
        check(!parse_travel(std::string("{")+version+body,ng));
    const std::string old_file="{\"version\":2,\"firmware\":[642,642],\"roll\":{\"low\":-7637,\"high\":7985},\"pitch\":{\"low\":-5458,\"high\":5712}}";
    check(!parse_travel(old_file,ng));
    const std::string head="{\"version\":3,\"usb\":[9659,139],";
    for (const auto& garbage:{std::string(),std::string("x"),std::string("{}"),std::string("[]"),head+"\"roll\":{\"low\":0,\"high\":1}}",
            head+"\"roll\":{\"low\":5,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",                      // inverted
            head+"\"roll\":{\"low\":\"a\",\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",                   // wrong type
            head+"\"roll\":{\"low\":-9999999999,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",             // absurd
            std::string("{\"version\":3,\"usb\":[\"9659\",\"139\"],\"roll\":{\"low\":0,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}"),
            std::string("{\"version\":3,\"usb\":[9659],\"roll\":{\"low\":0,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}")})
        check(!parse_travel(garbage,ng));
}
// The NG homes itself at power-on, sweeping past its own limits with bit 0x1000 absent.
// That sweep is the machine's, not the pilot's: never learned, never forwarded.
void homing_is_neither_learned_nor_forwarded() {
    const auto now=Clock::now();
    const std::array<bool,2> forward{{false,false}};
    const auto with_status=[&](std::int32_t roll,std::int32_t pitch,std::uint16_t roll_status,std::uint16_t pitch_status) {
        auto s=read_only_snapshot(now,roll,pitch);s.axes[0].status=roll_status;s.axes[1].status=pitch_status;return s;
    };
    // Axis gate.
    check(axis_usable(0x1427) && axis_usable(0x1c27));                 // at rest; at a limit (0x0800 allowed)
    check(!axis_usable(0x0027) && !axis_usable(0x0021) && !axis_usable(0x0000));           // homing, switching on, off
    check(!axis_usable(0x142f) && !axis_usable(0x1527));               // fault 0x0008, homing error 0x0100
    // A homing sample on a fresh yoke teaches nothing.
    Travel travel;
    auto homing=read_only_flight_input(with_status(7985,5712,0x0027,0x0027),travel,forward,now);
    check(!homing.input.ready && !homing.widened && !travel[0].seen && !travel[1].seen);
    check(homing.input.reason.find("homing")!=std::string::npos);
    // The capture: the hand sweeps to the controller's limits (flag 0x0800)...
    for (const auto& p:{std::pair<int,int>{-7637,-5458},{7647,5478},{0,0}})
        read_only_flight_input(with_status(p.first,p.second,0x1c27,0x1c27),travel,forward,now);
    check(travel[0].low==-7637 && travel[0].high==7647 && travel[1].low==-5458 && travel[1].high==5478);
    // ...then the self-test sweeps past them, unreferenced: the range does not move and nothing is forwarded.
    for (const auto& p:{std::pair<int,int>{414,80},{7985,5712},{-7412,-5320},{-99,-87}}) {
        auto r=read_only_flight_input(with_status(p.first,p.second,0x0027,0x0027),travel,forward,now);
        check(!r.input.ready && !r.widened);
    }
    check(travel[0].low==-7637 && travel[0].high==7647 && travel[1].low==-5458 && travel[1].high==5478);
    // Referenced again: the limit reads exactly +-1 and the middle is the middle.
    auto top=read_only_flight_input(with_status(7647,5478,0x1c27,0x1c27),travel,forward,now);
    auto bottom=read_only_flight_input(with_status(-7637,-5458,0x1c27,0x1c27),travel,forward,now);
    auto rest=read_only_flight_input(with_status(5,10,0x1427,0x1427),travel,forward,now);
    check(top.input.ready && near(top.input.roll,1.0f) && near(top.input.pitch,1.0f));
    check(bottom.input.ready && near(bottom.input.roll,-1.0f) && near(bottom.input.pitch,-1.0f));
    check(rest.input.ready && std::fabs(rest.input.roll)<0.001f && std::fabs(rest.input.pitch)<0.001f);
    // Even with the range already learned, a homing sample is not forwarded.
    check(!read_only_flight_input(with_status(0,0,0x0027,0x0027),travel,forward,now).input.ready);
    // One axis unreferenced is enough to hold both back, and a limit flag alone is not a fault.
    check(!read_only_flight_input(with_status(0,0,0x1427,0x0027),travel,forward,now).input.ready);
    check(read_only_flight_input(with_status(0,0,0x1c27,0x1427),travel,forward,now).input.ready);
    // Fault and homing-error bits.
    Travel other;
    for (const std::uint16_t bad:{0x142f,0x1527}) {
        auto r=read_only_flight_input(with_status(100,100,bad,0x1427),other,forward,now);
        check(!r.input.ready && !r.widened && !other[0].seen && r.input.reason=="Controller fault");
    }
}
void the_supervisor_asks_for_nothing() {
    YokeSupervisor yoke;
    YokeInputs in;
    in.now=Clock::now();in.sim_steady=true;in.reader_running=true;
    in.snapshot=read_only_snapshot(in.now,0,0);
    in.snapshot.message="Yoke firmware 642 not supported: read-only, no force feel";
    const auto quiet=[&] {
        for (int i=0;i<6;++i) {
            in.now+=std::chrono::seconds(1);in.snapshot.sampled=in.now;
            const auto d=yoke.step(in);
            check(d.action==YokeAction::none && d.status==in.snapshot.message && !d.retry_offered && d.show_notice);
        }
    };
    quiet();
    yoke.retry();yoke.force_feel_switched_on();in.force_feel=true;in.input_ready=true;quiet();   // nothing the pilot asks makes it act
    yoke.stop();quiet();
}
// The owner's saved choice: only an exact, literal "true" in a file of this version turns it on.
void the_saved_owner_state() {
    check(!OwnerState{}.allow_unverified_yoke);
    check(parse_owner_state(serialize_owner_state({true})).allow_unverified_yoke);
    check(!parse_owner_state(serialize_owner_state({false})).allow_unverified_yoke);
    check(serialize_owner_state({true})==std::string("{\"version\":1,\"allow_unverified_yoke\":true}\n"));
    for (const auto* doubtful:{"","not json","{}","[]","null","true","{\"version\":1}","{\"allow_unverified_yoke\":true}",
            "{\"version\":2,\"allow_unverified_yoke\":true}","{\"version\":\"1\",\"allow_unverified_yoke\":true}","{\"version\":1.5,\"allow_unverified_yoke\":true}",
            "{\"version\":1,\"allow_unverified_yoke\":\"true\"}","{\"version\":1,\"allow_unverified_yoke\":1}","{\"version\":1,\"allow_unverified_yoke\":null}",
            "{\"version\":1,\"allow_unverified_yoke\":\"yes\"}","{\"version\":1,\"allow_unverified_yoke\":true,\"other\":1}",
            "{\"version\":1,\"allow_unverified_yoke\":true} trailing","{\"version\":1,\"allow_unverified_yoke\":tru"})
        check(!parse_owner_state(doubtful).allow_unverified_yoke);     // any doubt is off
}
// What the menu toggle does to the reader: stop it, change the choice, start it again.
void the_toggle_restarts_the_reader_with_the_new_choice() {
    ScopedDrive restore(cls_e_drive());
    Run run;
    run.reader.start();
    check(wait_for([&] {const auto s=run.reader.snapshot();return s.read_only && s.connected && s.cycles>2;}));   // off: read-only
    check(run.writes.empty());
    // ON.
    run.reader.stop();run.reader.set_allow_unverified(true);run.reader.start();
    check(wait_for([&] {return run.ready();}));
    check(run.writes.size()==2 && (run.writes[0][7]|(run.writes[0][8]<<8))==0x2114);   // the no-change test write, node 1 then node 2
    // OFF again: read-only, and no further writes.
    const auto written=run.writes.size();
    run.reader.stop();run.reader.set_allow_unverified(false);run.reader.start();
    check(wait_for([&] {const auto s=run.reader.snapshot();return s.read_only && s.connected && s.cycles>2;}));
    run.reader.stop();
    check(run.writes.size()==written);
}
// The NG idles enabled. Session start shuts it down the standard way, and its resting mode
// (0x0605, not the CLS-E's 0x0205) is accepted; before this it was "Unknown controller spring mode".
void an_idle_enabled_model_is_shut_down_and_its_own_mode_accepted() {
    ScopedDrive restore(cls_e_drive());
    ControllerFake* device=nullptr;
    Run run({0x25bb,0x008b},[&](ControllerFake& f) {device=&f;f.tracks_state=true;f.enabled=true;f.surveyed_registers();});
    run.reader.set_allow_unverified(true);
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    const auto s=run.reader.snapshot();
    check(!s.loading_fault && s.loading_result.find("failed")==std::string::npos);
    check(s.loading_result.find("disabled")!=std::string::npos);
    bool shutdown=false;
    for (const auto& w:run.writes) if ((w[7]|(w[8]<<8))==0x6040 && (le(w,10,2)&0x87)==0x06) shutdown=true;
    check(shutdown && !device->enabled);
    // The NG comes back on by itself; centring shuts it down again instead of refusing.
    auto shutdowns=[&] {std::size_t n=0;for (const auto& w:run.writes) if ((w[7]|(w[8]<<8))==0x6040 && (le(w,10,2)&0x87)==0x06) ++n;return n;};
    const auto before=shutdowns();
    device->enabled=true;
    check(wait_for([&] {return run.reader.snapshot().cycles>0;}));
    check(run.reader.request_reference_test());
    check(wait_for([&] {return run.reader.snapshot().reference_active || shutdowns()>before;}));
    check(wait_for([&] {return shutdowns()>before;}));
    check(run.reader.snapshot().loading_result.find("requires disabled")==std::string::npos);
    run.reader.stop();
}
// A model that does not idle enabled is still refused when found enabled at a centring.
void a_model_that_idles_off_is_still_refused_when_enabled() {
    ScopedDrive restore(cls_e_drive());
    ControllerFake* device=nullptr;
    Run run({0x25bb,0x0081},[&](ControllerFake& f) {device=&f;f.tracks_state=true;f.surveyed_registers();});
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    std::size_t writes_before=run.writes.size();
    device->enabled=true;
    check(run.reader.request_reference_test());
    check(wait_for([&] {return run.reader.snapshot().loading_result.find("requires disabled")!=std::string::npos;}));
    run.reader.stop();
    check(run.writes.size()==writes_before);                // refused untouched: nothing written
    // The preset's own flag, not the model's name, decides.
    check(!builtin_cls_e().idle_enabled && builtin_cls_e_ng().idle_enabled);
    check(builtin_cls_e().spring_mode==0x0205 && builtin_cls_e_ng().spring_mode==0x0605 && builtin_cls_e_ng().status_stopped==0x0421);
    auto bad=builtin_cls_e_ng();bad.spring_mode=0x2605;bool refused=false;
    try {validate_preset(bad);} catch (const PresetError&) {refused=true;}
    check(refused);
}
// A pause holds a force-feel session instead of ending it, so resume has nothing to rebuild.
void a_pause_is_not_a_lapse() {
    using ms=std::chrono::milliseconds;
    const auto t0=Clock::now();
    SimulatorWatch w;
    check(w.failure(t0)=="Simulator controls are not ready");                      // nothing known yet
    w.update(true,false,t0);check(w.failure(t0).empty());
    // Paused for a long time, the plugin still calling each frame: no lapse, however long.
    for (int i=1;i<=100;++i) {w.update(false,true,t0+ms(i*100));check(w.failure(t0+ms(i*100)).empty() && w.paused());}
    // Resumed, the first frames without a verdict yet: believed for the grace only.
    const auto resume=t0+ms(10000);
    w.update(false,false,resume);check(!w.paused() && w.failure(resume+ms(100)).empty());
    check(w.failure(resume+SimulatorWatch::resume_grace-ms(1)).empty());   // still inside the grace...
    w.update(false,false,resume+ms(450));
    check(w.failure(resume+SimulatorWatch::resume_grace+ms(100))=="Simulator controls are not ready");   // ...a real loss outlasts it
    // A verdict of running before the grace ends clears it for good.
    SimulatorWatch r;r.update(true,false,t0);r.update(false,true,t0+ms(50));r.update(true,false,t0+ms(100));
    r.update(true,false,t0+ms(1500));check(r.failure(t0+ms(1600)).empty());
    // Paused from the start: the watch reports no lapse while the pauses keep coming, because
    // there is no session to hold (the supervisor never starts one while paused).
    SimulatorWatch cold;cold.update(false,true,t0);cold.update(false,true,t0+ms(600));check(cold.failure(t0+ms(600)).empty());
    // A silent plugin is a lapse whether paused or not: the heartbeat.
    SimulatorWatch quiet;quiet.update(true,false,t0);quiet.update(false,true,t0+ms(10));
    check(quiet.failure(t0+ms(10)+SimulatorWatch::heartbeat_limit+ms(1)).find("heartbeat expired")!=std::string::npos);
}
// A Brunner bridge with no preset is read-only, and the owner's toggle does not make it drivable:
// there is nothing (travel, ceilings, status words) to drive it by.
void an_unknown_brunner_id_is_read_only_and_never_written() {
    ScopedDrive restore(cls_e_drive());
    for (const bool allow:{false,true}) {
        Run run({0x25bb,0x00ff});
        run.reader.set_allow_unverified(allow);
        run.reader.start();
        check(wait_for([&] {const auto s=run.reader.snapshot();return s.read_only && s.connected && s.cycles>2;}));
        const auto s=run.reader.snapshot();
        check(s.model=="unknown" && s.model_title=="Unknown Brunner yoke");
        check(s.message.find("Unknown yoke USB id 25BB:00FF")!=std::string::npos && s.message.find("BrunnerDirect.yokes.json")!=std::string::npos);
        check(s.usb_vendor==0x25bb && s.usb_product==0x00ff && s.yoke_note.empty());
        check(!run.reader.request_reference_test() && !run.reader.request_loading_test() && !run.reader.request_flight());
        run.reader.stop();
        check(!run.sent.empty() && run.writes.empty());          // read requests only, whatever the toggle
    }
    // A preset for the id (an edited BrunnerDirect.yokes.json) makes it a known model again.
    auto club=builtin_cls_e_ng();club.name="club";club.usb_ids={{0x25bb,0x00ff}};
    Run known({0x25bb,0x00ff});
    known.reader.set_presets({builtin_cls_e(),club});
    known.reader.start();
    check(wait_for([&] {const auto s=known.reader.snapshot();return s.read_only && s.model=="club" && s.cycles>2;}));   // known, unverified: the gate, not "unknown"
    known.reader.stop();
    check(known.writes.empty());
}
// A firmware update never changes the mode: a known verified model is driven at any firmware, and the
// learned range of a read-only yoke stays with its model.
void firmware_never_changes_the_mode() {
    ScopedDrive restore(cls_e_drive());
    Run run({0x25bb,0x0081},[](ControllerFake& f) {f.firmware[1]=f.firmware[2]=999;});
    run.reader.start();
    check(wait_for([&] {return run.ready();}));
    const auto s=run.reader.snapshot();
    check(!s.read_only && s.model=="cls-e" && s.yoke_note.find("999")!=std::string::npos);   // driven; the note says so
    run.reader.stop();
}
void the_new_config_keys() {
    const std::string base="schema_version=2\nroll_spring_percent=35\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\nreference_airspeed_knots=316\nminimum_spring_percent=20\nmaximum_trim_counts=6000\n";
    {std::istringstream in(base);const auto c=parse_configuration(in);check(c.flight.read_only_roll_reverse==0 && c.flight.read_only_pitch_reverse==0);}
    {std::istringstream in(base+"read_only_roll_reverse=1\nread_only_pitch_reverse=1\n");const auto c=parse_configuration(in);
        check(c.flight.read_only_roll_reverse==1 && c.flight.read_only_pitch_reverse==1);}
    // allow_unverified_yoke is no longer an ini setting (the Developer menu's saved choice is the only
    // source): a file that still has it loads, the line is ignored at any value, and it is reported.
    for (const char* value:{"1","0","2","-1"}) {
        std::istringstream in(base+"allow_unverified_yoke="+value+"\n");
        const auto c=parse_configuration(in);
        check(std::find(c.retired.begin(),c.retired.end(),"allow_unverified_yoke")!=c.retired.end());
    }
    {std::istringstream in(base);const auto c=parse_configuration(in);check(std::find(c.retired.begin(),c.retired.end(),"allow_unverified_yoke")==c.retired.end());}
    for (const auto* bad:{"read_only_roll_reverse=2\n","read_only_pitch_reverse=-1\n"}) {
        bool refused=false;
        try {std::istringstream in(base+bad);parse_configuration(in);} catch (const std::exception&) {refused=true;}
        check(refused);
    }
}
}
int main() {
    try {
        the_gate_rule();
        the_saved_owner_state();
        a_pause_is_not_a_lapse();
        an_unknown_brunner_id_is_read_only_and_never_written();
        firmware_never_changes_the_mode();
        the_toggle_restarts_the_reader_with_the_new_choice();
        unverified_model_is_read_only_and_never_written();
        an_acknowledged_model_starts_with_a_no_change_write();
        a_refused_test_write_ends_the_session();
        an_idle_enabled_model_is_shut_down_and_its_own_mode_accepted();
        a_model_that_idles_off_is_still_refused_when_enabled();
        a_verified_model_on_other_firmware_is_noted_not_gated();
        presets_decide_the_ids_and_the_gate();
        mixed_firmware_is_the_models_rule();
        travel_learning();
        input_from_the_learned_travel();
        the_remembered_travel();
        homing_is_neither_learned_nor_forwarded();
        the_supervisor_asks_for_nothing();
        the_new_config_keys();
        std::cout<<"read-only tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
