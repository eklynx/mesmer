// X-Plane implementation of YokeMenu. Contract: ui/menu/menu.hpp.
#include "ui/menu/menu.hpp"
#include "XPLMMenus.h"
#include <exception>

namespace brunner::ui {
namespace {
// Positions in the main menu (status first) and the Developer menu; see the order in menu.hpp.
constexpr int menu_status=0,menu_recenter=2,menu_force_feel=3,menu_status_line=4,menu_keep_feel=5,menu_feel_tuning=6;
constexpr int developer_roll=3,developer_pitch=4,developer_unverified=11;
XPLMMenuID id(void* menu) {return static_cast<XPLMMenuID>(menu);}
void* refcon(MenuItem item) {return reinterpret_cast<void*>(static_cast<std::intptr_t>(item));}
}

struct MenuCallbacks {
    static void click(void* menu_ref,void* item) {
        auto& self=*static_cast<YokeMenu*>(menu_ref);
        try {
            if (self.callbacks_.clicked) self.callbacks_.clicked(static_cast<MenuItem>(reinterpret_cast<std::intptr_t>(item)));
        } catch (const std::exception& e) {
            if (self.callbacks_.log) self.callbacks_.log(e.what());
        }
    }
};

void YokeMenu::create(Callbacks callbacks,const std::string& feel_text) {
    callbacks_=std::move(callbacks);
    parent_item_=XPLMAppendMenuItem(XPLMFindPluginsMenu(),"Yoke (Brunner)",nullptr,0);
    const auto menu=XPLMCreateMenu("Yoke (Brunner)",XPLMFindPluginsMenu(),parent_item_,MenuCallbacks::click,this);
    menu_=menu;
    XPLMAppendMenuItem(menu,"Yoke plugin is disabled",nullptr,0); // menu_status
    XPLMAppendMenuItem(menu,"Stop yoke motors",refcon(MenuItem::stop_motors),0);
    XPLMAppendMenuItem(menu,"Re-centre yoke (hands off)",refcon(MenuItem::recenter),0); // menu_recenter
    XPLMAppendMenuItem(menu,"Force feel",refcon(MenuItem::force_feel),0); // menu_force_feel
    XPLMAppendMenuItem(menu,"Show yoke status on screen",refcon(MenuItem::status_line),0); // menu_status_line
    XPLMAppendMenuItem(menu,"Keep yoke feel on screen",refcon(MenuItem::pin),0); // menu_keep_feel
    XPLMAppendMenuItem(menu,feel_text.c_str(),nullptr,0); // menu_feel_tuning
    const int developer_item=XPLMAppendMenuItem(menu,"Developer",nullptr,0);
    const auto developer=XPLMCreateMenu("Developer",menu,developer_item,MenuCallbacks::click,this);
    developer_menu_=developer;
    XPLMAppendMenuItem(developer,"Force monitor (read-only)",refcon(MenuItem::monitor),0);
    XPLMAppendMenuItem(developer,"Show / hide flight HUD",refcon(MenuItem::hud),0);
    XPLMAppendMenuItem(developer,"Move HUD / finish moving",refcon(MenuItem::move),0);
    XPLMAppendMenuItem(developer,"Roll: unavailable",nullptr,0); // developer_roll
    XPLMAppendMenuItem(developer,"Pitch: unavailable",nullptr,0); // developer_pitch
    XPLMAppendMenuItem(developer,"Supervised reference and stop (moves yoke)",refcon(MenuItem::reference),0);
    XPLMAppendMenuItem(developer,"Supervised light spring test (ten minutes)",refcon(MenuItem::loading),0);
    XPLMAppendMenuItem(developer,"Start flight loading (force feel on)",refcon(MenuItem::flight),0);
    XPLMAppendMenuItem(developer,"Stop test and disconnect",refcon(MenuItem::stop),0);
    XPLMAppendMenuItem(developer,"Reconnect reader",refcon(MenuItem::reconnect),0);
    XPLMAppendMenuItem(developer,"Survey yoke registers (read-only firmware)",refcon(MenuItem::survey),0);
    XPLMAppendMenuItem(developer,"Allow unverified yoke models (writes to untested hardware)",refcon(MenuItem::unverified),0); // developer_unverified
    XPLMAppendMenuItem(developer,"Reset yoke: motors off, the model's resting settings",refcon(MenuItem::reset_yoke),0);
}

void YokeMenu::refresh(const MenuModel& m) noexcept {
    if (!menu_ || !developer_menu_) return;
    const auto check=[](bool on) {return on?xplm_Menu_Checked:xplm_Menu_Unchecked;};
    XPLMSetMenuItemName(id(menu_),menu_status,m.status.c_str(),0);
    XPLMSetMenuItemName(id(menu_),menu_recenter,m.retry_offered?"Retry":"Re-centre yoke (hands off)",0);
    XPLMCheckMenuItem(id(menu_),menu_force_feel,check(m.force_feel));
    XPLMCheckMenuItem(id(menu_),menu_status_line,check(m.status_line));
    XPLMCheckMenuItem(id(menu_),menu_keep_feel,check(m.pinned));
    XPLMCheckMenuItem(id(developer_menu_),developer_unverified,check(m.unverified));
    XPLMSetMenuItemName(id(developer_menu_),developer_roll,m.roll_line.c_str(),0);
    XPLMSetMenuItemName(id(developer_menu_),developer_pitch,m.pitch_line.c_str(),0);
}
void YokeMenu::set_status(const std::string& status) noexcept {
    if (menu_) XPLMSetMenuItemName(id(menu_),menu_status,status.c_str(),0);
}
void YokeMenu::set_feel_text(const std::string& text) noexcept {
    if (menu_) XPLMSetMenuItemName(id(menu_),menu_feel_tuning,text.c_str(),0);
}
void YokeMenu::destroy() noexcept {
    if (developer_menu_) XPLMDestroyMenu(id(developer_menu_));
    developer_menu_=nullptr;
    if (menu_) XPLMDestroyMenu(id(menu_));
    menu_=nullptr;
    if (parent_item_>=0) XPLMRemoveMenuItem(XPLMFindPluginsMenu(),parent_item_);
    parent_item_=-1;
}
}
