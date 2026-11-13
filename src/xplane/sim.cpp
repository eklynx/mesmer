// X-Plane implementation of Sim. Contract: xplane/sim.hpp.
#include "xplane/sim.hpp"
#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include <algorithm>
#include <cmath>

namespace brunner::xplane {
namespace {
XPLMDataRef ref(void* handle) {return static_cast<XPLMDataRef>(handle);}
}

void Sim::bind_surfaces() {
    panel_aoa_=XPLMFindDataRef("sim/flightmodel2/wing/elements/element_AOA_deg");
    panel_stalled_=XPLMFindDataRef("sim/flightmodel2/wing/elements/element_is_stalled");
    elevator_deg_=XPLMFindDataRef("sim/flightmodel2/wing/elevator1_deg");
    aileron_deg_=XPLMFindDataRef("sim/flightmodel2/wing/aileron1_deg");
    bank_=XPLMFindDataRef("sim/flightmodel/position/phi");
    vertical_=XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");
    flaps_=XPLMFindDataRef("sim/flightmodel2/wing/flap1_deg");
    aircraft_name_ref_=XPLMFindDataRef("sim/aircraft/view/acf_ui_name");
    panel_speed_=XPLMFindDataRef("sim/flightmodel2/wing/elements/element_speed_msc");
    panel_area_=XPLMFindDataRef("sim/flightmodel2/wing/elements/element_surface_area_mtr_sq");
    air_density_=XPLMFindDataRef("sim/weather/rho");
    native_pitch_=XPLMFindDataRef("sim/flightmodel/misc/act_frc_ptch_lb");
    native_roll_=XPLMFindDataRef("sim/flightmodel/misc/act_frc_roll_lb");
    flow_stamp_={};flow_clock_=-1;
    std::array<char,251> name{};
    if (aircraft_name_ref_) XPLMGetDatab(ref(aircraft_name_ref_),name.data(),0,250);
    aircraft_name_=name.data();
}

void Sim::bind_controls() {
    if (pitch_override_ && roll_override_ && pitch_value_ && roll_value_ && paused_
        && trim_override_ && sim_trim_ && airspeed_ && scenery_loading_ && flight_clock_) return;
    pitch_override_=XPLMFindDataRef("sim/operation/override/override_joystick_pitch");
    roll_override_=XPLMFindDataRef("sim/operation/override/override_joystick_roll");
    pitch_value_=XPLMFindDataRef("sim/joystick/yoke_pitch_ratio");
    roll_value_=XPLMFindDataRef("sim/joystick/yoke_roll_ratio");
    paused_=XPLMFindDataRef("sim/time/paused");
    trim_override_=XPLMFindDataRef("sim/operation/override/override_pitch_trim");
    sim_trim_=XPLMFindDataRef("sim/cockpit2/controls/elevator_trim");
    load_factor_=XPLMFindDataRef("sim/flightmodel/forces/g_nrml");
    airspeed_=XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
    scenery_loading_=XPLMFindDataRef("sim/graphics/scenery/async_scenery_load_in_progress");
    flight_clock_=XPLMFindDataRef("sim/time/total_flight_time_sec");
}

// The user's aircraft: its file picks its profile (force/profiles.hpp); the stock
// C172 SP's also selects roll's airflow and the page's C172 details.
void Sim::identify_aircraft() {
    char name[256]{},path[1024]{};
    XPLMGetNthAircraftModel(0,name,path);
    aircraft_path_=path;std::replace(aircraft_path_.begin(),aircraft_path_.end(),'\\','/');
    aircraft_file_=name;
    const std::string suffix="/Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf";
    default_cessna_=aircraft_path_.size()>=suffix.size() && aircraft_path_.compare(aircraft_path_.size()-suffix.size(),suffix.size(),suffix)==0;
}

bool Sim::controls_available(bool feeding) const {
    if (!feeding) return pitch_override_ && roll_override_ && pitch_value_ && roll_value_;
    return pitch_override_ && roll_override_ && pitch_value_ && roll_value_
        && XPLMCanWriteDataRef(ref(pitch_override_)) && XPLMCanWriteDataRef(ref(roll_override_))
        && XPLMCanWriteDataRef(ref(pitch_value_)) && XPLMCanWriteDataRef(ref(roll_value_))
        && (owns_axes_ || (!XPLMGetDatai(ref(pitch_override_)) && !XPLMGetDatai(ref(roll_override_))));
}
// Force feel suppresses X-Plane's own pitch trim on every aircraft: the model's rest already
// carries trim (decision 15), so it must count once. Another plugin's trim override costs
// force feel only, never the yoke's input.
bool Sim::trim_available() const {
    return trim_override_ && XPLMCanWriteDataRef(ref(trim_override_)) && (owns_trim_ || !XPLMGetDatai(ref(trim_override_)));
}

void Sim::release_axes() {
    if (owns_trim_) {XPLMSetDatai(ref(trim_override_),0);owns_trim_=false;}
    if (!owns_axes_) return;
    XPLMSetDataf(ref(pitch_value_),0);XPLMSetDataf(ref(roll_value_),0);
    XPLMSetDatai(ref(pitch_override_),0);XPLMSetDatai(ref(roll_override_),0);owns_axes_=false;
}
void Sim::leave_axes() {
    if (!owns_axes_) return;
    XPLMSetDataf(ref(pitch_value_),0);XPLMSetDataf(ref(roll_value_),0);
    XPLMSetDatai(ref(pitch_override_),0);XPLMSetDatai(ref(roll_override_),0);owns_axes_=false;
}
// Read-only mode: every aircraft gets pitch and roll from the yoke whenever its position is valid.
std::string Sim::apply_input(const FlightInput& input) {
    bind_controls();
    if (!pitch_override_ || !roll_override_ || !pitch_value_ || !roll_value_) return "Waiting for simulator controls";
    if (!input.ready) {release_axes();return input.reason;}
    if (!owns_axes_) {
        if (!XPLMCanWriteDataRef(ref(pitch_override_)) || !XPLMCanWriteDataRef(ref(roll_override_))
            || !XPLMCanWriteDataRef(ref(pitch_value_)) || !XPLMCanWriteDataRef(ref(roll_value_))) return "Simulator controls are not writable";
        if (XPLMGetDatai(ref(pitch_override_)) || XPLMGetDatai(ref(roll_override_))) return "Pitch or roll is owned by another plugin";
        XPLMSetDatai(ref(pitch_override_),1);XPLMSetDatai(ref(roll_override_),1);owns_axes_=true;
    }
    XPLMSetDataf(ref(pitch_value_),input.pitch);XPLMSetDataf(ref(roll_value_),input.roll);
    return input.reason;
}
void Sim::claim_trim() {
    if (!owns_trim_) {XPLMSetDatai(ref(trim_override_),1);owns_trim_=true;}
}
void Sim::release_trim() {
    if (owns_trim_) {XPLMSetDatai(ref(trim_override_),0);owns_trim_=false;}
}
void Sim::set_sim_trim(float ratio) {
    if (sim_trim_) XPLMSetDataf(ref(sim_trim_),ratio);
}

bool Sim::paused() const {return paused_ && XPLMGetDatai(ref(paused_))!=0;}

std::optional<Readiness> Sim::readiness() const {
    if (!scenery_loading_ || !flight_clock_) return std::nullopt;
    return Readiness{XPLMGetDatai(ref(scenery_loading_))!=0,static_cast<double>(XPLMGetDataf(ref(flight_clock_)))};
}

// Reads only; it must stay free of resource creation and motor requests (it runs after the flight model).
std::optional<FrameRead> Sim::observe_frame(bool enabled,Clock::time_point now,const FrameSections& sections) {
    if (!enabled || !paused_ || XPLMGetDatai(ref(paused_)) || !flight_clock_ || !sim_trim_ || !pitch_value_ || !roll_value_) {
        flow_stamp_={};flow_clock_=-1;
        return std::nullopt;
    }
    const float clock=XPLMGetDataf(ref(flight_clock_));
    if (!std::isfinite(clock) || clock<flow_clock_) {flow_stamp_={};flow_clock_=-1;return std::nullopt;}
    if (!(clock>flow_clock_)) return std::nullopt;
    std::array<float,99> speed{},area{};
    const int n=panel_speed_ && panel_area_?std::min(XPLMGetDatavf(ref(panel_speed_),speed.data(),0,99),XPLMGetDatavf(ref(panel_area_),area.data(),0,99)):0;
    const double nan=unknown;
    const double rho=air_density_?XPLMGetDataf(ref(air_density_)):nan;
    // The stock C172's surfaces for the page; the tail's and each aileron's air over the
    // profile's sections on the profile's own aircraft (elsewhere airspeed's).
    auto flow=sections.default_cessna?c172_flow(rho,speed.data(),area.data(),n):SurfaceFlow{};
    flow.tail=sections.matched && sections.tail
        ?sections_pa(rho,speed.data(),area.data(),n,(*sections.tail)[0],(*sections.tail)[1]):-1;
    // Each panel's angle and stall, read once a frame when a surface needs them.
    FrameRead frame;
    std::optional<AileronInputs> ailerons;
    std::optional<AileronSide> tail_read;
    if (sections.matched && (sections.aileron || sections.wing_root || sections.tail) && panel_aoa_ && panel_stalled_) {
        std::array<float,99> aoa{},stalled{};
        const int m=std::min({n,XPLMGetDatavf(ref(panel_aoa_),aoa.data(),0,99),XPLMGetDatavf(ref(panel_stalled_),stalled.data(),0,99)});
        const auto side=[&](int wing) {return aileron_side(rho,speed.data(),area.data(),aoa.data(),stalled.data(),m,wing);};
        if (sections.aileron) {
            const auto left=side((*sections.aileron)[0]),right=side((*sections.aileron)[1]);
            if (left && right) ailerons=AileronInputs{*left,*right};
        }
        if (sections.wing_root) {
            const auto first=side((*sections.wing_root)[0]),second=side((*sections.wing_root)[1]);
            if (first && second) frame.root_stall=std::array<AileronSide,2>{*first,*second};
        }
        // The tail's angle and stalled share over both its sections (decision 17).
        if (sections.tail)
            tail_read=tail_side(rho,speed.data(),area.data(),aoa.data(),stalled.data(),m,(*sections.tail)[0],(*sections.tail)[1]);
    }
    frame.sample={airspeed_?XPLMGetDataf(ref(airspeed_)):nan,XPLMGetDataf(ref(sim_trim_)),flow,
        XPLMGetDataf(ref(pitch_value_)),XPLMGetDataf(ref(roll_value_)),native_pitch_?XPLMGetDataf(ref(native_pitch_)):nan,
        native_roll_?XPLMGetDataf(ref(native_roll_)):nan,clock,load_factor_?XPLMGetDataf(ref(load_factor_)):1.0,ailerons,tail_read};
    flow_clock_=clock;flow_stamp_=now;
    return frame;
}

PageReadings Sim::read_page() const {
    const auto value=[&](Ref handle) {return handle?static_cast<double>(XPLMGetDataf(ref(handle))):unknown;};
    PageReadings r;
    r.ias_kt=value(airspeed_);r.bank_deg=value(bank_);r.vertical_fpm=value(vertical_);
    if (flaps_) {
        std::array<float,48> flaps{};
        const int n=std::clamp(XPLMGetDatavf(ref(flaps_),flaps.data(),0,48),0,48);
        if (n>0) r.flaps_deg=*std::max_element(flaps.begin(),flaps.begin()+n);
    }
    return r;
}

C172Details Sim::read_c172() const {
    C172Details d;
    std::array<float,100> area{},values{};
    const int n=panel_area_?XPLMGetDatavf(ref(panel_area_),area.data(),0,99):0;
    if (panel_aoa_) {
        const int m=std::min(n,XPLMGetDatavf(ref(panel_aoa_),values.data(),0,99));
        d.pitch_aoa_deg=c172_section_mean(values.data(),area.data(),m,8,9);
        d.roll_aoa_deg=c172_section_mean(values.data(),area.data(),m,2,3);
    }
    if (panel_stalled_) {
        const int m=XPLMGetDatavf(ref(panel_stalled_),values.data(),0,99);
        d.pitch_stalled=c172_section_stalled(values.data(),m,8,9);
        d.roll_stalled=c172_section_stalled(values.data(),m,2,3);
    }
    // Degrees, trailing edge down positive, as X-Plane reports them (api.md). Wing 2 is the left
    // (read 2026-11-09: it rose under a left input).
    std::array<float,48> surface{};
    if (elevator_deg_ && XPLMGetDatavf(ref(elevator_deg_),surface.data(),0,10)==10) d.pitch_deflection_deg=(surface[8]+surface[9])/2.0;
    if (aileron_deg_ && XPLMGetDatavf(ref(aileron_deg_),surface.data(),0,4)==4) d.roll_deflection_deg=(surface[2]-surface[3])/2.0;
    return d;
}
}
