#pragma once
#include "ui/view/telemetry.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>

#ifndef BRUNNER_BUILD_ID
#define BRUNNER_BUILD_ID "development"
#endif

namespace brunner::recording {
inline double seconds() {return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
inline std::int64_t utc_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// One producer (the simulator thread), one background consumer. No locks,
// allocation or I/O in push/pop. Overflow rejects the new record explicitly.
template<class T,std::size_t N> class Queue {
    std::array<T,N> items_{};
    std::atomic<std::size_t> head_{0},tail_{0};
public:
    bool push(const T& item) noexcept {
        const auto head=head_.load(std::memory_order_relaxed),next=(head+1)%N;
        if (next==tail_.load(std::memory_order_acquire)) return false;
        items_[head]=item;head_.store(next,std::memory_order_release);return true;
    }
    bool pop(T& item) noexcept {
        const auto tail=tail_.load(std::memory_order_relaxed);
        if (tail==head_.load(std::memory_order_acquire)) return false;
        item=items_[tail];tail_.store((tail+1)%N,std::memory_order_release);return true;
    }
};
inline std::string quote(const std::string& value) {
    std::string out="\"";
    for (const unsigned char c:value) {
        if (c=='"' || c=='\\') {out+='\\';out+=char(c);}
        else if (c<32 || c>126) out+=' '; // event text is bounded ASCII; no raw device paths
        else out+=char(c);
    }
    return out+'"';
}
inline const char* mode(monitor::Mode m) {
    switch (m) {
    case monitor::Mode::input:return "input";case monitor::Mode::reference:return "reference";
    case monitor::Mode::bench:return "bench";case monitor::Mode::flight:return "flight";
    case monitor::Mode::paused:return "paused";case monitor::Mode::fault:return "fault";
    default:return "disconnected";
    }
}
inline void number(std::ostream& out,double v) {if (std::isfinite(v)) out<<v;else out<<"null";}
inline void tuning(std::ostream& out,const Tuning& t) {
    out<<"{\"roll_percent\":"<<t.roll_percent<<",\"pitch_percent\":"<<t.pitch_percent<<",\"neutral_counts\":"<<t.pitch_trim
       <<",\"roll_centre_counts\":"<<t.roll_trim<<'}';
}
inline bool changed(const monitor::Frame& a,const monitor::Frame& b) {
    return a.mode!=b.mode || a.fresh!=b.fresh || a.motors!=b.motors || a.status!=b.status
        || a.tuning!=b.tuning || a.bias_verified!=b.bias_verified || a.bias!=b.bias
        || a.sim_valid!=b.sim_valid || a.limited!=b.limited || a.owns_axes!=b.owns_axes || a.owns_trim!=b.owns_trim
        || !same_tuning(a.requested,b.requested) || !same_tuning(a.applied,b.applied) || !same_tuning(a.ceilings,b.ceilings)
        || a.settings.roll_torque_units_per_nm!=b.settings.roll_torque_units_per_nm || a.settings.auto_start!=b.settings.auto_start || a.settings.reference_airspeed!=b.settings.reference_airspeed
        || a.settings.pitch_force_units_per_lb!=b.settings.pitch_force_units_per_lb
        || a.settings.pitch_force_gain_divisor!=b.settings.pitch_force_gain_divisor || a.settings.pitch_move_back_gain!=b.settings.pitch_move_back_gain
        || a.solved!=b.solved || a.solution.strength!=b.solution.strength || a.solution.centre!=b.solution.centre || a.solution.bias!=b.solution.bias
        || a.roll_solved!=b.roll_solved || a.roll_solution.strength!=b.roll_solution.strength || a.roll_solution.centre!=b.roll_solution.centre;
}
struct Record {
    std::uint64_t sequence=0;
    monitor::Frame frame;
    std::array<char,240> message{}; // nonempty = event, otherwise sample
};
inline std::string encode(const Record& r,double start,std::int64_t epoch,double previous_sample) {
    const auto& f=r.frame;
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12);
    out<<"{\"type\":\""<<(r.message[0]?"event":"sample")<<"\",\"seq\":"<<r.sequence
       <<",\"t\":"<<f.time-start<<",\"utc_ms\":"<<epoch+std::int64_t((f.time-start)*1000);
    if (r.message[0]) {out<<",\"message\":"<<quote(r.message.data())<<"}\n";return out.str();}
    out<<",\"mode\":"<<quote(mode(f.mode))<<",\"fresh\":"<<f.fresh<<",\"age_ms\":";number(out,f.age_ms);
    out<<",\"cycles\":"<<f.cycles<<",\"sampling_gap_s\":"<<(previous_sample>=0 && f.time-previous_sample>.25?f.time-previous_sample:0)
       <<",\"motors\":["<<f.motors[0]<<','<<f.motors[1]<<"],\"encoder\":["<<f.encoder[0]<<','<<f.encoder[1]
       <<"],\"status\":["<<f.status[0]<<','<<f.status[1]<<"],\"position_valid\":"<<f.position_valid<<",\"position\":[";
    number(out,f.position[0]);out<<',';number(out,f.position[1]);out<<"],\"owns_axes\":"<<f.owns_axes<<",\"owns_trim\":"<<f.owns_trim;
    out<<",\"sim_valid\":"<<f.sim_valid<<",\"ias_knots\":";number(out,f.simulator.airspeed);
    out<<",\"aircraft_trim\":";number(out,f.simulator.trim);
    if (f.sim_valid) {
        out<<",\"physics_time_s\":";number(out,f.simulator.physics_time);
        out<<",\"profile\":"<<quote(stock_loading_profile)<<",\"tail_pa\":";number(out,f.simulator.flow.tail);
        out<<",\"left_aileron_pa\":";number(out,f.simulator.flow.left);
        out<<",\"right_aileron_pa\":";number(out,f.simulator.flow.right);
        out<<",\"roll_pa\":";number(out,f.simulator.flow.roll);
        out<<",\"native_pitch_lb\":";number(out,f.simulator.native_pitch_lb);
        out<<",\"native_roll_lb\":";number(out,f.simulator.native_roll_lb);
        out<<",\"load_factor\":";number(out,f.simulator.load_factor);
        if (f.preview.roll) {
            // The aileron model's inputs, per side as the model read them.
            const auto side=[&](const char* name,const AileronSide& a) {
                out<<",\""<<name<<"\":{\"pa\":";number(out,a.pa);out<<",\"alpha_deg\":";number(out,a.alpha_deg);
                out<<",\"stalled\":";number(out,a.stalled);out<<'}';
            };
            side("left_aileron",f.preview.roll->inputs.left);side("right_aileron",f.preview.roll->inputs.right);
            out<<",\"aileron_source\":"<<quote(f.preview.aileron_source==AileronSource::surface?"panels":"airspeed");
        }
        // The tail (decision 17): X-Plane's angle and the tail-plane's stalled
        // share over the profile's tail sections as read, raw; then, at the
        // yoke where its position is known, the angle the model takes, the
        // tail-plane's stall and the elevator's own (alpha + tau*d) kept
        // apart, and the larger, which the hinge moment uses. Step 0 reads
        // from these whether X-Plane's angle includes the deflection.
        if (f.simulator.tail) {
            const auto& t=*f.simulator.tail;
            out<<",\"tail\":{\"pa\":";number(out,t.pa);out<<",\"alpha_deg\":";number(out,t.alpha_deg);
            out<<",\"tail_stalled_xp\":";number(out,t.stalled);out<<'}';
        }
        if (f.preview.pitch && f.position_valid) {
            const auto s=elevator_state(f.preview.pitch->figures,f.preview.pitch->inputs);
            const auto t=tail_flow(s,elevator_rad(s,drive_counts_from_encoder(f.encoder[1])));
            out<<",\"tail_model\":{\"alpha_deg\":";number(out,t.alpha/radians_per_degree);
            out<<",\"tail_stalled_xp\":";number(out,t.tail_stalled_xp);out<<",\"elevator_stalled\":";number(out,t.elevator_stalled);
            out<<",\"hinge_stalled\":";number(out,t.hinge_stalled);out<<",\"reversed\":"<<t.reversed<<'}';
        }
        out<<",\"commissioning_preview\":";tuning(out,f.preview.tuning);
        out<<",\"pitch_error_ratio\":";number(out,f.preview.pitch_error);
        out<<",\"roll_error_ratio\":";number(out,f.preview.roll_error);
    }
    out<<",\"requested\":";tuning(out,f.requested);out<<",\"readback\":";tuning(out,f.applied);
    out<<",\"readback_valid\":"<<f.tuning<<",\"ceilings\":";tuning(out,f.ceilings);
    if (f.solved) {
        // The pitch solver's last update: the model at the yoke and what the drive was told.
        const auto& p=f.solution;
        out<<",\"pitch_model\":{\"rest_counts\":";number(out,p.rest_counts);out<<",\"force_lb\":";number(out,p.force_lb);
        out<<",\"drive_force_lb\":";number(out,p.drive_force_lb);out<<",\"strength\":"<<p.strength<<",\"centre\":"<<p.centre<<",\"bias\":"<<p.bias
           <<",\"saturated\":"<<p.saturated<<",\"tail_stalled_xp\":";number(out,p.tail_stalled_xp);
        out<<",\"elevator_stalled\":";number(out,p.elevator_stalled);out<<",\"hinge_stalled\":";number(out,p.hinge_stalled);
        out<<",\"tail_reversed\":"<<p.tail_reversed<<'}';
    }
    if (f.roll_solved) {
        // The roll solver's last update, the same way, N·m at the wheel.
        const auto& roll=f.roll_solution;
        out<<",\"roll_model\":{\"rest_counts\":";number(out,roll.rest_counts);out<<",\"wheel_counts\":";number(out,roll.x);
        out<<",\"model_nm\":";number(out,roll.model_nm);out<<",\"drive_nm\":";number(out,roll.drive_nm);
        out<<",\"strength\":"<<roll.strength<<",\"centre\":"<<roll.centre<<",\"saturated\":"<<roll.saturated<<",\"floored\":"<<roll.floored<<'}';
    }
    out<<",\"bias_limited\":"<<f.limited<<",\"bias_valid\":"<<f.bias_verified<<",\"bias_counts\":["<<f.bias[0]<<','<<f.bias[1]
       <<"],\"config\":{\"auto_start\":"<<f.settings.auto_start<<",\"reference_airspeed_knots\":"<<f.settings.reference_airspeed
       <<",\"roll_torque_units_per_nm\":"<<f.settings.roll_torque_units_per_nm<<",\"pitch_force_units_per_lb\":"<<f.settings.pitch_force_units_per_lb
       <<",\"pitch_force_gain_divisor\":"<<f.settings.pitch_force_gain_divisor<<",\"pitch_move_back_gain\":"<<f.settings.pitch_move_back_gain<<"}}\n";
    return out.str();
}
struct Options {std::uintmax_t segment_bytes=16*1024*1024;std::size_t keep_segments=16;};
// Only recorder-owned files are eligible for retention. Foreign files untouched.
inline bool owned(const std::filesystem::path& path) {
    const auto n=path.filename().string();
    if (n.size()<12 || n.substr(0,4)!="ffb-" || path.extension()!=".jsonl") return false;
    return std::all_of(n.begin()+4,n.end()-6,[](char c){return (c>='0' && c<='9') || c=='-';});
}
enum class State {off,starting,recording,failed};
inline std::string status_label(State state,std::uint64_t dropped) {
    switch (state) {
    case State::recording:return dropped?"Debug log: "+std::to_string(dropped)+" dropped":"Debug log: recording";
    case State::starting:return "Debug log: starting";
    case State::failed:return "Debug log: unavailable";
    default:return "Debug log: off";
    }
}
class Recorder {
    Queue<Record,2048> queue_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<State> state_{State::off};
    std::atomic<std::uint64_t> dropped_{0},written_{0};
    std::array<char,240> error_{}; // published before the release-store of failed
    std::uint64_t sequence_=0;
    monitor::Frame last_{};
    bool sampled_=false;
    void enqueue(Record& r) noexcept {r.sequence=++sequence_;if (!queue_.push(r)) ++dropped_;}
    void work(const std::filesystem::path& root,Options options,double start,std::int64_t epoch) noexcept {
        try {
            if (!options.keep_segments || options.segment_bytes<1024) throw std::invalid_argument("Invalid recorder retention limits");
            std::filesystem::create_directories(root);
            const auto id=std::to_string(epoch)+"-"+std::to_string(std::uint64_t(start*1000000));
            std::ofstream file;std::uintmax_t bytes=0;unsigned segment=0;
            auto open=[&] {
                std::ostringstream suffix;suffix<<std::setw(6)<<std::setfill('0')<<++segment;
                const auto path=root/("ffb-"+id+"-"+suffix.str()+".jsonl");
                if (std::filesystem::exists(path)) throw std::runtime_error("Recorder path collision");
                file.open(path,std::ios::binary|std::ios::out);file.exceptions(std::ios::badbit|std::ios::failbit);
                const auto header="{\"type\":\"session\",\"schema\":1,\"session\":"+quote(id)+",\"segment\":"+std::to_string(segment)
                    +",\"build\":"+quote(BRUNNER_BUILD_ID)+",\"utc_start_ms\":"+std::to_string(epoch)
                    +",\"sample_hz\":20,\"axis_order\":[\"roll\",\"pitch\"],\"mapping\":"+quote(stock_loading_profile)+","
                    "\"readback_semantics\":\"last verified settings, not a measurement of hand force\",\"hand_force_measured\":false}\n";
                file<<header;file.flush();bytes=header.size();
                std::vector<std::filesystem::path> old;
                for (const auto& e:std::filesystem::directory_iterator(root)) if (e.is_regular_file() && owned(e.path())) old.push_back(e.path());
                std::sort(old.begin(),old.end());auto count=old.size();
                for (const auto& p:old) if (count>options.keep_segments && p!=path) {std::filesystem::remove(p);--count;}
            };
            open();state_=State::recording;
            std::uint64_t expected=1;double previous_sample=-1,last_flush=seconds();
            for (;;) {
                Record r;
                if (!queue_.pop(r)) {
                    if (!running_.load()) break;
                    if (seconds()-last_flush>=.5) {file.flush();last_flush=seconds();}
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;
                }
                if (bytes>=options.segment_bytes) {file<<"{\"type\":\"segment_end\"}\n";file.close();open();}
                if (r.sequence!=expected) {
                    const auto gap="{\"type\":\"queue_gap\",\"first_seq\":"+std::to_string(expected)+",\"last_seq\":"+std::to_string(r.sequence-1)+"}\n";
                    file<<gap;bytes+=gap.size();
                }
                expected=r.sequence+1;
                const auto line=encode(r,start,epoch,previous_sample);file<<line;bytes+=line.size();++written_;
                if (!r.message[0]) previous_sample=r.frame.time;
                if (seconds()-last_flush>=.5) {file.flush();last_flush=seconds();}
            }
            if (expected<=sequence_) file<<"{\"type\":\"queue_gap\",\"first_seq\":"<<expected<<",\"last_seq\":"<<sequence_<<"}\n";
            file<<"{\"type\":\"session_end\",\"dropped\":"<<dropped_.load()<<",\"records\":"<<written_.load()<<"}\n";file.flush();file.close();state_=State::off;
        } catch (const std::exception& e) {
            const std::string message=e.what();std::copy_n(message.data(),std::min(message.size(),error_.size()-1),error_.data());
            state_=State::failed;
        } catch (...) {state_=State::failed;}
    }
public:
    ~Recorder() {stop();}
    State state() const {return state_.load();}
    std::uint64_t dropped() const {return dropped_.load();}
    std::uint64_t written() const {return written_.load();}
    const char* error() const {return state_==State::failed?error_.data():"";}
    void start(const std::filesystem::path& root,Options options={}) {
        stop();Record unused;while (queue_.pop(unused)) {}
        sampled_=false;sequence_=0;dropped_=0;written_=0;error_.fill(0);running_=true;state_=State::starting;
        try {worker_=std::thread([this,root,options,start=seconds(),epoch=utc_ms()] {work(root,options,start,epoch);});}
        catch (...) {running_=false;state_=State::failed;} // diagnostics failure must not prevent flight input
    }
    void observe(const monitor::Frame& f) noexcept {
        if (!running_ || state_==State::failed || !std::isfinite(f.time)) return;
        if (sampled_ && f.time>=last_.time && f.time-last_.time<.05 && !changed(f,last_)) return;
        Record r;r.frame=f;enqueue(r);last_=f;sampled_=true;
    }
    void event(const std::string& text) noexcept {
        if (!running_ || state_==State::failed || text.empty()) return;
        Record r;r.frame.time=seconds();std::copy_n(text.data(),std::min(text.size(),r.message.size()-1),r.message.data());enqueue(r);
    }
    void stop() {running_=false;if (worker_.joinable()) worker_.join();}
};
}
