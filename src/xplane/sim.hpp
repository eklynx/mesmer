// SOURCE OF TRUTH for how the plugin reads and writes X-Plane's own datarefs:
// the pilot's axis and trim overrides, the flight model's air, panel and surface
// readings, the pause and scenery state, and the user's aircraft. This header is
// the interface; sim.cpp is the implementation. Nothing here knows about the
// yoke, the menu or the feel page: it reports what X-Plane says and sets what it
// is asked to set. The plugin's own published datarefs are xplane/published.hpp.
//
// Thread: X-Plane's thread only (a flight loop or command callback). The feel page's
// server thread never calls this.
//
// Missing datarefs are normal (an old X-Plane, or before the first bind): every
// reader returns "unknown" (NaN, nullopt, false) and every writer does nothing,
// as the plugin did when this code lived in plugin.cpp.
#pragma once
#include "brunner/core.hpp"
#include "force/flight.hpp"
#include <array>
#include <limits>
#include <optional>
#include <string>

namespace brunner::xplane {
// Which of the user's aircraft sections the profile reads (workbench Applied); empty
// elsewhere. default_cessna selects the stock C172's surface details.
struct FrameSections {
    bool default_cessna=false,matched=false;
    std::optional<std::array<int,2>> tail,aileron,wing_root;
};
// One completed physics frame, as read after the flight model.
struct FrameRead {
    FlightSample sample;
    std::optional<std::array<AileronSide,2>> root_stall; // buffet's pitch source, profile's own aircraft only
};
struct Readiness {bool scenery_busy=false;double flight_time=0;};
inline constexpr double unknown=std::numeric_limits<double>::quiet_NaN();
// What the feel page shows beside the model: flight state, and (stock C172 only) its surfaces.
struct PageReadings {double ias_kt=unknown,bank_deg=unknown,vertical_fpm=unknown,flaps_deg=unknown;};
struct C172Details {
    double pitch_aoa_deg=unknown,roll_aoa_deg=unknown,pitch_deflection_deg=unknown,roll_deflection_deg=unknown;
    int pitch_stalled=-1,roll_stalled=-1; // -1 unknown
};

class Sim {
 public:
    // ---- Finding datarefs. Idempotent; call again after an aircraft change.
    void bind_controls();    // axis and trim overrides, pause, scenery, clock, speed, load factor
    void bind_surfaces();    // panel and surface readings, the aircraft's name; restarts frame timing
    void identify_aircraft();// the user's aircraft file and path (default_cessna for the stock C172 SP)
    const std::string& aircraft_name() const {return aircraft_name_;}
    const std::string& aircraft_path() const {return aircraft_path_;}
    const std::string& aircraft_file() const {return aircraft_file_;}
    bool default_cessna() const {return default_cessna_;}

    // ---- The pilot's axes and pitch trim.
    // True when pitch and roll can be taken: all four datarefs exist and are writable, and
    // nobody else holds the overrides (unless this plugin does).
    bool controls_available() const;
    bool trim_available() const; // likewise the pitch-trim override
    // Gives X-Plane the yoke's position. Returns the reason to show: input.reason when the yoke
    // is (or is not) usable, or why the controls cannot be taken ("Waiting for simulator controls",
    // "Simulator controls are not writable", "Pitch or roll is owned by another plugin"). A yoke that
    // is not ready releases the axes.
    std::string apply_input(const FlightInput& input);
    void release_axes();     // axes to zero, overrides off, trim released too; safe when not owned
    bool owns_axes() const {return owns_axes_;}
    bool owns_trim() const {return owns_trim_;}
    void claim_trim();       // force feel counts the model's trim once: X-Plane's own is suppressed
    void release_trim();
    bool has_sim_trim() const {return sim_trim_!=nullptr;}
    void set_sim_trim(float ratio); // the developer lab only

    // ---- State and the per-frame read.
    bool pause_known() const {return paused_!=nullptr;}
    bool paused() const;
    // After the flight model: restarts timing when the plugin is disabled, X-Plane is paused or a
    // dataref is missing; returns a frame only when the flight clock has advanced. The frame's
    // moment is flow_stamp().
    std::optional<FrameRead> observe_frame(bool enabled,Clock::time_point now,const FrameSections& sections);
    Clock::time_point flow_stamp() const {return flow_stamp_;}
    std::optional<Readiness> readiness() const; // nullopt when scenery or clock is missing

    // ---- For the feel page, about ten times a second.
    PageReadings read_page() const;
    C172Details read_c172() const; // meaningful on the stock C172 only; call only then

 private:
    using Ref=void*; // XPLMDataRef
    Ref pitch_override_=nullptr,roll_override_=nullptr,pitch_value_=nullptr,roll_value_=nullptr;
    Ref paused_=nullptr,scenery_loading_=nullptr,flight_clock_=nullptr;
    Ref trim_override_=nullptr,sim_trim_=nullptr,airspeed_=nullptr,load_factor_=nullptr;
    Ref panel_speed_=nullptr,panel_area_=nullptr,air_density_=nullptr,native_pitch_=nullptr,native_roll_=nullptr;
    Ref panel_aoa_=nullptr,panel_stalled_=nullptr,elevator_deg_=nullptr,aileron_deg_=nullptr,bank_=nullptr,vertical_=nullptr,flaps_=nullptr,aircraft_name_ref_=nullptr;
    bool owns_axes_=false,owns_trim_=false,default_cessna_=false;
    std::string aircraft_name_,aircraft_path_,aircraft_file_;
    Clock::time_point flow_stamp_{};
    float flow_clock_=-1;
};
}
