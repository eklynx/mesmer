// SOURCE OF TRUTH for the one object the plugin talks to about the yoke's buttons. Specification: docs/dev/button-commands.md.
// Portable; read this header, not service.cpp.
//
// The service owns the whole chain: a ButtonHub (where the reader, which the plugin attaches to sink(), delivers the yoke's buttons)
// and the action: Direct runs X-Plane commands through a CommandRunner, VJoy presses a virtual joystick. Which action is the
// connected yoke's (its preset, buttons/methods.hpp), so it is chosen when the model is known.
//
// Happy path, in the order the plugin calls it: construct (until a yoke is known the action is Direct); set_commands(entries) with
// the owner's button_commands; start() at XPluginEnable; set_aircraft(label, entries, log) when X-Plane loads or changes the aircraft, with
// that aircraft's own button_commands (empty entries when it has none), which override the owner's for it
// (buttons/commands.hpp) and are resolved again at once for the yoke now connected; set_yoke(model, aliases, action, log) when the connected yoke's model becomes
// known or changes, which builds the controls from its button_aliases, switches to its action and resolves the commands through the
// controls (notes go to `log`; an empty model is the placeholder used before the yoke is known, which does all of it quietly);
// update(now, snapshot, log) every flight-loop frame on X-Plane's thread; stop() at XPluginDisable (nothing stays pressed or held);
// unload() at XPluginStop. wants_buttons() says whether the reader should read the yoke's button registers (ask it again after
// set_yoke), and sink() is where it publishes. set_yoke(model, aliases, log) keeps the action in force.
//
// Error cases (none thrown): an action the operating system cannot do, VJoy without a virtual joystick, becomes Direct and `log`
// says so once per model. When neither a virtual joystick nor any button_commands would use the buttons, nothing is read (no bus
// traffic) and the log says so once per model. A refused register read is said in `log` once per change; the yoke flies as before
// in every case.
#pragma once
#include "brunner/core.hpp"
#include "buttons/actions/direct_action.hpp"
#include "buttons/actions/vjoy_action.hpp"
#include <functional>
#include <memory>
#include <string>

namespace buttons {
class ButtonService {
public:
    using VirtualJoystickFactory = std::function<std::unique_ptr<VirtualJoystick>()>;
    explicit ButtonService(CommandRunner& runner, VirtualJoystickFactory virtual_joystick = make_virtual_joystick);
    ~ButtonService();
    ButtonSink& sink() { return hub_; }
    ActionMethod action_method() const { return action_in_force_; }   // the one in force, after any fallback
    // The reader should read the yoke's button registers: something uses the buttons.
    bool wants_buttons() const { return in_use(); }
    void set_commands(const ButtonCommands::Entries& entries);
    void set_aircraft(const std::string& label, const ButtonCommands::Entries& entries, const Log& log);
    void set_yoke(const std::string& model, const ControlMap::Aliases& aliases, ActionMethod action, const Log& log);
    void set_yoke(const std::string& model, const ControlMap::Aliases& aliases, const Log& log) { set_yoke(model, aliases, requested_, log); }
    void start(const Log& log);
    void stop();
    void unload();
    void update(Clock::time_point now, const brunner::Snapshot& snapshot, const Log& log);
private:
    bool in_use() const { return action_in_force_ == ActionMethod::vjoy || !entries_.empty() || !aircraft_entries_.empty(); }
    std::shared_ptr<const ButtonCommands> resolve_commands(const Log& log) const;
    ActionMethod requested_ = default_action_method, action_in_force_ = default_action_method;   // what the yoke's model asked for; what is in force
    CommandRunner& runner_;
    VirtualJoystickFactory virtual_joystick_;
    std::string fallback_note_;
    ButtonHub hub_;
    ButtonCommands::Entries entries_, aircraft_entries_;
    std::string aircraft_label_, model_;   // the aircraft the entries are for ("aircraft C172"); the yoke model once known
    std::unique_ptr<ButtonAction> action_;
    DirectAction* direct_ = nullptr;   // action_ when it is the Direct one
    std::shared_ptr<const ControlMap> controls_;
    std::string last_refusal_;
    bool started_ = false;
};
}
