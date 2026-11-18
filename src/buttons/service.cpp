// Implementation of buttons/service.hpp (read that header for the contract).
#include "buttons/service.hpp"

namespace buttons {
ButtonService::ButtonService(CommandRunner& runner, VirtualJoystickFactory virtual_joystick)
    : runner_(runner), virtual_joystick_(std::move(virtual_joystick)) {
    auto direct = std::make_unique<DirectAction>(runner_);
    direct_ = direct.get();
    action_ = std::move(direct);
    controls_ = std::make_shared<const ControlMap>();
    hub_.configure(controls_);
}

ButtonService::~ButtonService() { stop(); }

void ButtonService::set_commands(const ButtonCommands::Entries& entries) { entries_ = entries; }

// The owner's commands with the aircraft's laid over them, through the yoke's controls; the notes go to the log once the yoke is known.
std::shared_ptr<const ButtonCommands> ButtonService::resolve_commands(const Log& log) const {
    std::vector<std::string> notes;
    auto commands = std::make_shared<const ButtonCommands>(aircraft_entries_.empty() ? ButtonCommands::resolve(entries_, *controls_, notes)
                                                                                      : ButtonCommands::resolve(entries_, aircraft_entries_, aircraft_label_, *controls_, notes));
    if (!model_.empty()) for (const auto& note : notes) log(note);
    return commands;
}

void ButtonService::set_aircraft(const std::string& label, const ButtonCommands::Entries& entries, const Log& log) {
    aircraft_label_ = label;
    aircraft_entries_ = entries;
    auto commands = resolve_commands(log);
    if (direct_) direct_->set_commands(controls_, commands);   // ends what the old commands had held
    if (!model_.empty() && !aircraft_entries_.empty()) {
        if (action_in_force_ == ActionMethod::disabled) log("Yoke buttons: " + label + ": the buttons are disabled; its button_commands are ignored");
        else log("Yoke buttons: " + label + ": " + std::to_string(commands->size()) + " controls with commands");
    }
}

void ButtonService::set_yoke(const std::string& model, const ControlMap::Aliases& aliases, ActionMethod action, const Log& log) {
    const bool known = !model.empty();   // an empty model is the placeholder before the yoke is known: its settings are not loaded yet, so any note would be wrong
    requested_ = action;
    const auto say = [&](const std::string& line) { if (known) log(line); };
    // The action in force: VJoy only where there is a virtual joystick.
    fallback_note_.clear();
    ActionMethod in_force = action;
    std::unique_ptr<ButtonAction> next;
    if (action == ActionMethod::vjoy && action_in_force_ != ActionMethod::vjoy) {
        auto joystick = virtual_joystick_ ? virtual_joystick_() : nullptr;
        if (joystick) next = std::make_unique<VJoyAction>(std::move(joystick));
        else in_force = ActionMethod::direct;
    }
    if (action == ActionMethod::vjoy && in_force == ActionMethod::direct)
        fallback_note_ = "Yoke buttons: " + model + " asks for the VJoy action, which needs a virtual joystick driver this operating system does not have yet; using Direct";
    if (in_force == ActionMethod::vjoy && action_in_force_ == ActionMethod::vjoy) next.reset();   // already in force
    else if (in_force != action_in_force_ || next) {
        if (action_) { action_->close(); action_->unload(); }
        direct_ = nullptr;
        if (in_force == ActionMethod::vjoy) action_ = std::move(next);
        else if (in_force == ActionMethod::disabled) { action_.reset(); hub_.release_all(); }   // no action: nothing is pressed or held
        else { auto direct = std::make_unique<DirectAction>(runner_); direct_ = direct.get(); action_ = std::move(direct); }
    }
    action_in_force_ = in_force;
    try {
        controls_ = std::make_shared<const ControlMap>(aliases);
    } catch (const ControlError& e) {
        say("Yoke buttons: the controls of " + model + " are not usable (" + e.what() + "); using the plain button names");
        controls_ = std::make_shared<const ControlMap>();
    }
    model_ = model;
    auto commands = resolve_commands(log);
    hub_.configure(controls_);
    if (direct_) direct_->set_commands(controls_, commands);   // ends what the old commands had held
    if (known) {
        log(std::string("Yoke buttons: ") + model + ": action " + action_method_name(action_in_force_));
        if (!fallback_note_.empty()) log(fallback_note_);
        if (action_in_force_ == ActionMethod::disabled) {
            const bool any = !entries_.empty() || !aircraft_entries_.empty();
            log("Yoke buttons: " + model + ": the buttons are disabled; " + (any ? "button_commands are ignored" : "the yoke's buttons are not read"));
        } else {
            log("Yoke buttons: " + model + " has " + std::to_string(controls_->size()) + " controls; " + std::to_string(commands->size()) + " with commands");
            if (!in_use()) log("Yoke buttons: no button_commands configured and no virtual joystick; the yoke's buttons are not read");
        }
    }
}

void ButtonService::start(const Log&) { started_ = true; }

void ButtonService::stop() {
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
    if (s.button_refusal != last_refusal_) {
        if (!s.button_refusal.empty()) log("Yoke buttons not read: " + s.button_refusal + "; trying again every 5 seconds");
        else if (s.buttons) log("Yoke buttons read again");
        last_refusal_ = s.button_refusal;
    }
}
}
