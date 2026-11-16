// Tests for the virtual joystick's logic (buttons/actions/vjoy_presser.hpp, vjoy_action.hpp) and for the read of the
// yoke's buttons (buttons/input/can_source.hpp), on every platform: plain C++, a fake joystick, a fake link and a fake sink.
#include "buttons/actions/vjoy_action.hpp"
#include "buttons/input/can_source.hpp"
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace buttons;
using brunner::Object;
using brunner::Snapshot;
using brunner::OperationCancelled;
using brunner::TransportError;
void check_at(bool okay,int line) { if (!okay) throw std::runtime_error("check failed line "+std::to_string(line)); }
#define check(...) check_at((__VA_ARGS__),__LINE__)
// vJoy's feeder as the plugin sees it, recording each call.
struct FakeJoystick : VirtualJoystick {
    std::string missing;bool on=true,takes=true,obeys=true;Status state=Status::available;int count=32;
    std::vector<std::string> calls;
    std::string load() override {calls.push_back("load");return missing;}
    bool enabled() override {return on;}
    Status status() override {return state;}
    bool acquire() override {calls.push_back("acquire");if (takes) state=Status::owned;return takes;}
    void relinquish() override {calls.push_back("relinquish");state=Status::available;}
    int buttons() override {return count;}
    bool set(unsigned button,bool down) override {calls.push_back((down?"down ":"up ")+std::to_string(button));return obeys;}
    bool reset() override {calls.push_back("reset");return obeys;}
    std::vector<std::string> take() {auto c=calls;calls.clear();return c;}
};
using Calls=std::vector<std::string>;
// Node 2's button word as the reader's bridge answers it, or fails to.
struct RecordingSink : ButtonSink {
    std::vector<ButtonState> states;int releases=0;
    void publish(const ButtonState& state,Clock::time_point) override {states.push_back(state);}
    void release_all() override {++releases;}
};
// Objects as the reader's bridge answers them (key: node << 24 | index << 8 | sub), or fails to.
struct ButtonLink {
    static std::uint32_t key(unsigned node,unsigned index,unsigned sub) {return (node<<24)|(index<<8)|sub;}
    std::map<std::uint32_t,std::uint32_t> words;std::vector<std::uint32_t> reads;std::function<void(std::uint32_t)> fail;CanSource* can=nullptr;
    CanSource* can_source() const {return can;}
    std::uint32_t ref_read(unsigned node,brunner::Register reg,const std::atomic<bool>& stop,int reply_ms) {
        check((node==1 || node==2) && reply_ms==brunner::sample_reply_timeout_ms);   // a sampling read: one attempt, one sample period
        if (stop) throw OperationCancelled{};
        const auto k=key(node,reg.index,reg.sub);
        reads.push_back(k);
        if (fail) fail(k);
        return words[k];
    }
};
int main() {
    try {
        check(pressed_slots(0x80018001u)=="1, 16, 17, 32" && pressed_slots(0x2800u)=="12, 14" && pressed_slots(0)=="none");
        // Reading them over the bridge, the original CLS-E's layout: node 2's 0x60fd, a mask per slot (bits 16 to 31), in every
        // cycle when enabled and attached, published. A refusal or a garble leaves the buttons unread and releases
        // everything at the sink, retried from 5 s on; the link failing, a missed reply and the reader stopping pass through and record
        // nothing. Disabled, nothing is read at all. A slot the yoke has no entry for (6, 15, 16) is never pressed.
        {
            const auto t0=Clock::now();
            const auto digital=ButtonLink::key(2,0x60fd,0);
            ButtonLink link;link.words[digital]=0x8005ffffu;                                       // slots 1 and 3, and bit 31 (slot 16: no entry)
            RecordingSink sink;CanSource can;link.can=&can;
            std::atomic<bool> halt{false};
            Snapshot b;
            sample_buttons(link,b,halt,t0);check(link.reads.empty() && !b.buttons);               // not enabled, not attached: no request
            can.enable(true);sample_buttons(link,b,halt,t0);check(link.reads.empty());            // enabled but nobody to tell
            can.attach(&sink);
            sample_buttons(link,b,halt,t0);check(b.buttons==0x5 && sink.states.size()==1 && sink.states[0]==ButtonState{0x5u,0});
            link.words[digital]=0x0002ffffu;
            b.cycles=1;sample_buttons(link,b,halt,t0);check(b.buttons==0x0002 && sink.states.back()==ButtonState{0x2u,0});   // odd cycles too
            check((link.reads==std::vector<std::uint32_t>{digital,digital}));                     // one read per cycle for all 13 slots
            can.enable(false);sample_buttons(link,b,halt,t0);check(link.reads.size()==2);          // off again: no request
            can.enable(true);
            link.fail=[](std::uint32_t) {throw std::runtime_error("Controller rejected SDO read");};
            b.cycles=2;sample_buttons(link,b,halt,t0);
            check(!b.buttons && b.button_refusal=="Controller rejected SDO read" && b.button_retry==t0+std::chrono::seconds(5) && sink.releases==1);
            link.reads.clear();
            b.cycles=3;sample_buttons(link,b,halt,t0+std::chrono::milliseconds(4999));check(link.reads.empty() && !b.buttons);
            link.fail=[](std::uint32_t) {throw std::runtime_error("Short SDO reply");};
            sample_buttons(link,b,halt,t0+std::chrono::seconds(5));
            check(b.button_refusal=="Short SDO reply" && b.button_retry==t0+std::chrono::seconds(10) && link.reads.size()==1 && sink.releases==2);
            link.fail=nullptr;
            sample_buttons(link,b,halt,t0+std::chrono::seconds(10));check(b.buttons==0x0002 && b.button_refusal.empty());
            const auto published=sink.states.size();
            link.fail=[](std::uint32_t) {throw TransportError("No matching controller reply");};
            bool passed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const TransportError&) {passed=true;}
            check(passed && b.button_refusal.empty() && b.buttons==0x0002 && sink.states.size()==published);
            link.fail=[](std::uint32_t) {throw brunner::ReplyMissed("HID I/O timed out");};        // a missed reply is thrown on, for the reader to tolerate
            passed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const brunner::ReplyMissed&) {passed=true;}
            check(passed && b.button_refusal.empty());
            link.fail=nullptr;halt=true;passed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const OperationCancelled&) {passed=true;}
            check(passed && b.button_refusal.empty() && b.buttons==0x0002);
            can.link_lost();check(sink.releases==3);                                                // the session ended: everything is released
            ButtonLink none;                                                                        // a link with no CanSource reads nothing
            std::atomic<bool> go{false};Snapshot c;sample_buttons(none,c,go,t0);check(none.reads.empty());
        }
        // The NG's layout: its own record, a mask per button on entry 3, a value per hat position on entry 4. Two reads a cycle (one per
        // object, however many controls use it), published together; bits above the six are not buttons, and a value that is no
        // position is centred.
        {
            const auto t0=Clock::now();
            const auto buttons=ButtonLink::key(2,0x2880,3),hat=ButtonLink::key(2,0x2880,4);
            ButtonLink link;RecordingSink sink;CanSource can;link.can=&can;can.attach(&sink);can.enable(true);can.set_layout(cls_e_ng_can_layout());
            std::atomic<bool> halt{false};Snapshot b;
            link.words[buttons]=0x21;link.words[hat]=3;                                            // trigger and hat push, hat east
            sample_buttons(link,b,halt,t0);
            check((link.reads==std::vector<std::uint32_t>{buttons,hat}) && b.buttons==0x21 && sink.states.back()==ButtonState{0x21u,3});
            link.words[buttons]=0xff;link.words[hat]=9;                                            // two high bits are not buttons; 9 is not a hat position
            sample_buttons(link,b,halt,t0);check(sink.states.back()==ButtonState{0x3fu,0} && b.buttons==0x3f);
            link.words[hat]=0;link.words[buttons]=0;sample_buttons(link,b,halt,t0);check(sink.states.back()==ButtonState{0,0});
            // The hat read missing ends the whole cycle (nothing half-published), and a refusal of either releases everything.
            const auto published=sink.states.size();
            link.fail=[&](std::uint32_t k) {if (k==hat) throw brunner::ReplyMissed("HID I/O timed out");};
            bool missed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const brunner::ReplyMissed&) {missed=true;}
            check(missed && sink.states.size()==published);
            link.fail=[&](std::uint32_t k) {if (k==hat) throw std::runtime_error("Controller rejected SDO read");};
            sample_buttons(link,b,halt,t0);
            check(!b.buttons && b.button_refusal=="Controller rejected SDO read" && sink.releases==1 && sink.states.size()==published);
            // The original CLS-E's layout again reads one object.
            ButtonLink plain;plain.can=&can;plain.words[ButtonLink::key(2,0x60fd,0)]=0x00040000u;
            can.set_layout(cls_e_can_layout());Snapshot c;
            sample_buttons(plain,c,halt,t0+std::chrono::seconds(30));
            check(plain.reads.size()==1 && c.buttons==4 && sink.states.back()==ButtonState{4u,0});
        }
        // A layout is whatever the model says: the node is part of the object, a mask needs all its bits, a value the exact value, two hat
        // positions pressed at once give the lower, and a layout with no entries reads nothing and presses nothing.
        {
            const auto t0=Clock::now();
            CanLayout custom;
            custom[{InputKind::button,1}]=CanSignal{1,0x2880,3,true,0x01};                         // node 1
            custom[{InputKind::button,2}]=CanSignal{2,0x2880,3,true,0x01};                         // the same object on node 2: its own read
            custom[{InputKind::button,3}]=CanSignal{2,0x2880,3,true,0x06};                         // two bits: both must be set
            custom[{InputKind::hat,6}]=CanSignal{2,0x2880,4,false,2};
            custom[{InputKind::hat,3}]=CanSignal{2,0x2880,4,true,0x02};                            // a mask on a hat register too
            ButtonLink link;RecordingSink sink;CanSource can;link.can=&can;can.attach(&sink);can.enable(true);can.set_layout(custom);
            std::atomic<bool> halt{false};Snapshot b;
            link.words[ButtonLink::key(1,0x2880,3)]=1;link.words[ButtonLink::key(2,0x2880,3)]=0x04;link.words[ButtonLink::key(2,0x2880,4)]=2;
            sample_buttons(link,b,halt,t0);
            check(link.reads.size()==3);                                                           // three distinct objects, once each
            check(sink.states.back()==ButtonState{0x1u,3});                                        // node 1's bit 0 is button 1; node 2's 0x04 is not button 2 or 3; hats 3 and 6 both: the lower
            link.words[ButtonLink::key(2,0x2880,3)]=0x07;link.words[ButtonLink::key(2,0x2880,4)]=0;
            sample_buttons(link,b,halt,t0);
            check(sink.states.back()==ButtonState{0x7u,0});                                        // buttons 1, 2 and 3 (0x06 is all set)
            CanSource empty;empty.attach(&sink);empty.enable(true);empty.set_layout({});
            ButtonLink quiet;quiet.can=&empty;Snapshot q;
            sample_buttons(quiet,q,halt,t0);
            check(quiet.reads.empty() && sink.states.back()==ButtonState{0,0} && q.buttons==0);
        }
        // Pressing them: take the device and let every button up first, send
        // only changes, let all up when unknown, retry a refusal every 5 s and
        // tell each line once until the device has stayed taken for 5 s.
        {
            const auto t0=Clock::now();
            const auto at=[&](int ms) {return t0+std::chrono::milliseconds(ms);};
            const auto quiet=[](const ButtonPresser::Lines& l) {return l.empty();};
            const auto says=[](const ButtonPresser::Lines& l,const std::string& line) {return l.size()==1 && l[0]==line;};
            const std::string pressing32="Yoke buttons: pressing vJoy device 1's 32 buttons";
            const auto off=[](const std::string& why) {return "Yoke buttons off: "+why+"; trying again every 5 seconds";};
            FakeJoystick j;ButtonPresser presser;
            check(says(presser.update(j,std::nullopt,t0),pressing32));
            check((j.take()==Calls{"load","acquire","reset"}) && presser.pressing());
            check(quiet(presser.update(j,0x5u,at(10))) && (j.take()==Calls{"down 1","down 3"}) && presser.pressed()==0x5u);
            check(quiet(presser.update(j,0x5u,at(20))) && j.take().empty());
            check(quiet(presser.update(j,0x4u,at(30))) && (j.take()==Calls{"up 1"}));
            check(quiet(presser.update(j,0x80000004u,at(40))) && (j.take()==Calls{"down 32"}));
            check(quiet(presser.update(j,std::nullopt,at(50))) && (j.take()==Calls{"reset"}) && presser.pressed()==0);
            check(quiet(presser.update(j,std::nullopt,at(60))) && j.take().empty());
            // Taken for 5 s, a refused press is told: all up, the device given
            // back, five seconds' wait, and taking it again is told too.
            check(quiet(presser.update(j,std::nullopt,at(5000))));
            j.obeys=false;
            check(says(presser.update(j,0x2u,at(5010)),off("vJoy refused a button")));
            check((j.take()==Calls{"down 2","reset","relinquish"}) && !presser.pressing() && presser.pressed()==0);
            check(quiet(presser.update(j,0x2u,at(10009))) && j.take().empty());
            j.obeys=true;
            check(says(presser.update(j,0x2u,at(10010)),pressing32) && (j.take()==Calls{"load","acquire","reset","down 2"}));
            // Taken and refused in the same update: both lines, in order; then,
            // taken and refused at once retry after retry, told no more.
            {
                struct Refuses:FakeJoystick {bool set(unsigned,bool) override {calls.push_back("set");return false;}} r;
                ButtonPresser q;
                const auto lines=q.update(r,0x1u,t0);
                check(lines.size()==2 && lines[0]==pressing32 && lines[1]==off("vJoy refused a button"));
                check((r.take()==Calls{"load","acquire","reset","set","reset","relinquish"}));
                for (int seconds:{5,10,15}) {
                    check(quiet(q.update(r,0x1u,t0+std::chrono::seconds(seconds))));
                    check((r.take()==Calls{"load","acquire","reset","set","reset","relinquish"}));
                }
            }
            // Another program holding the device: told once, retried quietly.
            presser.close(j);check((j.take()==Calls{"reset","relinquish"}) && !presser.pressing());
            j.state=FakeJoystick::Status::busy;
            check(says(presser.update(j,0x2u,at(26000)),off("vJoy device 1 is in use by another program")));
            check(quiet(presser.update(j,0x2u,at(31000))) && (j.take()==Calls{"load","load"}));
            j.state=FakeJoystick::Status::available;
            check(says(presser.update(j,0x2u,at(36000)),pressing32));
            check((j.take()==Calls{"load","acquire","reset","down 2"}));
            // Already ours (no second acquire), and a device with fewer buttons:
            // slots past its count are never pressed.
            presser.close(j);j.take();j.state=FakeJoystick::Status::owned;j.count=16;
            check(says(presser.update(j,0x00018000u,at(37000)),"Yoke buttons: pressing vJoy device 1's 16 buttons"));
            check((j.take()==Calls{"load","reset","down 16"}) && presser.pressed()==0x8000u);
            // No feeder, the driver off, no buttons: each told once, the device
            // never left taken.
            presser.close(j);j.take();j.missing="vJoy is not installed (no 64-bit folder in its uninstall entry)";
            check(says(presser.update(j,0x1u,at(38000)),off(j.missing)));
            check(quiet(presser.update(j,0x1u,at(43000))));
            j.missing.clear();j.on=false;
            check(says(presser.update(j,0x1u,at(48000)),off("vJoy is installed but switched off")));
            j.on=true;j.state=FakeJoystick::Status::available;j.count=0;j.take();
            check(says(presser.update(j,0x1u,at(53000)),off("vJoy device 1 has no buttons")));
            check((j.take()==Calls{"load","acquire","relinquish"}) && !presser.pressing());
            j.state=FakeJoystick::Status::available;j.takes=false;j.count=32;
            check(says(presser.update(j,0x1u,at(58000)),off("vJoy device 1 could not be taken")));
            check(quiet(presser.update(j,0x1u,at(63000))) && !presser.pressing());
            for (auto state:{FakeJoystick::Status::missing,FakeJoystick::Status::unknown}) {
                FakeJoystick k;k.state=state;ButtonPresser q;
                check(!quiet(q.update(k,0x1u,t0)) && !q.pressing() && (k.take()==Calls{"load"}));
            }
            // Closing a device it never took sends nothing.
            {FakeJoystick k;ButtonPresser q;k.missing="no";q.update(k,0x1u,t0);k.take();q.close(k);check(k.take().empty());}
        }
        // The log of changes: at most one line per 100 ms, ending on the
        // latest, and naming a press that came and went between two lines.
        {
            const auto t0=Clock::now();
            const auto at=[&](int ms) {return t0+std::chrono::milliseconds(ms);};
            ButtonLog l;
            check(l.update(0u,t0).empty());
            check(l.update(0x1u,at(10))=="Yoke buttons down: 1");
            check(l.update(0x3u,at(60)).empty());
            check(l.update(0x3u,at(110))=="Yoke buttons down: 1, 2");
            check(l.update(0u,at(300))=="Yoke buttons down: none");
            check(l.update(std::nullopt,at(500)).empty() && l.update(0u,at(700)).empty());
            check(l.update(0x80010000u,at(800))=="Yoke buttons down: 17, 32");
            check(l.update(std::nullopt,at(900)).empty() && l.update(0x80010000u,at(1000))=="Yoke buttons down: 17, 32");
            ButtonLog taps;
            check(taps.update(0u,t0).empty() && taps.update(0x800u,at(10))=="Yoke buttons down: 12");
            check(taps.update(0u,at(40)).empty() && taps.update(0x800u,at(70)).empty()); // up and down again inside the spacing
            check(taps.update(0x800u,at(110))=="Yoke buttons down: 12; let up and pressed again since the last line: 12");
            check(taps.update(0u,at(150)).empty() && taps.update(0x800u,at(180)).empty() && taps.update(0u,at(200)).empty());
            check(taps.update(0u,at(210))=="Yoke buttons down: none; tapped since the last line: 12"); // up, and a whole tap after
            check(taps.update(0u,at(320)).empty()); // told once
            check(taps.update(0x1u,at(330))=="Yoke buttons down: 1");
            check(taps.update(0x5u,at(360)).empty() && taps.update(0x1u,at(390)).empty()); // 3 tapped while 1 is held
            check(taps.update(0x1u,at(440))=="Yoke buttons down: 1; tapped since the last line: 3");
            check(taps.update(0x3u,at(450)).empty() && taps.update(std::nullopt,at(460)).empty()); // unknown forgets it all
            check(taps.update(0u,at(700)).empty() && taps.update(0x2u,at(710))=="Yoke buttons down: 2");
        }

        std::cout<<"virtual joystick tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
