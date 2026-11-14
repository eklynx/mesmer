// Tests for the active Drive (brunner/drive.hpp, spec docs/yoke-presets.md): the CLS-E's
// numbers are unchanged, and another model's travel, force ceiling, target torque
// and directions flow through the force models and the drive writes. No hardware.
#include "force/aileron.hpp"
#include "brunner/drive.hpp"
#include "force/elevator.hpp"
#include "brunner/loading.hpp"
#include "force/tuning.hpp"
#include <functional>
#include <iostream>

using namespace brunner;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(x) check_at((x),__LINE__)
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
template<class F> bool throws(F f) {try {f();} catch (const std::exception&) {return true;} return false;}
Drive ng_drive(std::uint32_t roll_register=2150,std::uint32_t pitch_register=2250) {return make_drive(builtin_cls_e_ng(),roll_register,pitch_register);}
bool write_ok(unsigned node,Register reg,std::uint32_t value) {return loading_write_allowed(node,RegisterWrite{reg,value,0});}
std::uint32_t signed_bits(std::int32_t v) {return static_cast<std::uint32_t>(v);}

// The guard: with no yoke identified, every constant is the one the models were built on.
void the_cls_e_drive_is_the_original_constants() {
    check(active_drive()==cls_e_drive());
    check(roll_travel_bound()==13000 && pitch_travel_bound()==12000);
    check(roll_travel_counts()==13000 && full_travel_counts()==12000);
    check(protection_limit_units()==4500 && drive_margin_units()==4000 && max_pitch_bias()==2250);
    check(strongest_spring_percent()==400 && max_drive_reserve_units()==1350 && fixed_target_torque()==50);
    check(roll_units_per_count_percent()==1000.0/13000/100);          // bit for bit, not just close
    check(curve_units_per_count_percent()==1000.0/12000/100);
    check(roll_counts_from_encoder(5)==-5.0 && drive_counts_from_encoder(5)==-5.0);
    check(expected_protection_limit(1)==4500 && expected_protection_limit(2)==4500);
    const auto d=cls_e_drive();
    check(d.model=="cls-e" && d.wheel_deg==86 && d.margin_units==500 && d.limit_units==4500);
    check(d.roll_input_reverse && !d.pitch_input_reverse);
}
void the_ng_drive() {
    const auto d=ng_drive();
    check(d.model=="cls-e-ng" && d.roll_travel_counts==7350 && d.pitch_travel_counts==5300 && d.wheel_deg==90 && d.target_torque==25);
    check(d.roll_input_reverse && d.pitch_input_reverse);   // the NG inverts pitch by default
    // The lower of the preset's ceiling (spec: 11000 / 9500) and the register (2150 / 2250) per axis,
    // then one budget from the lower axis, less the margin (the CLS-E's 500 of 4500).
    check(d.roll_ceiling_units==2150 && d.pitch_ceiling_units==2250 && d.limit_units==2150);
    check(d.margin_units==239 && d.roll_limit_register==2150 && d.pitch_limit_register==2250);
    ScopedDrive scope(d);
    check(roll_travel_bound()==7350 && pitch_travel_bound()==5300 && protection_limit_units()==2150);
    check(drive_margin_units()==1911 && strongest_spring_percent()==191 && max_pitch_bias()==1075);
    check(max_drive_reserve_units()==1911-(400+1075) && valid_drive_reserve(max_drive_reserve_units()) && !valid_drive_reserve(max_drive_reserve_units()+1));
    check(fixed_target_torque()==25 && expected_protection_limit(1)==2150 && expected_protection_limit(2)==2250);
    check(roll_travel_counts()==7350 && full_travel_counts()==5300);
    // The spring's law (10 units per percent at full travel) holds at the NG's travel.
    check(near(roll_units_per_count_percent()*roll_travel_counts()*100,1000));
    check(near(curve_units_per_count_percent()*full_travel_counts()*100,1000));
    check(near(worst_force_units(100,0,0,pitch_travel_bound()),1000) && near(worst_force_units(100,0,0,roll_travel_bound()),1000));
}
// "Force numbers stop at limits": whichever is lower, the controller's register or the spec, wins.
void the_ceiling_is_the_lower_of_register_and_spec() {
    const auto low_register=ng_drive(2150,2250);
    check(low_register.limit_units==2150);                                   // register below the spec: the register
    const auto high_register=ng_drive(20000,20000);
    check(high_register.roll_ceiling_units==11000 && high_register.pitch_ceiling_units==9500 && high_register.limit_units==9500); // spec below the register: the spec
    const auto between=ng_drive(10000,9000);
    check(between.roll_ceiling_units==10000 && between.pitch_ceiling_units==9000 && between.limit_units==9000);
    // Each axis separately: roll's register is lower, pitch's spec is lower.
    const auto mixed=ng_drive(3000,20000);
    check(mixed.roll_ceiling_units==3000 && mixed.pitch_ceiling_units==9500 && mixed.limit_units==3000);
    // A controller that reports no usable limit is refused: a ceiling is never assumed.
    check(throws([&]{ng_drive(0,2250);}) && throws([&]{ng_drive(2150,0);}) && throws([&]{ng_drive(70000,2250);}));
    // Editing the preset changes the ceiling: easy to change, no rebuild.
    auto roomy=builtin_cls_e_ng();roomy.roll_ceiling_units=1800;
    check(make_drive(roomy,2150,2250).limit_units==1800);
}
// The models scale: the same physical state is the same fraction of travel on every model.
void the_models_scale_with_travel() {
    const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
    const AileronFigures ailerons{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};
    const ElevatorInputs air{0.1,600,1};
    const AileronInputs wings{{600,0,0},{600,0,0}};
    const auto cls_pitch=solve_pitch(c172,air,0,222);
    const auto cls_roll=solve_roll(ailerons,wings,0,1000,true);
    const auto ng=ng_drive();
    ScopedDrive scope(ng);
    const auto ng_pitch=solve_pitch(c172,air,0,222);
    const auto ng_roll=solve_roll(ailerons,wings,0,1000,true);
    check(near(ng_pitch.centre_counts/5300,cls_pitch.centre_counts/12000,1e-9));
    check(near(ng_pitch.rest_counts/5300,cls_pitch.rest_counts/12000,1e-9));
    check(near(ng_roll.rest_counts/7350,cls_roll.rest_counts/13000,1e-9));
    check(near(std::fabs(ng_pitch.centre_counts),std::fabs(cls_pitch.centre_counts)*5300/12000,1e-6));
}
// Whatever the models ask, what is sent stays inside the ceiling, and a request over it saturates.
void forces_stop_at_the_ceiling() {
    const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
    const AileronFigures ailerons{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};
    int cls_strongest=0;
    for (const double pa:{0.0,100.0,1000.0,5000.0,20000.0,100000.0})
        for (const double frac:{-0.9,0.0,0.9})
            cls_strongest=std::max(cls_strongest,solve_pitch(c172,{0.1,pa,1},frac*12000,222).strength);
    ScopedDrive scope(ng_drive());
    int ng_strongest=0;
    for (const double pa:{0.0,100.0,1000.0,5000.0,20000.0,100000.0})
        for (const double frac:{-0.9,0.0,0.9}) {
            const auto p=solve_pitch(c172,{0.1,pa,1},frac*5300,222);
            check(p.strength>=softest_spring_percent && p.strength<=strongest_spring_percent());
            check(within_margin(p.strength,p.centre,p.bias,pitch_travel_bound()));                 // never past the budget anywhere in the travel
            check(std::abs(p.centre)<=pitch_travel_bound() && std::abs(p.bias)<=max_pitch_bias()); // and the registers stay in range
            const auto r=solve_roll(ailerons,{{pa,0,0},{pa,0,0}},frac*7350,1000,true);
            check(r.strength>=softest_spring_percent && r.strength<=strongest_spring_percent());
            check(within_margin(r.strength,r.centre,0,roll_travel_bound()) && std::abs(r.centre)<=roll_travel_bound());
            ng_strongest=std::max(ng_strongest,std::max(p.strength,r.strength));
        }
    check(ng_strongest<=191);
    // The NG holds a resting point only up to +-5300 on pitch (a write of 5468 read back 5300 and faulted
    // force feel, 2026-11-13) and, from its homing sweep, about +-7392 on roll: nothing the models ask
    // for may be past that, even from a yoke beyond its stops.
    check(pitch_travel_bound()<=5300 && roll_travel_bound()<=7392);
    for (const double pa:{0.0,5000.0,100000.0})
        for (const double x:{-9999.0,-5300.0,5300.0,9999.0}) {
            check(std::abs(solve_pitch(c172,{0.1,pa,1},x,222).centre)<=5300);
            check(std::abs(solve_roll(ailerons,{{pa,0,0},{pa,0,0}},x*7350/5300,1000,true).centre)<=7392);
        }
    check(cls_strongest>191);   // the CLS-E reaches strengths the NG's ceiling does not allow: the NG saturates, it is not refused
    // A tuning past the budget is not valid on the NG, and the same one is on the CLS-E.
    check(!valid_tuning(Tuning{192,40,0,0,0}) && valid_tuning(Tuning{191,40,0,0,0}));
    check(!valid_tuning(Tuning{40,40,5301,0,0}) && !valid_tuning(Tuning{40,40,0,0,7351}));
}
// The writes: bounds follow the active drive, per node.
void the_writes_follow_the_drive() {
    // The CLS-E.
    check(write_ok(1,trim_position,signed_bits(13000)) && !write_ok(1,trim_position,signed_bits(13001)));
    check(write_ok(2,trim_position,signed_bits(-12000)) && !write_ok(2,trim_position,signed_bits(-12001)));
    check(write_ok(1,target_torque,50) && !write_ok(1,target_torque,25));
    check(write_ok(1,force_scale,400) && !write_ok(1,force_scale,401));
    check(write_ok(2,manual_force_bias,encode_bias(2250)) && !write_ok(2,manual_force_bias,encode_bias(2251)));
    {
        ScopedDrive scope(ng_drive());
        check(write_ok(1,trim_position,signed_bits(7350)) && !write_ok(1,trim_position,signed_bits(7351)));      // roll's travel
        check(write_ok(2,trim_position,signed_bits(-5300)) && !write_ok(2,trim_position,signed_bits(-5301)));    // pitch's travel
        check(write_ok(1,target_torque,25) && write_ok(2,target_torque,25) && !write_ok(1,target_torque,50));   // the NG's own torque
        check(write_ok(1,force_scale,191) && !write_ok(1,force_scale,192) && write_ok(2,force_scale,20) && !write_ok(2,force_scale,19));
        check(write_ok(2,manual_force_bias,encode_bias(1075)) && write_ok(2,manual_force_bias,encode_bias(-1075)));
        check(!write_ok(2,manual_force_bias,encode_bias(1076)) && !write_ok(2,manual_force_bias,encode_bias(-1076)));
        check(throws([&]{loading_write_request(1,RegisterWrite{target_torque,50,0},0);}));
        check(loading_write_request(1,RegisterWrite{target_torque,25,0},0)[10]==25);
    }
    check(write_ok(1,target_torque,50));       // restored afterwards
}
void the_directions_come_from_the_preset() {
    check(roll_counts_from_encoder(7)==-7.0 && drive_counts_from_encoder(7)==-7.0);
    auto flipped=builtin_cls_e_ng();flipped.roll_encoder_sign=1;flipped.pitch_encoder_sign=-1;
    flipped.roll_input_reverse=true;flipped.pitch_input_reverse=false;
    const auto d=make_drive(flipped,2150,2250);
    ScopedDrive scope(d);
    check(roll_counts_from_encoder(7)==7.0 && drive_counts_from_encoder(7)==-7.0);
    check(active_drive().roll_input_reverse && !active_drive().pitch_input_reverse);
    flipped.roll_encoder_sign=2;
    check(throws([&]{make_drive(flipped,2150,2250);}));   // only +1 or -1
}
void the_active_drive_is_scoped_and_shared() {
    const Drive* before=&active_drive();
    {
        ScopedDrive scope(ng_drive());
        check(active_drive().model=="cls-e-ng");
        const Drive* inner=&active_drive();
        {ScopedDrive again(ng_drive());check(&active_drive()==inner);}   // an equal drive is the same object, not another copy
        check(&active_drive()==inner);
    }
    check(&active_drive()==before && active_drive().model=="cls-e");
}
}
int main() {
    try {
        the_cls_e_drive_is_the_original_constants();
        the_ng_drive();
        the_ceiling_is_the_lower_of_register_and_spec();
        the_models_scale_with_travel();
        forces_stop_at_the_ceiling();
        the_writes_follow_the_drive();
        the_directions_come_from_the_preset();
        the_active_drive_is_scoped_and_shared();
        std::cout<<"drive tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
