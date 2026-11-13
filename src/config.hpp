#pragma once
#include "flight.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace brunner {
inline std::string config_trim(const std::string& text) {
    const auto begin=text.find_first_not_of(" \t\r\n");
    if (begin==std::string::npos) return {};
    return text.substr(begin,text.find_last_not_of(" \t\r\n")-begin+1);
}
// Complete, versioned startup defaults. Invalid/partial files fail closed;
// runtime tuning never writes this file or enables motors by itself.
// Schema 4 (decision 15, 2026-11-08): pitch is the elevator model, so its
// keys are the drive's calibration and drag; the elevator's figures are the
// profiles file's. Schema 5 (decision 6, 2026-11-09): roll is the aileron
// model, so roll's airflow and plain spring keys retire and its drive
// calibration, roll_torque_units_per_nm, joins. Schema 6 (decision 16,
// 2026-11-09): no firmest on either axis, so roll_spring_percent and
// pitch_spring_percent retire; the light spring bench test's strengths are
// the commissioning channel's (Tuning's defaults, 40 %). Schemas 2 to 5
// still parse, so a new plugin starts on an install's older file: retired
// keys are accepted and ignored (listed in `retired` for the log), in every
// schema.
struct Configuration {Tuning tuning;FlightSettings flight;int workbench_port=0;std::vector<std::string> retired;};
inline Configuration parse_configuration(std::istream& input) {
    std::map<std::string,int> values;
    std::string line;
    while (std::getline(input,line)) {
        line=config_trim(line);
        if (line.empty() || line.front()=='#') continue;
        const auto equals=line.find('=');
        if (equals==std::string::npos) throw std::invalid_argument("Malformed Brunner configuration line");
        const auto key=config_trim(line.substr(0,equals)),value=config_trim(line.substr(equals+1));
        if (values.count(key)) throw std::invalid_argument("Duplicate Brunner configuration key: "+key);
        int number=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid integer for "+key);
        values[key]=number;
    }
    if (input.bad()) throw std::invalid_argument("Incomplete Brunner configuration");
    const auto schema=values.find("schema_version");
    if (schema==values.end()) throw std::invalid_argument("Incomplete Brunner configuration");
    if (schema->second<2 || schema->second>6) throw std::invalid_argument("Unsupported Brunner configuration schema");
    const int version=schema->second;
    // Required keys first, then optional ones with their defaults.
    std::vector<std::string> required{"schema_version","pitch_trim_counts","auto_start","reference_airspeed_knots"},
        optional{"force_feel","buffet","buffet_percent","buffet_period","read_only_roll_reverse","read_only_pitch_reverse"};
    const std::vector<std::string> drive{"pitch_force_units_per_lb","pitch_force_gain_divisor","pitch_move_back_gain"};
    // What only the firmness pitch read (main's schemas 2 and 3), the
    // never-deployed elevator_model switch, and roll's airflow and plain
    // springs (schemas 2 to 4; schema 2's shared floor and pressure were roll's),
    // and each axis's firmest (schemas 2 to 5).
    const std::vector<std::string> retired{"allow_unverified_yoke","maximum_trim_counts","pitch_minimum_spring_percent","pitch_half_gain_pa","full_trim_pa",
        "elevator_unbalance_inlb","elevator_model","roll_minimum_spring_percent","roll_half_gain_pa","plain_spring_percent",
        "minimum_spring_percent","half_gain_pa","roll_spring_percent","pitch_spring_percent"};
    if (version>=3) required.push_back("workbench_port");
    for (const auto& k:drive) (version>=4?required:optional).push_back(k);
    (version>=5?required:optional).push_back("roll_torque_units_per_nm");
    const auto listed=[](const std::vector<std::string>& v,const std::string& k) {return std::find(v.begin(),v.end(),k)!=v.end();};
    Configuration result;
    for (const auto& entry:values) {
        if (listed(required,entry.first) || listed(optional,entry.first)) continue;
        if (listed(retired,entry.first)) {result.retired.push_back(entry.first);continue;}
        throw std::invalid_argument("Unknown Brunner configuration key: "+entry.first);
    }
    for (const auto& key:required) if (!values.count(key)) throw std::invalid_argument("Incomplete Brunner configuration");
    const auto set=[&](const char* key,TuningField field) {
        if (!set_tuning(result.tuning,field,values.at(key))) throw std::invalid_argument(std::string("Brunner configuration outside commissioning bounds: ")+key);
    };
    set("pitch_trim_counts",TuningField::pitch_trim);
    const auto value=[&](const char* key,int fallback) {const auto found=values.find(key);return found==values.end()?fallback:found->second;};
    auto& f=result.flight;
    f.auto_start=values.at("auto_start");
    f.reference_airspeed=values.at("reference_airspeed_knots");
    f.force_feel=value("force_feel",f.force_feel);
    f.read_only_roll_reverse=value("read_only_roll_reverse",f.read_only_roll_reverse);f.read_only_pitch_reverse=value("read_only_pitch_reverse",f.read_only_pitch_reverse);
    f.buffet=value("buffet",f.buffet);f.buffet_percent=value("buffet_percent",f.buffet_percent);f.buffet_period=value("buffet_period",f.buffet_period);
    f.roll_torque_units_per_nm=value("roll_torque_units_per_nm",f.roll_torque_units_per_nm);
    f.pitch_force_units_per_lb=value("pitch_force_units_per_lb",f.pitch_force_units_per_lb);
    f.pitch_force_gain_divisor=value("pitch_force_gain_divisor",f.pitch_force_gain_divisor);
    f.pitch_move_back_gain=value("pitch_move_back_gain",f.pitch_move_back_gain);
    if (version>=3) {
        result.workbench_port=values.at("workbench_port");
        if (result.workbench_port!=0 && (result.workbench_port<1024 || result.workbench_port>65535))
            throw std::invalid_argument("Brunner workbench_port must be 0 or 1024-65535");
    }
    if (!valid_flight_settings(result.flight)) throw std::invalid_argument("Flight defaults outside validated bounds");
    return result;
}
}
