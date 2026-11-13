#pragma once
#include "flight.hpp"

// "The yoke just works": one supervisor decides when the yoke centres, when
// force feel starts or stops, when the reader restarts, and what the pilot is
// told. It only produces decisions; the plugin executes them against the
// reader and X-Plane. Nothing here enables a motor by itself.
namespace brunner {
// The yoke must be held still before centring starts: the encoder stays within
// a small band of where it was when the window opened, for the whole window,
// read before any reference (positions are readable unreferenced). A slow,
// steady pull therefore counts as movement, and a repeated or stale sample
// counts as no evidence.
class StillEvidence {
    Clock::time_point since_{};
    std::array<std::int32_t,2> anchor_{};
    std::uint64_t cycles_=0;
    bool seen_=false;
public:
    static constexpr std::int32_t band=60; // encoder counts of travel allowed over the whole window
    static constexpr std::chrono::milliseconds window{1000},freshness{250};
    void reset() {since_={};seen_=false;cycles_=0;}
    bool observe(const Snapshot& s,Clock::time_point now) {
        if (!s.connected || s.cycles==0 || now<s.sampled || now-s.sampled>freshness) {reset();return false;}
        if (seen_ && s.cycles==cycles_) return false; // no new sample yet: nothing learned this frame
        cycles_=s.cycles;
        bool steady=seen_;
        for (unsigned i=0;i<2 && steady;++i) {
            const auto travel=static_cast<std::int64_t>(s.axes[i].encoder)-anchor_[i];
            steady=travel>=-band && travel<=band;
        }
        if (!steady || now<since_) {since_=now;for (unsigned i=0;i<2;++i) anchor_[i]=s.axes[i].encoder;}
        seen_=true;
        return steady && now-since_>=window;
    }
};
struct YokeInputs {
    Snapshot snapshot;
    bool enabled=true;
    bool reader_running=true; // worker thread alive; false after Stop or a disabled plugin
    bool sim_steady=false; // physics advancing, unpaused, controls writable
    bool paused=false;
    bool input_ready=false; // pitch/roll currently reaching X-Plane
    bool force_feel=false;
    bool lab_lease=false; // headless developer lease: no automatic hardware action
    Clock::time_point now{};
};
// recover: restore the surveyed baseline and clear controller faults with the
// motors off, then centre. reference: centre only.
enum class YokeAction {none,start_reader,reference,recover,start_forces,stop_forces};
struct YokeDecision {
    YokeAction action=YokeAction::none;
    std::string status; // the one plain-language line for menu, screen, HUD and monitor
    bool show_notice=false; // on-screen line: only while the yoke is not simply working
    bool retry_offered=false; // the pilot's single action is needed
};
inline constexpr char centring_text[]="Yoke centring - hands off";
inline constexpr char input_lost_text[]="Yoke not reaching X-Plane - another add-on may control pitch and roll";
class YokeSupervisor {
public:
    enum class Hold {none,stopped,fault};
    // Automatic recoveries per fault episode. Each issued recovery counts; the
    // budget is renewed only after a steady period of healthy operation or by
    // the pilot's own action, never by a centring that merely succeeded.
    static constexpr unsigned max_recoveries=2;
    static constexpr std::chrono::milliseconds recovery_backoff{2000},request_timeout{1500};
    // A recovery the reader never acts on (refused by the drive status, so no
    // centring is ever seen) is given this long before the next attempt.
    static constexpr std::chrono::seconds attempt_timeout{10},healthy_period{30},input_grace{2};
    // Pilot's Stop: motors off and nothing automatic until the pilot's one action.
    void stop() {hold_=Hold::stopped;developer_test_=false;test_seen_=false;requested_at_={};manual_retry_=false;recentre_=false;}
    // Pilot's Retry / Re-centre: clears any hold and asks for one fresh centring.
    // On a working yoke (no hold) it is a request to centre again even though
    // the reference is kept; after a Stop or fault a kept reference is adopted.
    void retry() {recentre_=hold_==Hold::none;hold_=Hold::none;recoveries_=0;fault_seen_=false;developer_test_=false;test_seen_=false;requested_at_={};retry_after_={};manual_retry_=true;force_retry_=true;}
    // The pilot switched force feel on: one fresh attempt even after a refusal.
    void force_feel_switched_on() {force_retry_=true;}
    // A supervised developer test is running; automatic operation resumes when it ends.
    void developer_test() {developer_test_=true;test_seen_=false;requested_at_={};}
    Hold hold() const {return hold_;}
    bool developer_test_active() const {return developer_test_;}
    unsigned recoveries() const {return recoveries_;}
    YokeDecision step(const YokeInputs& in) {
        const auto& s=in.snapshot;
        YokeDecision d;
        if (!in.enabled) {still_.reset();return finish(d,"Yoke plugin is disabled",false);}
        if (!s.connected) {
            still_.reset();requested_at_={};healthy_since_={};input_missing_since_={};developer_test_=false;test_seen_=false;answered_=unanswered;
            if (hold_==Hold::stopped) return finish(d,s.stop_unconfirmed?fault_text(s):"Yoke stopped - Retry to resume",true,true);
            if (hold_==Hold::fault) return finish(d,fault_text(s),true,true);
            if (in.lab_lease) return finish(d,"Yoke paused for a developer session",true);
            if (!in.reader_running) d.action=YokeAction::start_reader;
            return finish(d,unavailable_text(s),true);
        }
        if (s.read_only) {
            // Firmware the plugin does not drive (readonly.hpp): it only reads the yoke.
            // Never centre, recover or load, and never offer Retry for something that cannot happen.
            still_.reset();requested_at_={};healthy_since_={};input_missing_since_={};developer_test_=false;test_seen_=false;
            return finish(d,s.message,true);
        }
        if (developer_test_ && (s.reference_active || s.loading_active)) test_seen_=true;
        if (s.reference_active) {
            // A centring in progress is the recovery itself: a fault hold ends with it.
            fault_seen_=false;recentre_=false;healthy_since_={};still_.reset();
            if (hold_==Hold::fault) hold_=Hold::none;
            return finish(d,centring_text,true);
        }
        if (developer_test_) {
            if (s.loading_active) return finish(d,s.loading_result.empty()?"Developer test running":s.loading_result,true);
            if (test_seen_ || (requested_at_!=Clock::time_point{} && in.now-requested_at_>request_timeout)) {developer_test_=false;test_seen_=false;requested_at_={};}
            else {
                if (requested_at_==Clock::time_point{}) requested_at_=in.now;
                return finish(d,"Developer test starting",true);
            }
        }
        if (hold_==Hold::stopped) return finish(d,"Yoke stopped - Retry to resume",true,true);
        const bool fault=s.loading_fault || s.stop_unconfirmed;
        if (fault) {
            healthy_since_={};
            if (manual_retry_) {
                // The pilot asked once; centre again as soon as it is safe to move.
                if (in.lab_lease) return finish(d,"Yoke paused for a developer session",true);
                if (!in.sim_steady) {still_.reset();return finish(d,waiting_text(in),true);}
                if (still_.observe(s,in.now) && request(in.now)) {
                    d.action=YokeAction::recover;manual_retry_=false;recentre_=false;answered_=s.failures;
                    fault_seen_=true;retry_after_=in.now+attempt_timeout; // the still-displayed fault is this one, not a new one
                }
                return finish(d,centring_text,true);
            }
            // A refusal only a switch-on clears is never retried by itself (a
            // recovery would refuse the same way). It is held at once, unless it
            // is the one a recovery was just sent to answer and has not yet.
            if (s.power_cycle_needed && s.failures!=answered_) hold_=Hold::fault;
            if (!fault_seen_) {
                fault_seen_=true;retry_after_=in.now+recovery_backoff;
                // An unconfirmed stop is never retried by itself; a new fault
                // with the budget already spent asks the pilot at once.
                if (s.stop_unconfirmed || recoveries_>=max_recoveries) hold_=Hold::fault;
            }
            if (hold_==Hold::fault) return finish(d,fault_text(s),true,true);
            if (in.lab_lease) return finish(d,"Yoke paused for a developer session",true);
            if (!in.sim_steady) {still_.reset();return finish(d,waiting_text(in),true);}
            const bool still=still_.observe(s,in.now);
            if (in.now>=retry_after_) {
                // The previous attempt has had its time. Budget spent: ask the pilot.
                if (recoveries_>=max_recoveries) {hold_=Hold::fault;return finish(d,fault_text(s),true,true);}
                if (still && request(in.now)) {d.action=YokeAction::recover;++recoveries_;retry_after_=in.now+attempt_timeout;answered_=s.failures;}
            }
            return finish(d,centring_text,true);
        }
        fault_seen_=false;answered_=unanswered;
        if (hold_==Hold::fault) return finish(d,fault_text(s),true,true);
        if (in.lab_lease) {still_.reset();return finish(d,"Yoke paused for a developer session",!in.input_ready);}
        if (!s.session_reference) {
            healthy_since_={};
            if (!in.sim_steady) {still_.reset();return finish(d,waiting_text(in),true);}
            // The warning line is up from this frame on; motion begins only
            // after the yoke has been still for a full second.
            if (still_.observe(s,in.now) && request(in.now)) {d.action=YokeAction::reference;manual_retry_=false;recentre_=false;}
            return finish(d,centring_text,true);
        }
        manual_retry_=false;
        // Retry on a refused force feel tries force feel again: the reference
        // is intact, so the yoke does not need to move.
        if (recentre_ && in.force_feel && !s.force_unavailable.empty()) recentre_=false;
        if (recentre_) {
            // The pilot's Re-centre on a working yoke: forces off first, then a
            // fresh centring once the simulator is steady and the yoke is still.
            healthy_since_={};
            if (s.loading_active) {
                if (!s.flight_session) return finish(d,"Developer test running",true);
                if (request(in.now)) d.action=YokeAction::stop_forces;
                return finish(d,centring_text,true);
            }
            if (!in.sim_steady) {still_.reset();return finish(d,waiting_text(in),true);}
            if (still_.observe(s,in.now) && request(in.now)) {d.action=YokeAction::reference;recentre_=false;}
            return finish(d,centring_text,true);
        }
        still_.reset();
        // Healthy: force feel running, or off with the yoke centred. A steady
        // period of it renews the recovery budget; a passing centring alone does not.
        const bool healthy=!in.force_feel || (s.loading_active && s.flight_session);
        if (!healthy) healthy_since_={};
        else if (healthy_since_==Clock::time_point{}) healthy_since_=in.now;
        else if (in.now-healthy_since_>=healthy_period) recoveries_=0;
        // Referenced, steady simulator, yet pitch and roll are not reaching it:
        // after a short grace that is the yoke not working, and the line says so.
        const bool input_missing=!in.input_ready && in.sim_steady && !in.paused;
        if (!input_missing) input_missing_since_={};
        else if (input_missing_since_==Clock::time_point{}) input_missing_since_=in.now;
        const bool input_lost=input_missing && in.now-input_missing_since_>=input_grace;
        if (s.loading_active && s.flight_session) {
            if (!in.force_feel && request(in.now)) d.action=YokeAction::stop_forces;
            if (input_lost) return finish(d,input_lost_text,true);
            return finish(d,in.force_feel?"Ready - force feel on":"Ready - force feel switching off",false);
        }
        std::string ready="Ready - force feel off";
        // Force feel was refused with nothing changed: the yoke flies on, and
        // a new attempt waits for the pilot (Retry, or force feel switched on)
        // or a fresh connection, which clears the refusal.
        const bool refused=in.force_feel && !s.force_unavailable.empty() && !force_retry_;
        if (refused) {
            if (input_lost) return finish(d,input_lost_text,true);
            return finish(d,s.force_unavailable,true,true);
        }
        if (in.force_feel) {
            if (in.paused) ready="Ready - force feel paused with X-Plane";
            else if (!in.sim_steady) ready="Ready - force feel waiting for X-Plane";
            else {
                if (request(in.now)) {d.action=YokeAction::start_forces;force_retry_=false;}
                ready="Ready - force feel starting";
            }
        }
        if (input_lost) return finish(d,input_lost_text,true);
        return finish(d,std::move(ready),!in.input_ready);
    }
private:
    Hold hold_=Hold::none;
    bool developer_test_=false,test_seen_=false,fault_seen_=false,manual_retry_=false,recentre_=false,force_retry_=false;
    unsigned recoveries_=0;
    // The failure count a recovery was sent to answer.
    static constexpr unsigned unanswered=~0u;
    unsigned answered_=unanswered;
    Clock::time_point retry_after_{},requested_at_{},healthy_since_{},input_missing_since_{};
    StillEvidence still_;
    bool request(Clock::time_point now) {
        if (requested_at_!=Clock::time_point{} && now>=requested_at_ && now-requested_at_<request_timeout) return false;
        requested_at_=now;return true;
    }
    static std::string waiting_text(const YokeInputs& in) {
        return in.paused?"Yoke waiting - X-Plane is paused":"Yoke waiting for X-Plane";
    }
    static std::string fault_text(const Snapshot& s) {
        if (s.stop_unconfirmed) return "Yoke fault - switch the yoke off, then Retry";
        if (s.power_cycle_needed) return "Yoke fault - switch the yoke off and on, then Retry";
        return "Yoke fault - Retry";
    }
    // Why the reader has no yoke, with the remedy that actually applies.
    static std::string unavailable_text(const Snapshot& s) {
        if (s.stop_unconfirmed) return fault_text(s);
        if (s.bridge_busy) return "Yoke is in use by another program - close CLS2Sim and Simux";
        if (s.loading_fault) return "Yoke not recognised - see X-Plane's Log.txt"; // firmware or hardware the plugin refuses
        // The yoke's power switch also removes its USB bridge, so the two cannot
        // be told apart, and the bridge appears a moment before the controllers.
        return "Yoke off or unplugged - check its power switch and USB cable";
    }
    static YokeDecision& finish(YokeDecision& d,std::string status,bool notice,bool retry=false) {
        d.status=std::move(status);d.show_notice=notice;d.retry_offered=retry;return d;
    }
};
}
