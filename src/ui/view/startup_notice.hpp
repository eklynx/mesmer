// SOURCE OF TRUTH for the corner notice window: the small translucent box in the
// top-left of the sim that shows the yoke's status (centring, retry) and the feel
// page's latest notice. This header is the interface; startup_notice.cpp is the
// X-Plane implementation. The window draws what the plugin gives it and decides
// nothing: what to say, and when it is visible, is the plugin's (plugin.cpp).
//
// Thread: every call, and every callback, runs on X-Plane's thread (a draw or mouse
// callback, or the flight loop).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace brunner::ui {
// Width in pixels of a string in the window's font.
using TextWidth=std::function<double(const std::string&)>;

// What the window shows this frame: at most two lines, drawn top to bottom.
struct NoticeFrame {
    bool visible=false;
    std::vector<std::string> lines;
};

class StartupNotice {
 public:
    struct Source {
        // Happy path: the frame to draw. inner_width is the room for text in pixels,
        // measure the width of a string, so a line can be fitted. An invisible frame
        // draws nothing (and is not counted as a draw).
        std::function<NoticeFrame(int inner_width,const TextWidth& measure)> frame;
        // A click on the window. Called on mouse down; the plugin decides whether
        // it means anything (it is Retry while Retry is offered).
        std::function<void()> clicked;
        // A line for X-Plane's Log.txt.
        std::function<void(const std::string&)> log;
    };
    // Creates the window, hidden until set_visible(true). Errors: throws
    // std::runtime_error if X-Plane cannot create it. Calling it twice replaces
    // nothing: the second call is ignored while a window exists.
    void create(Source source);
    // Safe to call when there is no window.
    void destroy() noexcept;
    void set_visible(bool visible) noexcept;
    // How many frames have been drawn with something to show (saturating); the
    // plugin publishes it as a dataref so a test can see the notice appear.
    int draws() const noexcept {return draws_;}

 private:
    friend struct StartupNoticeCallbacks;
    void* window_=nullptr; // XPLMWindowID, kept opaque so this header needs no SDK
    Source source_;
    int draws_=0;
};
}
