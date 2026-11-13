// SOURCE OF TRUTH for the plugin's menu: "Yoke (Brunner)" in X-Plane's Plugins
// menu, with its Developer submenu. This header is the interface; menu.cpp is the
// X-Plane implementation. The menu shows what the plugin gives it and decides
// nothing: what a click does is the plugin's (plugin.cpp, menu_action).
//
// Item order is part of the interface (users know where things are): status,
// Stop yoke motors, Re-centre, Force feel, Show yoke status, Keep yoke feel, Feel
// tuning, Developer > (monitor, HUD, move HUD, roll, pitch, reference, light
// spring, flight, stop, reconnect, survey, Allow unverified yoke, Reset yoke). New items go at
// the end of their menu so no position moves.
//
// Thread: every call, and every callback, runs on X-Plane's thread.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace brunner::ui {
// What a click asks for. The values are the menu items' refcons.
enum class MenuItem:std::intptr_t {
    reconnect=1,reference=2,stop=3,loading=4,flight=5,monitor=6,hud=7,pin=8,move=9,recenter=10,
    force_feel=11,status_line=12,stop_motors=13,survey=14,unverified=15,reset_yoke=16
};

// What the menu shows. The plugin builds one per update.
struct MenuModel {
    std::string status;                       // the first line: the yoke's status text
    bool retry_offered=false;                 // the re-centre item reads "Retry"
    bool force_feel=false,status_line=false,pinned=false,unverified=false; // check marks
    std::string roll_line,pitch_line;         // the Developer menu's two live axis lines
};

class YokeMenu {
 public:
    struct Callbacks {
        // A click. If it throws, the menu catches it and passes the message to log;
        // an exception never reaches X-Plane.
        std::function<void(MenuItem)> clicked;
        std::function<void(const std::string&)> log;
    };
    // Builds both menus. feel_text is the Feel tuning line's first text. Errors:
    // none thrown; if X-Plane refuses a menu, later calls do nothing.
    void create(Callbacks callbacks,const std::string& feel_text);
    // Cheap and idempotent: sets names and check marks from the model. Does nothing
    // before create() or after destroy().
    void refresh(const MenuModel& model) noexcept;
    void set_status(const std::string& status) noexcept;       // the first line alone
    void set_feel_text(const std::string& text) noexcept;      // the Feel tuning line
    // Removes both menus and the plugin's entry. Safe to call twice.
    void destroy() noexcept;

 private:
    friend struct MenuCallbacks;
    void* menu_=nullptr;           // XPLMMenuID
    void* developer_menu_=nullptr; // XPLMMenuID
    int parent_item_=-1;
    Callbacks callbacks_;
};
}
