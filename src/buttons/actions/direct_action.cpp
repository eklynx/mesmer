// Implementation of buttons/actions/direct_action.hpp (read that header for the contract).
#include "buttons/actions/direct_action.hpp"

namespace buttons {
void DirectAction::set_commands(std::shared_ptr<const ControlMap> controls, std::shared_ptr<const ButtonCommands> commands) {
    close();   // what is held belongs to the old commands
    controls_ = std::move(controls);
    commands_ = std::move(commands);
}

void DirectAction::run(const std::vector<HoldAction>& actions) {
    for (const auto& action : actions) {
        if (action.begin) runner_->begin(action.command);
        else runner_->end(action.command);
    }
}

void DirectAction::update(ButtonHub& hub, Clock::time_point, const Log&) {
    const auto edges = hub.take_edges();
    for (const auto& edge : edges)
        for (const auto& command : commands_->commands_for(edge.control, edge.released ? Variant::release : Variant::press))
            runner_->once(command);
    run(holds_.plan(edges, *commands_));   // every end before any begin, per command
}

void DirectAction::close() { run(holds_.end_all()); }
}
