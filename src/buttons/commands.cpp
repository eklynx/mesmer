// Implementation of buttons/commands.hpp (read that header for the contract).
#include "buttons/commands.hpp"
#include "buttons/methods.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace buttons {
namespace {
bool ends_with(const std::string& text, const std::string& tail) { return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0; }
}

ButtonCommands ButtonCommands::resolve(const Entries& entries, const ControlMap& map, ReadMethod read, std::vector<std::string>& notes) {
    ButtonCommands out;
    std::map<std::pair<int, Variant>, std::vector<std::string>> writers;   // the entry names that gave commands
    std::set<int> unreachable_noted;
    for (const auto& entry : entries) {
        std::string base = lower_case(entry.first);
        Variant variant = Variant::press;
        if (ends_with(base, "_released")) { variant = Variant::release; base.resize(base.size() - 9); }
        else if (ends_with(base, "_hold")) { variant = Variant::hold; base.resize(base.size() - 5); }
        const auto control = map.resolve(base);
        if (!control) {
            notes.push_back("button_commands: \"" + entry.first + "\" is not a control of this yoke; ignored");
            continue;
        }
        std::vector<std::string> usable;
        for (const auto& text : entry.second) {
            if (text.empty()) continue;   // no command, as for null
            if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) || std::iscntrl(c); })) {
                notes.push_back("button_commands: " + entry.first + " has \"" + text + "\", which is not a command name (it has spaces or control characters); ignored");
                continue;
            }
            usable.push_back(text);
        }
        if (usable.empty()) continue;
        auto& commands = out.commands_[{*control, variant}];
        commands.insert(commands.end(), usable.begin(), usable.end());
        writers[{*control, variant}].push_back(entry.first);
        const bool seen = read == ReadMethod::can_bus ? map.can_source(*control).has_value() : map.hid_source(*control).has_value();
        if (!seen && unreachable_noted.insert(*control).second)
            notes.push_back("button_commands: " + entry.first + (read == ReadMethod::can_bus ? " has no CAN slot linked on this yoke yet" : " has no HID input on this yoke")
                            + ", so it does not fire with this yoke's " + std::string(read_method_name(read)) + " read (it does with " + (read == ReadMethod::can_bus ? "HID" : "CanBus")
                            + "; the yoke's button_aliases can link it)");
    }
    for (const auto& writer : writers) {
        if (writer.second.size() < 2) continue;
        std::string names;
        for (const auto& name : writer.second) names += (names.empty() ? "" : ", ") + name;
        notes.push_back("button_commands: " + names + " are the same control on this yoke; all of their commands run");
    }
    return out;
}

const std::vector<std::string>& ButtonCommands::commands_for(int control, Variant variant) const {
    static const std::vector<std::string> none;
    const auto found = commands_.find({control, variant});
    return found == commands_.end() ? none : found->second;
}
}
