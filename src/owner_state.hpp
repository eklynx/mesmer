// SOURCE OF TRUTH for the owner's saved choices that are not in BrunnerDirect.ini:
// today one, "allow unverified yoke" (Developer menu). Specification:
// docs/yoke-presets.md. Saved as BrunnerDirect.state.json in the plugin folder.
//
// The default is OFF, and any doubt means off: a missing file is normal, and a file
// that is not exactly what serialize_owner_state writes (not JSON, another version,
// a missing or non-boolean field, a field this version does not know) is read as off
// rather than guessed at. Only a literal `true` turns the choice on.
#pragma once
#include "json.hpp"
#include <string>
#include <string_view>

namespace brunner {
inline constexpr char state_file[]="BrunnerDirect.state.json";
inline constexpr int owner_state_version=1;
struct OwnerState {
    bool allow_unverified_yoke=false;
    bool operator==(const OwnerState& o) const {return allow_unverified_yoke==o.allow_unverified_yoke;}
};
inline std::string serialize_owner_state(const OwnerState& s) {
    return std::string("{\"version\":")+std::to_string(owner_state_version)+",\"allow_unverified_yoke\":"+(s.allow_unverified_yoke?"true":"false")+"}\n";
}
inline OwnerState parse_owner_state(std::string_view text) {
    try {
        const auto root=parse_json(text);
        if (root.kind!=Json::Kind::object) return {};
        for (const auto& m:root.members) if (m.first!="version" && m.first!="allow_unverified_yoke") return {};
        const auto* version=root.get("version");
        const auto* allow=root.get("allow_unverified_yoke");
        if (!version || version->kind!=Json::Kind::integer || version->integer!=owner_state_version) return {};
        if (!allow || allow->kind!=Json::Kind::boolean) return {};
        return OwnerState{allow->boolean};
    } catch (const std::exception&) {return {};}
}
}
