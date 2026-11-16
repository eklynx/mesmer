// Implementation of buttons/service.hpp (read that header for the contract).
#include "buttons/service.hpp"

namespace buttons {
ButtonService::ButtonService(CommandRunner& runner, HidSource::Opener hid_opener, VirtualJoystickFactory virtual_joystick)
    : runner_(runner), virtual_joystick_(std::move(virtual_joystick)), hid_(std::move(hid_opener)) {
    settings_ = requested_;
    auto direct = std::make_unique<DirectAction>(runner_);
    direct_ = direct.get();
    action_ = std::move(direct);
    controls_ = std::make_shared<const ControlMap>();
    hub_.configure(controls_, settings_.read);
}

ButtonService::~ButtonService() { stop(); }

void ButtonService::set_commands(const ButtonCommands::Entries& entries) { entries_ = entries; }

void ButtonService::set_yoke(const std::string& model, const ControlMap::Aliases& aliases, const ButtonSettings& methods, const Log& log) {
    const bool known = !model.empty();   // an empty model is the placeholder before the yoke is known: its settings are not loaded yet, so any note would be wrong
    requested_ = methods;
    const auto quiet = [&](const std::string& line) { if (known) log(line); };
    // The action in force: VJoy only where there is a virtual joystick.
    fallback_note_.clear();
    ActionMethod action = methods.action;
    std::unique_ptr<ButtonAction> next;
    if (action == ActionMethod::vjoy && settings_.action != ActionMethod::vjoy) {
        auto joystick = virtual_joystick_ ? virtual_joystick_() : nullptr;
        if (joystick) next = std::make_unique<VJoyAction>(std::move(joystick));
        else action = ActionMethod::direct;
    }
    if (methods.action == ActionMethod::vjoy && action == ActionMethod::direct)
        fallback_note_ = "Yoke buttons: " + model + " asks for the VJoy action, which needs a virtual joystick driver this operating system does not have yet; using Direct";
    if (action == ActionMethod::vjoy && settings_.action == ActionMethod::vjoy) next.reset();   // already in force
    else if (action != settings_.action || next) {
        if (action_) { action_->close(); action_->unload(); }
        direct_ = nullptr;
        if (action == ActionMethod::vjoy) action_ = std::move(next);
        else { auto direct = std::make_unique<DirectAction>(runner_); direct_ = direct.get(); action_ = std::move(direct); }
    }
    settings_ = {methods.read, action};
    try {
        controls_ = std::make_shared<const ControlMap>(aliases);
    } catch (const ControlError& e) {
        quiet("Yoke buttons: the controls of " + model + " are not usable (" + e.what() + "); using the plain button names");
        controls_ = std::make_shared<const ControlMap>();
    }
    std::vector<std::string> notes;
    auto commands = std::make_shared<const ButtonCommands>(ButtonCommands::resolve(entries_, *controls_, settings_.read, notes));
    for (const auto& note : notes) quiet(note);
    hub_.configure(controls_, settings_.read);
    if (direct_) direct_->set_commands(controls_, commands);   // ends what the old commands had held
    if (known) {
        log(std::string("Yoke buttons: ") + model + ": read method " + read_method_name(settings_.read) + ", action " + action_method_name(settings_.action));
        if (!fallback_note_.empty()) log(fallback_note_);
        log("Yoke buttons: " + model + " has " + std::to_string(controls_->size()) + " controls; " + std::to_string(commands->size()) + " with commands");
        if (!in_use()) log("Yoke buttons: no button_commands configured and no virtual joystick; the yoke's buttons are not read");
    }
    apply_sources(log);
}

// The HID thread runs exactly when the read method is HID, something uses the buttons, and the service is started.
void ButtonService::apply_sources(const Log&) {
    if (started_ && in_use() && settings_.read == ReadMethod::hid) hid_.start(hub_);
    else hid_.stop();
}

void ButtonService::start(const Log& log) {
    started_ = true;
    apply_sources(log);
}

void ButtonService::stop() {
    hid_.stop();
    hub_.release_all();
    if (action_) action_->close();
    started_ = false;
}

void ButtonService::unload() {
    if (action_) action_->unload();
}

void ButtonService::update(Clock::time_point now, const brunner::Snapshot& s, const Log& log) {
    if (!started_ || !in_use()) return;
    action_->update(hub_, now, log);
    if (settings_.read == ReadMethod::hid) {
        const auto status = hid_.status();
        if (status != last_hid_status_) { last_hid_status_ = status; if (!status.empty()) log(status); }
        if (now - drops_logged_ >= std::chrono::seconds(10)) {
            drops_logged_ = now;
            const auto drops = hid_.take_drops();
            if (!drops.empty()) log(drops);
        }
    } else if (s.button_refusal != last_refusal_) {
        if (!s.button_refusal.empty()) log("Yoke buttons not read: " + s.button_refusal + "; trying again every 5 seconds");
        else if (s.buttons) log("Yoke buttons read again");
        last_refusal_ = s.button_refusal;
    }
}
}
