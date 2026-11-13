#pragma once
#include "brunner/core.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace brunner {
// The yoke's buttons reach X-Plane as a joystick's (decision 9): Brunner's
// signed vJoy driver holds one device of 32 buttons, and the plugin presses
// the device's button n for the yoke's slot n. Windows only (the Mac has no vJoy):
// the plugin reaches all of it through YokeButtons (hid/yoke_buttons.hpp). The logic
// is plain C++, checked by hid/win/buttons_tests.cpp; the feeder is vjoy_win.hpp.

// The yoke's buttons in a sampling cycle: node 2's 0x60fd, read in every
// cycle of every loop, so a press shorter than one cycle can still fall
// between two reads. A read that fails any way but the link (the controller's
// refusal, or an answer that makes no sense) leaves the buttons unread, with
// why, and is tried again from 5 s later: the buttons never stop pitch, roll
// or force feel. The link failing, or the reader stopping, ends the cycle as
// for any read. Link is the reader's bridge (brunner/bridge.hpp), or a test's.
constexpr std::chrono::seconds button_retry_after{5};
template<class Link> void sample_buttons(Link& link,Snapshot& s,const std::atomic<bool>& stop,Clock::time_point now) {
    if (!s.button_refusal.empty() && now<s.button_retry) return;
    try {
        s.buttons=slots_1_to_16(link.read(2,Object::digital_inputs,stop));
        s.button_refusal.clear();
    } catch (const TransportError&) {throw;}
    catch (const OperationCancelled&) {throw;}
    catch (const std::exception& e) {
        s.buttons.reset();s.button_refusal=e.what();s.button_retry=now+button_retry_after;
    }
}
// The yoke's slots as one word, bit n-1 for slot n: vJoy's button n. Only
// slots 1-16 are read (core.hpp), so bits 16-31 are always 0; buttons not
// read show nothing pressed.
inline std::uint32_t button_word(const Snapshot& s) {return s.buttons.value_or(0);}
// What the joystick shows now: nothing known, so every button up, without a
// live link or with readings older than flight input's 250 ms. A reading
// stamped after now is fresh, not stale.
inline std::optional<std::uint32_t> live_buttons(const Snapshot& s,Clock::time_point now) {
    if (!s.connected || s.cycles==0) return std::nullopt;
    if (now>s.sampled && now-s.sampled>std::chrono::milliseconds(250)) return std::nullopt;
    return button_word(s);
}
// The slots a word holds down, for the log: "1, 17", or "none".
inline std::string pressed_slots(std::uint32_t word) {
    std::string text;
    for (unsigned bit=0;bit<32;++bit) if (word&(1u<<bit)) {
        if (!text.empty()) text+=", ";
        text+=std::to_string(bit+1);
    }
    return text.empty()?"none":text;
}
// What the plugin asks of the joystick's feeder: vJoy's calls on its device 1
// (hid/win/vjoy_win.hpp), or a test's.
struct VirtualJoystick {
    enum class Status {owned,available,busy,missing,unknown}; // vJoy's VjdStat, in its order
    virtual ~VirtualJoystick()=default;
    // Finds and loads the feeder: empty once loaded, else why not, for the log.
    virtual std::string load()=0;
    virtual bool enabled()=0;                       // vJoyEnabled
    virtual Status status()=0;                      // GetVJDStatus
    virtual bool acquire()=0;                       // AcquireVJD
    virtual void relinquish()=0;                    // RelinquishVJD
    virtual int buttons()=0;                        // GetVJDButtonNumber
    virtual bool set(unsigned button,bool down)=0;  // SetBtn, button 1 to 32
    virtual bool reset()=0;                         // ResetButtons: every button up
};
// Keeps the joystick's buttons where the yoke's are. It takes the device when
// it can and lets every button up as soon as it has it (X-Plane may have quit
// with one down), lets them all up whenever the yoke's buttons are unknown,
// sends only changes, and after a refusal tries again every five seconds.
// Each call returns its lines for the log, in order. A line is told once
// until the device has stayed taken for five seconds, so a device taken and
// then refusing, retry after retry, is told about once.
class ButtonPresser {
public:
    using Lines=std::vector<std::string>;
    static constexpr std::chrono::seconds retry_after{5};
    Lines update(VirtualJoystick& joystick,std::optional<std::uint32_t> word,Clock::time_point now) {
        Lines said;
        if (!ready_) {
            if (now<retry_) return said;
            open(joystick,now,said);
            if (!ready_) return said;
        } else if (!told_.empty() && now-opened_>=retry_after) told_.clear();
        const auto want=word.value_or(0)&mask_;
        if (want==sent_) return said;
        if (!word) {
            if (joystick.reset()) sent_=0;
            else lost(joystick,"vJoy refused to let the buttons up",now,said);
            return said;
        }
        for (unsigned bit=0;bit<32;++bit) {
            const auto flag=1u<<bit;
            if (!((want^sent_)&flag)) continue;
            if (!joystick.set(bit+1,(want&flag)!=0)) {lost(joystick,"vJoy refused a button",now,said);return said;}
            sent_^=flag;
        }
        return said;
    }
    // Every button up and the device given back: the plugin disabled or
    // stopping. The next update takes it again.
    void close(VirtualJoystick& joystick) {
        if (ready_) {joystick.reset();joystick.relinquish();}
        ready_=false;sent_=0;retry_={};told_.clear();
    }
    bool pressing() const {return ready_;}
    std::uint32_t pressed() const {return sent_;}
private:
    bool ready_=false;
    std::uint32_t sent_=0,mask_=0;
    Clock::time_point retry_{},opened_{};
    std::vector<std::string> told_;
    void tell(const std::string& line,Lines& said) {
        if (std::find(told_.begin(),told_.end(),line)!=told_.end()) return;
        told_.push_back(line);said.push_back(line);
    }
    void open(VirtualJoystick& joystick,Clock::time_point now,Lines& said) {
        const auto why=joystick.load();
        if (!why.empty()) {unavailable(why,now,said);return;}
        if (!joystick.enabled()) {unavailable("vJoy is installed but switched off",now,said);return;}
        switch (joystick.status()) {
        case VirtualJoystick::Status::owned:break;
        case VirtualJoystick::Status::available:
            if (!joystick.acquire()) {unavailable("vJoy device 1 could not be taken",now,said);return;}
            break;
        case VirtualJoystick::Status::busy:unavailable("vJoy device 1 is in use by another program",now,said);return;
        case VirtualJoystick::Status::missing:unavailable("vJoy device 1 is not set up",now,said);return;
        case VirtualJoystick::Status::unknown:unavailable("vJoy device 1 is in an unknown state",now,said);return;
        }
        const int count=std::min(joystick.buttons(),32);
        if (count<1) {joystick.relinquish();unavailable("vJoy device 1 has no buttons",now,said);return;}
        if (!joystick.reset()) {joystick.relinquish();unavailable("vJoy device 1 refused to let its buttons up",now,said);return;}
        mask_=count==32?0xffffffffu:(1u<<static_cast<unsigned>(count))-1u;
        sent_=0;ready_=true;opened_=now;
        tell("Yoke buttons: pressing vJoy device 1's "+std::to_string(count)+" buttons",said);
    }
    void unavailable(const std::string& why,Clock::time_point now,Lines& said) {
        ready_=false;sent_=0;retry_=now+retry_after;
        tell("Yoke buttons off: "+why+"; trying again every 5 seconds",said);
    }
    void lost(VirtualJoystick& joystick,const std::string& why,Clock::time_point now,Lines& said) {
        joystick.reset();joystick.relinquish();
        unavailable(why,now,said);
    }
};
// Each change of the yoke's buttons, for the log: at most one line per
// 100 ms and always ending on the latest. A press that came and went between
// two lines is named in the later one, so the lines count the presses the
// plugin saw however quick they were (one per button per line). Unknown
// buttons are no change (the link's own lines say why) and count as all up.
class ButtonLog {
public:
    static constexpr std::chrono::milliseconds spacing{100};
    std::string update(std::optional<std::uint32_t> word,Clock::time_point now) {
        if (!word) {logged_=0;last_=0;rose_=0;return {};}
        rose_|=*word&~last_;last_=*word;
        const auto tapped=rose_&~*word,again=rose_&*word&logged_;
        if ((*word==logged_ && !tapped && !again) || (said_ && now-*said_<spacing)) return {};
        logged_=*word;rose_=0;said_=now;
        auto line="Yoke buttons down: "+pressed_slots(*word);
        if (tapped) line+="; tapped since the last line: "+pressed_slots(tapped);
        if (again) line+="; let up and pressed again since the last line: "+pressed_slots(again);
        return line;
    }
private:
    // Down at the last line, down at the last call, and went down since the last line.
    std::uint32_t logged_=0,last_=0,rose_=0;
    std::optional<Clock::time_point> said_;
};
} // namespace brunner
