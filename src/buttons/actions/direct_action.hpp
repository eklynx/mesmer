// SOURCE OF TRUTH for the Direct action: the yoke's controls run X-Plane commands. Specification:
// docs/dev/button-commands.md. Portable; X-Plane itself is behind CommandRunner, which the plugin implements with the SDK
// (XPLMFindCommand, XPLMCommandOnce, XPLMCommandBegin, XPLMCommandEnd) and tests with a recorder.
//
// Happy path: set_commands(map, commands) gives the controls and what button_commands says for them (buttons/commands.hpp).
// Each update() takes the hub's edges and, for every edge, runs the control's press or release commands once, in the order
// given, then does the hold work for the whole batch: every end before any begin, a command a control shares with the next one
// not ended and begun again (buttons/hold.hpp). close() ends everything still begun. set_commands() ends it first too, since a
// held command belongs to the commands it came from.
//
// Error cases (none thrown): a command X-Plane does not know is the runner's to say (once) and is skipped; the rest still run.
#pragma once
#include "buttons/actions/button_action.hpp"
#include "buttons/commands.hpp"
#include "buttons/hold.hpp"
#include <memory>
#include <string>

namespace buttons {
class CommandRunner {
public:
    virtual ~CommandRunner() = default;
    // Each returns false when X-Plane has no such command (and says so itself, once).
    virtual bool once(const std::string& command) = 0;
    virtual bool begin(const std::string& command) = 0;
    virtual bool end(const std::string& command) = 0;
};

class DirectAction final : public ButtonAction {
public:
    explicit DirectAction(CommandRunner& runner) : runner_(&runner), controls_(std::make_shared<const ControlMap>()), commands_(std::make_shared<const ButtonCommands>()) {}
    void set_commands(std::shared_ptr<const ControlMap> controls, std::shared_ptr<const ButtonCommands> commands);
    void update(ButtonHub& hub, Clock::time_point now, const Log& log) override;
    void close() override;
private:
    void run(const std::vector<HoldAction>& actions);
    CommandRunner* runner_;
    std::shared_ptr<const ControlMap> controls_;
    std::shared_ptr<const ButtonCommands> commands_;
    HoldTracker holds_;
};
}
