// Tests for the yoke's buttons on a virtual joystick (hid/win/buttons.hpp), built on Windows
// only with the code they check (hid/yoke_buttons.hpp). Plain C++, a fake joystick and a fake link.
#include "hid/win/buttons.hpp"
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace brunner;
void check_at(bool okay,int line) { if (!okay) throw std::runtime_error("check failed line "+std::to_string(line)); }
#define check(x) check_at((x),__LINE__)
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
struct ButtonLink {
    std::map<Object,std::uint32_t> words;std::vector<Object> reads;std::function<void(Object)> fail;
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& stop) {
        check(node==2);
        if (stop) throw OperationCancelled{};
        reads.push_back(object);
        if (fail) fail(object);
        return words[object];
    }
};
int main() {
    try {
        check(pressed_slots(0x80018001u)=="1, 16, 17, 32" && pressed_slots(0x2800u)=="12, 14" && pressed_slots(0)=="none");
        // What the joystick shows: the word as read, slot n in bit n-1, and
        // nothing known without a live link or with readings over 250 ms old.
        {
            const auto t0=Clock::now();
            Snapshot b;b.connected=true;b.cycles=3;b.sampled=t0;
            check(live_buttons(b,t0)==0u); // not read yet: nothing pressed
            b.buttons=std::uint16_t{0x8001};check(live_buttons(b,t0)==0x8001u);
            check(live_buttons(b,t0+std::chrono::milliseconds(250))==0x8001u);
            check(!live_buttons(b,t0+std::chrono::milliseconds(251)));
            check(live_buttons(b,t0-std::chrono::milliseconds(5))==0x8001u); // stamped after now: fresh
            auto off=b;off.connected=false;check(!live_buttons(off,t0));
            auto none=b;none.cycles=0;check(!live_buttons(none,t0));
        }
        // Reading them: the one word, in every cycle. A refusal or a garble
        // leaves the buttons unread, retried from 5 s on; the link failing
        // and the reader stopping pass through and record nothing.
        {
            const auto t0=Clock::now();
            ButtonLink link;link.words[Object::digital_inputs]=0x8001ffffu;
            std::atomic<bool> halt{false};
            Snapshot b;
            sample_buttons(link,b,halt,t0);check(b.buttons==0x8001 && button_word(b)==0x8001u);
            link.words[Object::digital_inputs]=0x0002ffffu;
            b.cycles=1;sample_buttons(link,b,halt,t0);check(b.buttons==0x0002 && button_word(b)==0x0002u); // odd cycles too
            check((link.reads==std::vector<Object>{Object::digital_inputs,Object::digital_inputs}));
            link.fail=[](Object) {throw std::runtime_error("Controller rejected SDO read");};
            b.cycles=2;sample_buttons(link,b,halt,t0);
            check(!b.buttons && button_word(b)==0u && b.button_refusal=="Controller rejected SDO read" && b.button_retry==t0+std::chrono::seconds(5));
            link.reads.clear();
            b.cycles=3;sample_buttons(link,b,halt,t0+std::chrono::milliseconds(4999));check(link.reads.empty() && !b.buttons);
            link.fail=[](Object) {throw std::runtime_error("Short SDO reply");};
            sample_buttons(link,b,halt,t0+std::chrono::seconds(5));
            check(b.button_refusal=="Short SDO reply" && b.button_retry==t0+std::chrono::seconds(10) && link.reads.size()==1);
            link.fail=nullptr;
            sample_buttons(link,b,halt,t0+std::chrono::seconds(10));check(b.buttons==0x0002 && b.button_refusal.empty());
            link.fail=[](Object) {throw TransportError("No matching controller reply");};
            bool passed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const TransportError&) {passed=true;}
            check(passed && b.button_refusal.empty() && b.buttons==0x0002);
            link.fail=nullptr;halt=true;passed=false;
            try {sample_buttons(link,b,halt,t0);} catch (const OperationCancelled&) {passed=true;}
            check(passed && b.button_refusal.empty() && b.buttons==0x0002);
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

        std::cout<<"brunner buttons tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
