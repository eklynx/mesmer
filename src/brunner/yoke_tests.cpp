#include "brunner/yoke.hpp"
#include "config.hpp"
#include <iostream>
#include <sstream>
#include <vector>

// Lifecycle scenarios driven through simulated reader snapshots. The reader
// itself is Windows-only; these tests pin down what the supervisor asks of it
// and what the pilot is told, frame by frame.
using namespace brunner;
namespace {
void require(bool okay,const char* what) {if (!okay) throw std::runtime_error(what);}
struct Sim {
    YokeSupervisor yoke;
    YokeInputs in;
    Clock::time_point t=Clock::now();
    std::vector<YokeAction> actions;
    bool referenced_on_connect=false;
    Sim() {in.now=t;in.sim_steady=true;in.reader_running=true;}
    Snapshot& s() {return in.snapshot;}
    void connect(std::uint16_t status) {
        s().connected=true;s().bridge_present=true;s().cycles=1;s().sampled=t;
        for (auto& a:s().axes) {a.firmware=367;a.status=status;a.encoder=0;}
        s().session_reference=referenced_stopped_status(status);
    }
    void disconnect(bool bridge) {s()=Snapshot{};s().bridge_present=bridge;}
    // One supervisor frame; a simulated reader reacts to the decision on the next frame.
    YokeDecision frame(int ms=50) {
        t+=std::chrono::milliseconds(ms);in.now=t;s().sampled=t;if (s().connected) ++s().cycles;
        auto d=yoke.step(in);
        if (d.action!=YokeAction::none) actions.push_back(d.action);
        return d;
    }
    // Run frames until the supervisor issues an action or the budget is spent.
    YokeAction run_until_action(int budget_ms,int step_ms=50) {
        for (int elapsed=0;elapsed<budget_ms;elapsed+=step_ms) {
            const auto d=frame(step_ms);
            if (d.action!=YokeAction::none) return d.action;
        }
        return YokeAction::none;
    }
    void reader_reference_pass() {
        s().loading_fault=false;s().stop_unconfirmed=false;s().reference_active=true;s().session_reference=false;frame();
        require(yoke.step(in).status==centring_text,"Centring must be announced while the motors move");
        s().reference_active=false;s().session_reference=true;
        for (auto& a:s().axes) a.status=0x0431;
        s().reference_result="Reference and centering passed; motors disabled";
    }
    void reader_reference_fail() {
        s().reference_active=true;s().loading_fault=false;frame();
        s().reference_active=false;s().session_reference=false;s().loading_fault=true;
        s().reference_result="Controller reported an error during reference test; motors disabled";
    }
    void reader_forces_on() {
        s().loading_active=true;s().flight_session=true;s().flight_suspended=false;s().tuning_valid=true;
        for (auto& a:s().axes) a.status=0x1437;
    }
    void reader_forces_off(bool suspended) {
        s().loading_active=false;s().flight_suspended=suspended;s().session_reference=true;
        for (auto& a:s().axes) a.status=0x0431;
    }
    void set_input_ready() {in.input_ready=s().connected && s().session_reference && !s().reference_active && !s().loading_fault;}
};
bool no_actions(const Sim& sim,std::size_t from) {return sim.actions.size()==from;}
void cold_start_with_yoke_on() {
    Sim sim;
    sim.in.sim_steady=false; // X-Plane still loading
    sim.disconnect(false);
    auto d=sim.frame();
    require(d.status=="Yoke off or unplugged - check its power switch and USB cable" && d.show_notice,"Absent bridge must read as unplugged");
    sim.connect(0x0031); // powered, never referenced this power session
    d=sim.frame();
    require(d.status=="Yoke waiting for X-Plane" && d.action==YokeAction::none,"No motion before the simulator is steady");
    sim.in.sim_steady=true;
    d=sim.frame();
    require(d.status==centring_text && d.show_notice && d.action==YokeAction::none,"Warning line precedes any homing motion");
    // Pilot is still touching the yoke: encoder moving.
    for (int i=0;i<10;++i) {sim.s().axes[0].encoder+=200;require(sim.frame().action==YokeAction::none,"Centring must wait for a still yoke");}
    require(sim.run_until_action(1500)==YokeAction::reference,"A still yoke after a steady simulator starts centring");
    sim.reader_reference_pass();
    sim.set_input_ready();
    d=sim.frame();
    require(d.status=="Ready - force feel off" && !d.show_notice && d.action==YokeAction::none,"Ready with motors off hides the line");
    std::cout<<"cold start with yoke on: ok\n";
}
void yoke_switched_on_after_xplane() {
    Sim sim;
    sim.disconnect(true); // bridge enumerated, controllers silent
    auto d=sim.frame();
    require(d.status=="Yoke off or unplugged - check its power switch and USB cable" && d.show_notice,"Present bridge without controllers reads as switched off");
    require(no_actions(sim,0),"Self-healing reader needs no restart from the supervisor");
    sim.connect(0x0031);
    require(sim.run_until_action(2000)==YokeAction::reference,"Power-on yoke centres once still");
    require(sim.frame().status==centring_text,"Line stays while the request is pending");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel off","Hand-over after centring");
    std::cout<<"yoke switched on after X-Plane: ok\n";
}
void usb_unplugged_and_replugged_powered() {
    Sim sim;
    sim.connect(0x0031);require(sim.run_until_action(2000)==YokeAction::reference,"initial centring");
    sim.reader_reference_pass();sim.set_input_ready();sim.frame();
    const auto before=sim.actions.size();
    sim.disconnect(false);sim.in.input_ready=false;
    auto d=sim.frame();
    require(d.status=="Yoke off or unplugged - check its power switch and USB cable" && d.show_notice,"Mid-flight unplug is told plainly");
    // The controller kept its reference across the USB blip: statusword 0x0431 on reconnect.
    sim.connect(0x0431);sim.set_input_ready();
    d=sim.frame();
    require(d.action==YokeAction::none && d.status=="Ready - force feel off","Kept reference reconnects without moving the yoke");
    require(no_actions(sim,before),"No centring after a kept-reference reconnect");
    std::cout<<"USB unplugged and replugged with yoke powered: ok\n";
}
void yoke_power_cycled_mid_flight() {
    Sim sim;
    sim.connect(0x0031);require(sim.run_until_action(2000)==YokeAction::reference,"initial centring");
    sim.reader_reference_pass();sim.set_input_ready();sim.frame();
    sim.disconnect(true);sim.in.input_ready=false;
    require(sim.frame().status=="Yoke off or unplugged - check its power switch and USB cable","Power loss with the bridge present reads as switched off");
    sim.connect(0x0031); // power-on word: reference lost
    auto d=sim.frame();
    require(d.status==centring_text && d.action==YokeAction::none,"Warn before re-centring");
    require(sim.run_until_action(2000)==YokeAction::reference,"Re-centre after a power cycle, once still");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel off","Back to input after re-centring");
    std::cout<<"yoke switched off and on mid-flight: ok\n";
}
void aircraft_change_and_back() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0031);require(sim.run_until_action(2000)==YokeAction::reference,"centring");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Force feel on: forces start when steady");
    sim.reader_forces_on();
    require(sim.frame().status=="Ready - force feel on","Forces active");
    // Aircraft load: the plugin reports the sim unsteady; the reader suspends forces cleanly.
    sim.in.sim_steady=false;sim.in.input_ready=false;
    sim.reader_forces_off(true);
    auto d=sim.frame();
    require(d.status=="Ready - force feel waiting for X-Plane" && d.action==YokeAction::none,"Suspension is not a fault");
    sim.in.sim_steady=true;sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Forces resume on the new aircraft without re-centring");
    require(std::count(sim.actions.begin(),sim.actions.end(),YokeAction::reference)==1,"No second centring across an aircraft change");
    sim.reader_forces_on();
    // And back to the first aircraft.
    sim.in.sim_steady=false;sim.reader_forces_off(true);sim.frame();
    sim.in.sim_steady=true;
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Forces resume again after changing back");
    std::cout<<"aircraft change and back: ok\n";
}
void pause_and_resume() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"kept reference goes straight to forces");
    sim.reader_forces_on();sim.frame();
    sim.in.paused=true;sim.in.sim_steady=false;sim.reader_forces_off(true);
    auto d=sim.frame();
    require(d.status=="Ready - force feel paused with X-Plane" && !d.show_notice && d.action==YokeAction::none,"Pause keeps input, hides the line");
    sim.in.paused=false;sim.in.sim_steady=true;
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Resume restarts forces");
    sim.reader_forces_on();
    require(sim.frame().status=="Ready - force feel on","Forces back after resume");
    // Pause with force feel off: nothing to do, yoke keeps working.
    sim.in.force_feel=false;
    require(sim.run_until_action(2000)==YokeAction::stop_forces,"Switching force feel off ends the loading session");
    sim.reader_forces_off(false);
    sim.in.paused=true;sim.in.sim_steady=false;
    d=sim.frame();
    require(d.status=="Ready - force feel off" && !d.show_notice && d.action==YokeAction::none,"Paused, motors off: still ready");
    std::cout<<"pause and resume: ok\n";
}
void developer_test_ends_or_is_stopped() {
    Sim sim;
    sim.connect(0x0431);sim.set_input_ready();sim.frame();
    sim.yoke.developer_test();
    auto d=sim.frame();
    require(d.status=="Developer test starting" && d.action==YokeAction::none,"Supervisor stands back for a supervised test");
    sim.s().reference_active=true;sim.s().session_reference=false;sim.in.input_ready=false;
    require(sim.frame().status==centring_text,"Test centring uses the same line");
    sim.s().reference_active=false;sim.s().loading_active=true;sim.s().loading_result="Spring active: roll 40%, pitch 40%; live calibration available";
    require(sim.frame().status=="Spring active: roll 40%, pitch 40%; live calibration available","Developer status comes from the reader while the test runs");
    sim.s().loading_active=false;sim.s().session_reference=true;sim.set_input_ready();
    d=sim.frame();
    require(!sim.yoke.developer_test_active() && d.status=="Ready - force feel off","Test end hands back to automatic operation");
    // Stopped by the pilot: motors off, reader disconnected, nothing automatic until Retry.
    sim.yoke.stop();sim.disconnect(false);sim.in.reader_running=false;sim.in.input_ready=false;
    d=sim.frame();
    require(d.status=="Yoke stopped - Retry to resume" && d.retry_offered && d.action==YokeAction::none,"Stop holds");
    for (int i=0;i<40;++i) require(sim.frame().action==YokeAction::none,"Hold never restarts by itself");
    sim.yoke.retry();
    require(sim.frame().action==YokeAction::start_reader,"Retry restarts the reader");
    sim.in.reader_running=true;sim.connect(0x0431);sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel off","Kept reference: no motion after Retry");
    std::cout<<"developer test ends or is stopped: ok\n";
}
void controller_fault_recovers_or_asks_once() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    sim.reader_forces_on();sim.frame();
    // Controller protection trips: loading ends in a fault, motors off, reference lost.
    sim.s().loading_active=false;sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
    for (auto& a:sim.s().axes) a.status=0x0031;
    auto d=sim.frame();
    require(d.status==centring_text && d.action==YokeAction::none,"Recovery is announced before it moves");
    const auto first=sim.actions.size();
    for (int i=0;i<30;++i) require(sim.frame().action==YokeAction::none,"Backoff before the first automatic recovery");
    require(sim.run_until_action(3000)==YokeAction::recover,"First automatic recovery restores the baseline before centring");
    sim.reader_reference_fail();
    require(sim.run_until_action(5000)==YokeAction::recover,"Second automatic recovery");
    sim.reader_reference_fail();
    d=sim.frame();
    require(d.status=="Yoke fault - Retry" && d.retry_offered && d.show_notice,"Bounded retries, then one action for the pilot");
    for (int i=0;i<100;++i) require(sim.frame().action==YokeAction::none,"No more automatic motion after asking");
    require(sim.actions.size()==first+2,"Exactly two automatic recoveries");
    sim.yoke.retry();
    require(sim.run_until_action(2000)==YokeAction::recover,"Retry recovers and centres once more");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Recovered: forces return");
    // A recovery that succeeds on the first try.
    sim.reader_forces_on();sim.frame();
    sim.s().loading_active=false;sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
    require(sim.run_until_action(4000)==YokeAction::recover,"Automatic recovery after a later fault");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel starting","Recovered without asking");
    // An unconfirmed motor stop is never retried automatically.
    sim.reader_forces_on();sim.frame();
    sim.s().loading_active=false;sim.s().stop_unconfirmed=true;sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
    d=sim.frame();
    require(d.status=="Yoke fault - switch the yoke off, then Retry" && d.retry_offered,"Unconfirmed stop asks immediately");
    for (int i=0;i<100;++i) require(sim.frame().action==YokeAction::none,"Unconfirmed stop never moves by itself");
    std::cout<<"controller fault recovers or asks once: ok\n";
}
void force_feel_on_off_from_menu() {
    Sim sim;
    sim.connect(0x0431);sim.set_input_ready();
    auto d=sim.frame();
    require(d.status=="Ready - force feel off" && d.action==YokeAction::none,"Default is off: input only, motors off");
    for (int i=0;i<40;++i) require(sim.frame().action==YokeAction::none,"Off means no spring request ever");
    sim.in.force_feel=true;
    require(sim.run_until_action(2000)==YokeAction::start_forces,"On: forces start");
    sim.reader_forces_on();
    require(sim.frame().status=="Ready - force feel on","On");
    sim.in.force_feel=false;
    require(sim.run_until_action(2000)==YokeAction::stop_forces,"Off: forces stop without disconnecting");
    d=sim.frame();
    require(d.status=="Ready - force feel switching off" && !d.show_notice,"Switching off keeps input");
    sim.reader_forces_off(false);
    require(sim.frame().status=="Ready - force feel off","Off again; yoke still centred and working");
    std::cout<<"force feel on/off from menu: ok\n";
}
void lab_lease_blocks_automation() {
    Sim sim;sim.in.lab_lease=true;
    sim.disconnect(false);sim.in.reader_running=false;
    auto d=sim.frame();
    require(d.action==YokeAction::none && d.status=="Yoke paused for a developer session","Lease blocks reader restart");
    sim.in.reader_running=true;sim.connect(0x0031);
    require(sim.run_until_action(3000)==YokeAction::none,"Lease blocks centring");
    sim.in.lab_lease=false;
    require(sim.run_until_action(3000)==YokeAction::reference,"Lease expiry resumes automatic operation");
    std::cout<<"lab lease blocks automation: ok\n";
}
void status_text() {
    for (const char* line:{"Yoke off or unplugged - check its power switch and USB cable","Yoke off or unplugged - check its power switch and USB cable",centring_text,
        "Yoke fault - Retry","Ready - force feel off","Ready - force feel on","Yoke waiting for X-Plane"}) {
        const std::string text(line);
        require(text.find("0x")==std::string::npos && text.find("SDO")==std::string::npos && text.find("HID")==std::string::npos,"No register names or abbreviations");
        require(text.rfind("Yoke",0)==0 || text.rfind("Ready",0)==0,"Every line starts with the subject");
    }
    Sim sim;sim.in.enabled=false;
    require(sim.frame().status=="Yoke plugin is disabled" && !sim.frame().show_notice,"Disabled plugin draws nothing");
    std::cout<<"status text: ok\n";
}
void configuration_defaults() {
    const std::string base="schema_version=2\nroll_spring_percent=40\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\n"
        "reference_airspeed_knots=316\nminimum_spring_percent=20\nmaximum_trim_counts=600\nhalf_gain_pa=200\nfull_trim_pa=51\n";
    std::istringstream legacy(base);
    const auto c=parse_configuration(legacy);
    require(c.flight.force_feel==0 && c.flight.roll_torque_units_per_nm==1000,"Existing files keep force feel off and roll's derived drive scale");
    require(c.retired==std::vector<std::string>({"full_trim_pa","half_gain_pa","maximum_trim_counts","minimum_spring_percent","pitch_spring_percent","roll_spring_percent"}),
        "Schema 2's shared floor and pressure were roll's spring, and both firmests the drive's own ceiling replaced (decision 16): retired, reported");
    std::istringstream on(base+"force_feel=1\nplain_spring_percent=30\n");
    const auto d=parse_configuration(on);
    require(d.flight.force_feel==1 && d.retired.size()==7 && std::count(d.retired.begin(),d.retired.end(),"plain_spring_percent")==1,"Step 1 flips force feel from configuration; the plain spring is retired (decision 6)");
    bool threw=false;
    try {std::istringstream bad(base+"roll_torque_units_per_nm=99\n");parse_configuration(bad);} catch (const std::exception&) {threw=true;}
    require(threw,"Roll's drive scale stays within its bounds");
    threw=false;
    try {std::istringstream bad(base+"force_feel=2\n");parse_configuration(bad);} catch (const std::exception&) {threw=true;}
    require(threw,"Force feel default is a switch");
    std::cout<<"configuration defaults: ok\n";
}

// The fault recurs every time force feel starts: centring passes, the spring
// faults. The budget must cover the whole episode, not reset on each centring.
void recurring_force_feel_fault_is_bounded() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    int recoveries=0,starts=1;
    for (int cycle=0;cycle<20;++cycle) {
        // The spring faults as it starts: motors off, reference lost.
        sim.s().loading_active=false;sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
        for (auto& a:sim.s().axes) a.status=0x0031;
        const auto action=sim.run_until_action(15000);
        if (action==YokeAction::none) break;
        require(action==YokeAction::recover,"Only recovery follows a fault");
        ++recoveries;
        sim.reader_reference_pass();sim.set_input_ready();
        const auto next=sim.run_until_action(3000);
        if (next==YokeAction::none) break;
        require(next==YokeAction::start_forces,"After centring, force feel starts again");
        ++starts;
    }
    const auto d=sim.frame();
    require(recoveries==int(YokeSupervisor::max_recoveries),"Bounded recoveries across repeated force-feel faults");
    require(starts<=recoveries+1,"No force-feel start without a centring");
    require(d.status=="Yoke fault - Retry" && d.retry_offered && d.show_notice,"The pilot is asked once the budget is spent");
    for (int i=0;i<200;++i) require(sim.frame().action==YokeAction::none,"No more automatic motion after asking");
    std::cout<<"recurring force feel fault is bounded: ok\n";
}
// The drive refuses recovery: the fault never clears and no centring is ever
// seen. Each attempt still counts, and the pilot is asked.
void refused_recovery_is_bounded() {
    Sim sim;
    sim.connect(0x0431);sim.set_input_ready();sim.frame();
    sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
    for (auto& a:sim.s().axes) a.status=0x0039;
    int recoveries=0;YokeDecision d;
    for (int elapsed=0;elapsed<60000;elapsed+=50) {
        d=sim.frame();
        if (d.action==YokeAction::recover) ++recoveries;
        if (d.retry_offered) break;
    }
    require(d.retry_offered && d.status=="Yoke fault - Retry","A refused recovery ends in one question for the pilot");
    require(recoveries==int(YokeSupervisor::max_recoveries),"Each unanswered recovery counts");
    for (int i=0;i<200;++i) require(sim.frame().action==YokeAction::none,"No recovery is re-issued after asking");
    std::cout<<"refused recovery is bounded: ok\n";
}
// A hand pulling the yoke slowly moves it far over a second even though each
// frame's step is small. That is not still. A repeated sample proves nothing.
void slow_pull_is_not_still() {
    Sim sim;
    sim.connect(0x0031);
    for (int i=0;i<200;++i) {
        sim.s().axes[1].encoder+=50; // 50 counts per 16 ms frame: about 3000 counts a second
        const auto d=sim.frame(16);
        require(d.action==YokeAction::none && d.status==centring_text,"A slowly moving yoke must not be centred");
    }
    const auto moved=sim.s().axes[1].encoder;
    require(sim.run_until_action(1500,16)==YokeAction::reference,"Centring starts once the yoke stops moving");
    require(sim.s().axes[1].encoder==moved,"Encoder was held still before centring");
    Sim stale;
    stale.connect(0x0031);stale.frame();
    for (int i=0;i<60;++i) {
        // The reader is busy: the same sample is published again and again.
        stale.t+=std::chrono::milliseconds(50);stale.in.now=stale.t;stale.s().sampled=stale.t;
        require(stale.yoke.step(stale.in).action==YokeAction::none,"A repeated sample is no evidence of stillness");
    }
    require(stale.run_until_action(1500)==YokeAction::reference,"Fresh samples complete the window");
    std::cout<<"slow pull is not still: ok\n";
}
void recentre_on_a_working_yoke() {
    Sim sim;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel off","working");
    sim.yoke.retry(); // the menu's Re-centre with nothing wrong
    auto d=sim.frame();
    require(d.status==centring_text && d.show_notice && d.action==YokeAction::none,"Re-centre warns before it moves");
    require(sim.run_until_action(1500)==YokeAction::reference,"Re-centre centres a referenced yoke once still");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel off","Back to work after re-centring");
    for (int i=0;i<40;++i) require(sim.frame().action==YokeAction::none,"Re-centre acts once");
    // With force feel on, the spring ends first, then the yoke centres, then forces return.
    sim.in.force_feel=true;
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    sim.reader_forces_on();sim.frame();
    sim.yoke.retry();
    require(sim.run_until_action(2000)==YokeAction::stop_forces,"Forces end before a re-centre");
    require(sim.frame().status==centring_text,"Warning stays up while forces end");
    sim.reader_forces_off(false);
    require(sim.run_until_action(2000)==YokeAction::reference,"Then the yoke centres");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Then force feel returns");
    std::cout<<"re-centre on a working yoke: ok\n";
}
void retry_after_unconfirmed_stop_recovers() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    sim.reader_forces_on();sim.frame();
    sim.s().loading_active=false;sim.s().stop_unconfirmed=true;sim.s().loading_fault=true;sim.s().session_reference=false;sim.in.input_ready=false;
    require(sim.frame().status=="Yoke fault - switch the yoke off, then Retry","asks");
    sim.yoke.retry();
    require(sim.run_until_action(2000)==YokeAction::recover,"Retry recovers");
    // The reader has not reacted yet: the old fault is still on display.
    for (int i=0;i<10;++i) {const auto d=sim.frame();require(d.status==centring_text && !d.retry_offered,"One Retry is enough");}
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.frame().status=="Ready - force feel starting","Recovered after the pilot's one action");
    std::cout<<"retry after unconfirmed stop recovers: ok\n";
}
// Yoke switched off while the spring is active: the reader reports the link
// lost, not an unconfirmed stop, and everything resumes at power-on.
void power_off_during_force_feel_self_heals() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    sim.reader_forces_on();sim.frame();
    sim.disconnect(true);sim.in.input_ready=false;
    auto d=sim.frame();
    require(d.status=="Yoke off or unplugged - check its power switch and USB cable" && !d.retry_offered,"Power loss is not a fault");
    for (int i=0;i<40;++i) require(sim.frame().action==YokeAction::none,"Nothing to do until the yoke is back");
    sim.connect(0x0031);
    require(sim.run_until_action(2000)==YokeAction::reference,"Power-on: centre again once still");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Forces return without the pilot");
    std::cout<<"power off during force feel self-heals: ok\n";
}
void unavailable_reasons() {
    Sim sim;
    sim.disconnect(true);sim.s().bridge_busy=true;
    require(sim.frame().status=="Yoke is in use by another program - close CLS2Sim and Simux","Busy bridge names the program to close");
    sim.disconnect(true);sim.s().loading_fault=true;
    require(sim.frame().status=="Yoke not recognised - see X-Plane's Log.txt","Refused hardware is not called switched off");
    sim.disconnect(false);
    require(sim.frame().status=="Yoke off or unplugged - check its power switch and USB cable","Absent bridge");
    // Stop after an unconfirmed motor stop: the reader keeps that flag in its final snapshot.
    sim.yoke.stop();sim.disconnect(true);sim.s().stop_unconfirmed=true;sim.in.reader_running=false;
    const auto d=sim.frame();
    require(d.status=="Yoke fault - switch the yoke off, then Retry" && d.retry_offered && d.action==YokeAction::none,"An unconfirmed stop stays visible after Stop");
    std::cout<<"unavailable reasons: ok\n";
}
// The yoke switched off and on mid-flight came back with settings the old
// plugin refused, then looped through recoveries that took pitch and roll away
// (2026-11-07). A refused force feel now changes nothing: the yoke flies on,
// the line says what to do, and nothing retries until the pilot asks or the
// yoke reconnects.
void refused_force_feel_keeps_the_controls() {
    Sim sim;sim.in.force_feel=true;
    sim.connect(0x0431);sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"forces");
    const auto refuse=[&sim](const char* line) {
        sim.s().force_unavailable=line;sim.s().loading_active=false;sim.s().loading_fault=false;
        sim.s().session_reference=true;sim.set_input_ready();
    };
    refuse("Force feel unavailable - switch the yoke off and on");
    auto d=sim.frame();
    require(d.status=="Force feel unavailable - switch the yoke off and on" && d.retry_offered && d.show_notice,"The refusal is told with its remedy");
    // That pitch and roll keep reaching X-Plane is checked in tests.cpp through
    // the input check itself; here the supervisor must simply not move the yoke.
    const auto issued=sim.actions.size();
    for (int i=0;i<600;++i) require(sim.frame().action==YokeAction::none,"No start, recovery or centring loop after a refusal");
    require(no_actions(sim,issued),"Nothing issued");
    // The pilot switches force feel off and on: one fresh attempt.
    sim.in.force_feel=false;sim.frame();sim.in.force_feel=true;sim.yoke.force_feel_switched_on();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Switching force feel on tries once more");
    refuse("Force feel unavailable - Retry");
    for (int i=0;i<200;++i) require(sim.frame().action==YokeAction::none,"Refused again: still no loop");
    // Retry on either refusal tries force feel again, and only that: the
    // reference is intact, so the yoke is never centred or recovered for it.
    for (const char* line:{"Force feel unavailable - Retry","Force feel unavailable - switch the yoke off and on"}) {
        refuse(line);sim.frame();
        const auto before=sim.actions.size();
        sim.yoke.retry();
        require(sim.run_until_action(3000)==YokeAction::start_forces,"Retry tries force feel again");
        require(sim.actions.size()==before+1,"Retry issues nothing but that attempt");
        refuse(line);
        for (int i=0;i<600;++i) require(sim.frame().action==YokeAction::none,"Refused again after Retry: no centring or recovery");
        require(sim.frame().status==line,"The same remedy is shown again");
    }
    refuse("Force feel unavailable - switch the yoke off and on");sim.frame();
    // Switching the yoke off and on is a fresh connection: it clears the refusal by itself.
    sim.disconnect(true);sim.in.input_ready=false;sim.frame();
    sim.connect(0x0031);
    require(sim.run_until_action(2000)==YokeAction::reference,"Switched on: centre once still");
    sim.reader_reference_pass();sim.set_input_ready();
    require(sim.run_until_action(2000)==YokeAction::start_forces,"Force feel returns without the pilot");
    std::cout<<"refused force feel keeps the controls: ok\n";
}
void refusal_cleared_by_switching_off_and_on_is_named() {
    Sim sim;
    sim.connect(0x0431);sim.set_input_ready();sim.frame();
    const auto refused=[&sim] {
        sim.s().reference_active=false;sim.s().loading_fault=true;sim.s().power_cycle_needed=true;++sim.s().failures;
    };
    refused(); // a Re-centre refused: nothing moved, the reference is kept
    const auto line="Yoke fault - switch the yoke off and on, then Retry";
    auto d=sim.frame();
    require(d.status==line && d.retry_offered,"A refusal only a switch-on clears is held at once and says so");
    std::size_t issued=sim.actions.size();
    for (int elapsed=0;elapsed<60000;elapsed+=50) require(sim.frame().status==line,"Held: never shown as centring");
    require(no_actions(sim,issued),"No recovery is tried by itself: it would refuse the same way");
    // Retry without switching the yoke off: one recovery, which the reader
    // has not answered yet, then refused again: held again, nothing more.
    sim.yoke.retry();
    require(sim.run_until_action(3000)==YokeAction::recover,"Retry tries one recovery");
    issued=sim.actions.size();
    for (int i=0;i<4;++i) require(sim.frame().action==YokeAction::none,"Waits for that recovery's answer");
    refused();
    require(sim.frame().status==line,"Refused again: held again at once");
    for (int elapsed=0;elapsed<60000;elapsed+=50) require(sim.frame().status==line,"Still held");
    require(no_actions(sim,issued),"No further recovery");
    // Switched off and on: a fresh connection, then Retry: one centring.
    sim.disconnect(true);sim.in.input_ready=false;
    require(sim.frame().retry_offered,"Still held while it is off");
    sim.connect(0x0031);
    d=sim.frame();
    require(d.retry_offered && d.status=="Yoke fault - Retry" && no_actions(sim,issued),"Switched on: the remedy is done, Retry is what is left");
    sim.yoke.retry();
    require(sim.run_until_action(3000)==YokeAction::reference,"Then a single centring");
    std::cout<<"refusal cleared by switching off and on is named: ok\n";
}
void input_not_reaching_xplane() {
    Sim sim;
    sim.connect(0x0431);sim.in.input_ready=false; // another add-on owns pitch and roll
    auto d=sim.frame();
    require(d.status=="Ready - force feel off" && d.show_notice,"Brief gaps keep the plain line");
    for (int elapsed=0;elapsed<1900;elapsed+=50) require(sim.frame().status=="Ready - force feel off","Grace period");
    for (int i=0;i<4;++i) d=sim.frame();
    require(d.status==input_lost_text && d.show_notice && d.action==YokeAction::none,"A lasting gap is named");
    sim.set_input_ready();
    d=sim.frame();
    require(d.status=="Ready - force feel off" && !d.show_notice,"Input back: line hidden");
    std::cout<<"input not reaching X-Plane: ok\n";
}
}
int main() {
    try {
        cold_start_with_yoke_on();
        yoke_switched_on_after_xplane();
        usb_unplugged_and_replugged_powered();
        yoke_power_cycled_mid_flight();
        aircraft_change_and_back();
        pause_and_resume();
        developer_test_ends_or_is_stopped();
        controller_fault_recovers_or_asks_once();
        force_feel_on_off_from_menu();
        lab_lease_blocks_automation();
        status_text();
        configuration_defaults();
        recurring_force_feel_fault_is_bounded();
        refused_recovery_is_bounded();
        slow_pull_is_not_still();
        recentre_on_a_working_yoke();
        retry_after_unconfirmed_stop_recovers();
        power_off_during_force_feel_self_heals();
        unavailable_reasons();
        input_not_reaching_xplane();
        refused_force_feel_keeps_the_controls();
        refusal_cleared_by_switching_off_and_on_is_named();
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
