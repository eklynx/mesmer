// Tests for read-only mode (readonly.hpp, spec docs/read-only-mode.md): firmware
// the plugin does not drive. The point of most of them is the guarantee: on such
// firmware nothing but read requests is ever sent to a controller. No hardware.
#include "bridge.hpp"
#include "config.hpp"
#include "readonly.hpp"
#include "reader.hpp"
#include "yoke.hpp"
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
std::unique_ptr<HidDevice> brunner::open_bridge() {throw BridgeAbsent{};}
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

// A controller pair on firmware 642. Answers SDO uploads from a table; any
// register not in it aborts, as one a controller lacks would. Records every
// report written.
struct ControllerFake : HidDevice {
    std::vector<Report>* sent;
    std::deque<Report> replies;
    int encoder=100;
    explicit ControllerFake(std::vector<Report>* log):sent(log) {}
    static unsigned width_of(unsigned index) {
        switch (index) {case 0x20a1:case 0x2102:case 0x2115:case 0x2116:case 0x2117:case 0x6064:case 0x2106:return 4;default:return 2;}
    }
    void write(const Report& r,int) override {
        sent->push_back(r);
        const unsigned node=r[4],index=r[7]|(r[8]<<8),sub=r[9];
        Report reply=r;
        reply[1]=0x11;reply[4]=static_cast<std::uint8_t>(0x80+node);reply[5]=0x85;
        std::uint32_t value=0;bool known=true;
        switch (index) {
        case 0x3018:value=642;break;
        case 0x6041:value=0x0010;break;
        case 0x6064:value=static_cast<std::uint32_t>(encoder++);break;
        case 0x2106:value=0;break;
        case 0x2114:value=100;break;
        default:known=false;
        }
        if (known) {
            reply[6]=width_of(index)==2?0x4b:0x43;
            for (unsigned i=0;i<4;++i) reply[10+i]=static_cast<std::uint8_t>(value>>(i*8));
        } else {
            reply[6]=0x80;
            const std::uint32_t abort_code=0x06020000u;
            for (unsigned i=0;i<4;++i) reply[10+i]=static_cast<std::uint8_t>(abort_code>>(i*8));
        }
        (void)sub;
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

void firmware_classification() {
    check(!needs_read_only({367,367}));
    check(needs_read_only({642,642}) && needs_read_only({367,642}) && needs_read_only({642,367}) && needs_read_only({366,366}) && needs_read_only({0,0}));
}

// The whole reader on firmware 642: it reads, it publishes read_only, it refuses
// every request that could write, it serves a survey, and it never writes.
void reader_never_writes_on_unsupported_firmware() {
    std::vector<Report> sent;
    Reader reader;
    reader.set_device_factory([&] {return std::make_unique<ControllerFake>(&sent);});
    reader.start();
    check(wait_for([&] {const auto s=reader.snapshot();return s.read_only && s.connected && s.cycles>2;}));
    const auto s=reader.snapshot();
    check(s.axes[0].firmware==642 && s.axes[1].firmware==642);
    check(s.message.find("642")!=std::string::npos && s.message.find("read-only")!=std::string::npos);
    check(!s.loading_active && !s.reference_active && !s.session_reference && !s.loading_fault && !s.stop_unconfirmed);
    // Everything that could write is refused...
    check(!reader.request_reference_test());
    check(!reader.request_loading_test());
    check(!reader.request_flight());
    reader.request_loading_recovery();
    reader.request_loading_inspection();
    // ...and a survey is served, tolerating registers the controller lacks.
    check(reader.request_survey());
    check(wait_for([&] {return reader.snapshot().surveys==1;}));
    const auto survey=reader.snapshot().survey;
    check(survey.find("\"node1\"")!=std::string::npos && survey.find("\"node2\"")!=std::string::npos);
    check(survey.find("\"firmware\":642")!=std::string::npos);
    check(survey.find("\"0x2114.0\":100")!=std::string::npos);   // a register it has
    check(survey.find("\"0x2113.1\":\"unsupported 0x6020000\"")!=std::string::npos); // one it lacks
    reader.stop();
    check(!sent.empty());
    for (const auto& r:sent) check(r[0]==0x3f && r[6]==0x40); // every report is an SDO upload: a read
    check(!reader.request_survey()); // not read-only any more once stopped
}
// Unsupported firmware on one axis is enough.
void mixed_firmware_is_read_only() {
    struct Mixed:ControllerFake {
        using ControllerFake::ControllerFake;
        void write(const Report& r,int t) override {
            ControllerFake::write(r,t);
            if (r[4]==1 && (r[7]|(r[8]<<8))==0x3018) {replies.back()[10]=0x6f;replies.back()[11]=0x01;} // node 1 reports 367
        }
    };
    std::vector<Report> sent;
    Reader reader;
    reader.set_device_factory([&] {return std::make_unique<Mixed>(&sent);});
    reader.start();
    check(wait_for([&] {const auto s=reader.snapshot();return s.read_only && s.cycles>1;}));
    check(reader.snapshot().axes[0].firmware==367 && reader.snapshot().axes[1].firmware==642);
    reader.stop();
    for (const auto& r:sent) check(r[6]==0x40);
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
    check(ready.input.reason.find("642")!=std::string::npos && ready.input.reason.find("no force feel")!=std::string::npos);
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
    const std::array<std::uint16_t,2> firmware{{642,642}};
    const auto text=serialize_travel(firmware,t);
    const auto back=parse_travel(text,firmware);
    check(back && (*back)[0].low==-9000 && (*back)[0].high==9100 && (*back)[1].low==-7000 && (*back)[1].high==7200 && (*back)[0].seen && (*back)[1].seen);
    check(!parse_travel(text,{{642,367}}) && !parse_travel(text,{{367,367}}));   // another yoke's firmware: ignored
    // The version: only 2 is read. The stretched file written during the NG's homing sweep
    // (no version) and any other version are ignored.
    const std::string body="\"firmware\":[642,642],\"roll\":{\"low\":-7637,\"high\":7985},\"pitch\":{\"low\":-5458,\"high\":5712}}";
    check(parse_travel("{\"version\":2,"+body,firmware).has_value());
    for (const auto* version:{"","\"version\":1,","\"version\":3,","\"version\":\"2\",","\"version\":2.5,","\"version\":null,"})
        check(!parse_travel(std::string("{")+version+body,firmware));
    const std::string head="{\"version\":2,\"firmware\":[642,642],";
    for (const auto& garbage:{std::string(),std::string("x"),std::string("{}"),std::string("[]"),head+"\"roll\":{\"low\":0,\"high\":1}}",
            head+"\"roll\":{\"low\":5,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",                      // inverted
            head+"\"roll\":{\"low\":\"a\",\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",                   // wrong type
            head+"\"roll\":{\"low\":-9999999999,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}",             // absurd
            std::string("{\"version\":2,\"firmware\":[\"642\",\"642\"],\"roll\":{\"low\":0,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}"),
            std::string("{\"version\":2,\"firmware\":[642],\"roll\":{\"low\":0,\"high\":1},\"pitch\":{\"low\":0,\"high\":1}}")})
        check(!parse_travel(garbage,firmware));
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
void the_new_config_keys() {
    const std::string base="schema_version=2\nroll_spring_percent=35\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\nreference_airspeed_knots=316\nminimum_spring_percent=20\nmaximum_trim_counts=6000\n";
    {std::istringstream in(base);const auto c=parse_configuration(in);check(c.flight.read_only_roll_reverse==0 && c.flight.read_only_pitch_reverse==0);}
    {std::istringstream in(base+"read_only_roll_reverse=1\nread_only_pitch_reverse=1\n");const auto c=parse_configuration(in);
        check(c.flight.read_only_roll_reverse==1 && c.flight.read_only_pitch_reverse==1);}
    for (const auto* bad:{"read_only_roll_reverse=2\n","read_only_pitch_reverse=-1\n"}) {
        bool refused=false;
        try {std::istringstream in(base+bad);parse_configuration(in);} catch (const std::exception&) {refused=true;}
        check(refused);
    }
}
}
int main() {
    try {
        firmware_classification();
        reader_never_writes_on_unsupported_firmware();
        mixed_firmware_is_read_only();
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
