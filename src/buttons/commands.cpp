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

namespace {
using Key = std::pair<int, Variant>;
struct Collected {
    std::map<Key, std::vector<std::string>> commands;   // what to run
    std::set<Key> named;                               // every control and variant an entry named, with commands or without
};
// One set of entries resolved through the controls. `source` starts each note ("button_commands" or the aircraft's).
Collected collect(const ButtonCommands::Entries& entries, const ControlMap& map, std::vector<std::string>& notes, const std::string& source) {
    Collected out;
    std::map<Key, std::vector<std::string>> writers;   // the entry names that gave commands
    for (const auto& entry : entries) {
        std::string base = lower_case(entry.first);
        Variant variant = Variant::press;
        if (ends_with(base, "_released")) { variant = Variant::release; base.resize(base.size() - 9); }
        else if (ends_with(base, "_hold")) { variant = Variant::hold; base.resize(base.size() - 5); }
        const auto control = map.resolve(base);
        if (!control) {
            notes.push_back(source + ": \"" + entry.first + "\" is not a control of this yoke; ignored");
            continue;
        }
        out.named.insert({*control, variant});
        std::vector<std::string> usable;
        for (const auto& text : entry.second) {
            if (text.empty()) continue;   // no command, as for null
            if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) || std::iscntrl(c); })) {
                notes.push_back(source + ": " + entry.first + " has \"" + text + "\", which is not a command name (it has spaces or control characters); ignored");
                continue;
            }
            usable.push_back(text);
        }
        if (usable.empty()) continue;
        auto& commands = out.commands[{*control, variant}];
        commands.insert(commands.end(), usable.begin(), usable.end());
        writers[{*control, variant}].push_back(entry.first);
    }
    for (const auto& writer : writers) {
        if (writer.second.size() < 2) continue;
        std::string names;
        for (const auto& name : writer.second) names += (names.empty() ? "" : ", ") + name;
        notes.push_back(source + ": " + names + " are the same control on this yoke; all of their commands run");
    }
    return out;
}
}

ButtonCommands ButtonCommands::resolve(const Entries& entries, const ControlMap& map, std::vector<std::string>& notes) {
    ButtonCommands out;
    out.commands_ = collect(entries, map, notes, "button_commands").commands;
    return out;
}

ButtonCommands ButtonCommands::resolve(const Entries& base, const Entries& aircraft, const std::string& label, const ControlMap& map, std::vector<std::string>& notes) {
    ButtonCommands out;
    out.commands_ = collect(base, map, notes, "button_commands").commands;
    if (aircraft.empty()) return out;
    const auto over = collect(aircraft, map, notes, label + " button_commands");
    std::size_t replaced = 0;
    for (const auto& key : over.named) {
        if (out.commands_.erase(key)) ++replaced;   // replace, not add: the state file's commands for this control and variant go
        const auto found = over.commands.find(key);
        if (found != over.commands.end()) out.commands_[key] = found->second;
    }
    if (replaced) notes.push_back(label + " button_commands: replace the state file's commands for " + std::to_string(replaced) + (replaced == 1 ? " control" : " controls"));
    return out;
}

const std::vector<std::string>& ButtonCommands::commands_for(int control, Variant variant) const {
    static const std::vector<std::string> none;
    const auto found = commands_.find({control, variant});
    return found == commands_.end() ? none : found->second;
}
}
