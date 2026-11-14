// SOURCE OF TRUTH for editing one direction field of BrunnerDirect.yokes.json (the user's presets
// file, brunner/yoke_preset.hpp) from the feel page. Pure: text in, text out; the plugin reads and
// writes the file. Spec: docs/yoke-presets.md ("Changing directions from the feel page").
//
// The six fields: roll_encoder_sign, pitch_encoder_sign (+1 or -1: the model's counts are sign x the
// encoder, the reading side), roll_spring_sign, pitch_spring_sign (+1 or -1: the controller's spring
// frame is sign x the encoder, which decides how the writes are mirrored) and roll_input_reverse,
// pitch_input_reverse (true or false: the axis X-Plane reads).
#pragma once
#include "brunner/yoke_preset.hpp"
#include "json.hpp"
#include <string>
#include <vector>

namespace brunner {
struct DirectionChange {
    bool pitch=false;          // false: roll
    enum class Field {encoder_sign,spring_sign,input_reverse} field=Field::encoder_sign;
    int value=0;               // a sign: +1 or -1. input reverse: 0 or 1
    bool input_reverse() const {return field==Field::input_reverse;}
};
inline std::string direction_key(const DirectionChange& c) {
    return std::string(c.pitch?"pitch":"roll")+(c.input_reverse()?"_input_reverse":c.field==DirectionChange::Field::spring_sign?"_spring_sign":"_encoder_sign");
}
inline bool valid_direction(const DirectionChange& c) {
    return c.input_reverse()?(c.value==0 || c.value==1):(c.value==1 || c.value==-1);
}

struct OverridesPatch {
    std::string text;   // the whole new file, when error is empty
    std::string error;  // why the change was refused; the old file stands
    bool ok() const {return error.empty();}
};

// Happy path: `overrides` is the current BrunnerDirect.yokes.json (empty string when there is none) and
// `shipped` the texts of the plugin's yokes/ folder; the result is the file with `model`'s entry holding
// the new value, every other key and entry as they were (member order kept, whitespace normalised).
// A model with no entry gets one that names only the change, which is a patch of the shipped model.
// Errors (none thrown): an invalid value; an overrides file that is not a JSON object with a "yokes"
// array; a model that no preset names (the new entry would lack the fields a new model needs); or any
// result load_yoke_models refuses. The caller keeps the old file in every error case.
inline OverridesPatch patch_direction(const std::string& overrides,const std::vector<std::string>& shipped,
                                      const std::string& model,const DirectionChange& change) {
    if (!valid_direction(change)) return {"","direction value out of range"};
    Json file;
    if (overrides.find_first_not_of(" \t\r\n")==std::string::npos) {
        file.kind=Json::Kind::object;
        Json schema;schema.kind=Json::Kind::integer;schema.integer=1;
        Json yokes;yokes.kind=Json::Kind::array;
        file.members={{"schema",schema},{"yokes",yokes}};
    } else {
        try {file=parse_json(overrides);} catch (const JsonError& e) {return {"",std::string("the presets file is not valid JSON: ")+e.what()};}
    }
    Json* list=nullptr;
    if (file.kind==Json::Kind::object) for (auto& m:file.members) if (m.first=="yokes") list=&m.second;
    if (!list || list->kind!=Json::Kind::array) return {"","the presets file has no \"yokes\" list"};
    Json* entry=nullptr;
    for (auto& item:list->items) {
        const auto* name=item.kind==Json::Kind::object?item.get("name"):nullptr;
        if (name && name->kind==Json::Kind::string && name->text==model) entry=&item;
    }
    if (!entry) {
        Json fresh;fresh.kind=Json::Kind::object;
        Json name;name.kind=Json::Kind::string;name.text=model;
        fresh.members.push_back({"name",name});
        list->items.push_back(fresh);
        entry=&list->items.back();
    }
    Json value;
    if (change.input_reverse()) {value.kind=Json::Kind::boolean;value.boolean=change.value==1;}
    else {value.kind=Json::Kind::integer;value.integer=change.value;}
    const auto key=direction_key(change);
    bool replaced=false;
    for (auto& m:entry->members) if (m.first==key) {m.second=value;replaced=true;}
    if (!replaced) entry->members.push_back({key,value});
    auto text=write_json(file)+"\n";
    const auto loaded=load_yoke_models(shipped,text);
    if (!loaded.overrides_applied) {
        std::string why;
        for (const auto& n:loaded.notes) why+=(why.empty()?"":"; ")+n;
        return {"",why.empty()?"the presets file was refused":why};
    }
    bool known=false;
    for (const auto& p:loaded.presets) known=known || p.name==model;
    if (!known) return {"","no yoke model named "+model};
    return {std::move(text),""};
}
}
