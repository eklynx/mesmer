// X-Plane implementation of StartupNotice. Contract: ui/view/startup_notice.hpp.
#include "ui/view/startup_notice.hpp"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include <limits>
#include <stdexcept>

namespace brunner::ui {
struct StartupNoticeCallbacks {
    static StartupNotice& from(void* refcon) {return *static_cast<StartupNotice*>(refcon);}
    static void draw(XPLMWindowID window,void* refcon) {
        auto& self=from(refcon);
        int l,t,r,b;XPLMGetWindowGeometry(window,&l,&t,&r,&b);
        const TextWidth measure=[](const std::string& text) {
            return static_cast<double>(XPLMMeasureString(xplmFont_Proportional,text.c_str(),static_cast<int>(text.size())));
        };
        const auto frame=self.source_.frame?self.source_.frame(r-l-24,measure):NoticeFrame{};
        if (!frame.visible) return;
        XPLMDrawTranslucentDarkBox(l,t,r,b);
        float color[]{0.94f,0.94f,0.94f};
        for (std::size_t i=0;i<frame.lines.size();++i)
            XPLMDrawString(color,l+12,t-22-21*static_cast<int>(i),frame.lines[i].c_str(),nullptr,xplmFont_Proportional);
        if (self.draws_<std::numeric_limits<int>::max()) ++self.draws_;
    }
    static int mouse(XPLMWindowID,int,int,XPLMMouseStatus status,void* refcon) {
        auto& self=from(refcon);
        if (status==xplm_MouseDown && self.source_.clicked) self.source_.clicked();
        return 1;
    }
    static int wheel(XPLMWindowID,int,int,int,int,void*) {return 1;}
    static XPLMCursorStatus cursor(XPLMWindowID,int,int,void*) {return xplm_CursorDefault;}
    static void key(XPLMWindowID,char,XPLMKeyFlags,char,void*,int) {}
};

void StartupNotice::create(Source source) {
    if (window_) return;
    source_=std::move(source);
    int l,t,r,b;XPLMGetScreenBoundsGlobal(&l,&t,&r,&b);
    XPLMCreateWindow_t parameters{};parameters.structSize=sizeof(parameters);
    parameters.left=l+24;parameters.right=l+544;parameters.top=t-60;parameters.bottom=t-120;
    parameters.drawWindowFunc=StartupNoticeCallbacks::draw;parameters.handleMouseClickFunc=StartupNoticeCallbacks::mouse;
    parameters.handleRightClickFunc=StartupNoticeCallbacks::mouse;parameters.handleMouseWheelFunc=StartupNoticeCallbacks::wheel;
    parameters.handleCursorFunc=StartupNoticeCallbacks::cursor;parameters.handleKeyFunc=StartupNoticeCallbacks::key;
    parameters.refcon=this;
    parameters.decorateAsFloatingWindow=xplm_WindowDecorationNone;parameters.layer=xplm_WindowLayerFlightOverlay;
    window_=XPLMCreateWindowEx(&parameters);
    if (!window_) throw std::runtime_error("Could not create yoke status indicator");
    int char_width=0,char_height=0;XPLMGetFontDimensions(xplmFont_Proportional,&char_width,&char_height,nullptr);
    if (source_.log) source_.log("Corner notices: proportional font about "+std::to_string(char_width)+" x "+std::to_string(char_height)+" px, two lines of "
        +std::to_string(parameters.right-parameters.left-24)+" px, measured per string");
}
void StartupNotice::destroy() noexcept {
    if (window_) {XPLMDestroyWindow(static_cast<XPLMWindowID>(window_));window_=nullptr;}
}
void StartupNotice::set_visible(bool visible) noexcept {
    if (window_) XPLMSetWindowIsVisible(static_cast<XPLMWindowID>(window_),visible);
}
}
