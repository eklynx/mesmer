// SOURCE OF TRUTH for the one object the plugin talks to about the yoke's buttons. Specification: docs/dev/button-commands.md.
// Portable; read this header, not service.cpp.
//
// The service owns the whole chain: a ButtonHub, the read method's source (the HID thread; the CanBus read lives in the reader,
// which the plugin attaches to sink()), and the action (Direct: X-Plane commands through a CommandRunner; VJoy: a virtual
// joystick). The two methods are the connected yoke's (its preset, buttons/methods.hpp), so they are chosen when the model is known.
//
// Happy path, in the order the plugin calls it: construct (until a yoke is known the methods are CanBus + Direct); set_commands(entries)
// with the owner's button_commands; start() at XPluginEnable; set_yoke(model, aliases, methods, log) when the connected yoke's model
// becomes known or changes, which builds the controls from its button_aliases, switches to its read and action methods and resolves
// the commands through the controls (notes go to `log`; an empty model is the placeholder used before the yoke is known, which does
// all of it quietly); update(now, snapshot, log) every flight-loop frame on X-Plane's thread; stop() at XPluginDisable (nothing stays
// pressed or held); unload() at XPluginStop. wants_can() says whether the reader should read the registers (ask it again after
// set_yoke), and sink() is where it publishes. set_yoke(model, aliases, log) keeps the methods in force.
//
// Error cases (none thrown): an action method the operating system cannot do, VJoy without a virtual joystick, becomes Direct and
// `log` says so once per model. When neither a virtual joystick nor any button_commands would use the buttons, no source is started
// at all (no bus traffic, the joystick interface not opened) and the log says so once per model. A source that has trouble (no
// joystick interface, a refused register read) says so in `log` once per change; the yoke flies as before in every case.
#pragma once
#include "brunner/core.hpp"
#include "buttons/actions/direct_action.hpp"
#include "buttons/actions/vjoy_action.hpp"
#include "buttons/input/hid_source.hpp"
#include <functional>
#include <memory>
#include <string>

namespace buttons {
struct ButtonSettings {
    ReadMethod read = default_read_method;
    ActionMethod action = default_action_method;
    bool operator==(const ButtonSettings& o) const { return read == o.read && action == o.action; }
};

class ButtonService {
public:
    using VirtualJoystickFactory = std::function<std::unique_ptr<VirtualJoystick>()>;
    ButtonService(CommandRunner& runner, HidSource::Opener hid_opener, VirtualJoystickFactory virtual_joystick = make_virtual_joystick);
    ~ButtonService();
    ButtonSink& sink() { return hub_; }
    ReadMethod read_method() const { return settings_.read; }
    ActionMethod action_method() const { return settings_.action; }   // the one in force, after any fallback
    // The reader should read the register: the read method is CanBus and something uses the buttons.
    bool wants_can() const { return settings_.read == ReadMethod::can_bus && in_use(); }
    void set_commands(const ButtonCommands::Entries& entries);
    void set_yoke(const std::string& model, const ControlMap::Aliases& aliases, const ButtonSettings& methods, const Log& log);
    void set_yoke(const std::string& model, const ControlMap::Aliases& aliases, const Log& log) { set_yoke(model, aliases, requested_, log); }
    void start(const Log& log);
    void stop();
    void unload();
    void update(Clock::time_point now, const brunner::Snapshot& snapshot, const Log& log);
private:
    bool in_use() const { return settings_.action == ActionMethod::vjoy || !entries_.empty(); }
    void apply_sources(const Log& log);
    ButtonSettings requested_, settings_;   // what the yoke's model asked for; what is in force, after any fallback
    CommandRunner& runner_;
    VirtualJoystickFactory virtual_joystick_;
    std::string fallback_note_;
    ButtonHub hub_;
    HidSource hid_;
    ButtonCommands::Entries entries_;
    std::unique_ptr<ButtonAction> action_;
    DirectAction* direct_ = nullptr;   // action_ when it is the Direct one
    std::shared_ptr<const ControlMap> controls_;
    std::string last_hid_status_, last_refusal_;
    Clock::time_point drops_logged_{};
    bool started_ = false;
};
}
