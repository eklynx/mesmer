#include "core.hpp"
#include "reference.hpp"
#include "loading.hpp"
#include "config.hpp"
#include "lab.hpp"
#include "buffet.hpp"
#include "slew.hpp"
#include <cstring>
#include <map>
#include <random>
#include <functional>
#include <iostream>
#include <limits>

using namespace brunner;
void check_at(bool okay,int line) { if (!okay) throw std::runtime_error("check failed line "+std::to_string(line)); }
#define check(x) check_at((x),__LINE__)
void rejects_at(const std::function<void()>& f,int line) { bool threw=false; try { f(); } catch (const std::exception&) {threw=true;} check_at(threw,line); }
#define rejects(...) rejects_at((__VA_ARGS__),__LINE__)
Report reply(unsigned node,Object object,std::uint32_t value,unsigned kind=1) {
    auto r=read_request(node,object,0);
    r[1]=static_cast<std::uint8_t>((kind<<4)|1); r[4]=static_cast<std::uint8_t>(0x80+node); r[5]=0x85;
    r[6]=width(object)==2?0x4b:0x43;
    for (unsigned i=0;i<4;++i) r[10+i]=static_cast<std::uint8_t>(value>>(i*8));
    return r;
}
struct StopTransport {
    std::vector<unsigned> writes,reads;
    unsigned fail_node=0;
    bool remains_enabled=false;
    void ref_write(unsigned node,RegisterWrite,const std::atomic<bool>& cancelled) {
        check(!cancelled);writes.push_back(node);
        if (node==fail_node) throw std::runtime_error("Injected transport failure");
    }
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& cancelled) {
        check(!cancelled && object==Object::status);reads.push_back(node);
        return remains_enabled?0x1437:0x0431;
    }
};
struct SurveyTransport {
    bool fail_io=false;
    unsigned profiles=0,limits=0;
    std::uint32_t ref_read(unsigned,Register reg,const std::atomic<bool>&) {
        if (fail_io) throw std::runtime_error("HID timeout");
        if (reg.index==0x20a1) throw SdoAbort(0x06020000);
        if (reg.index==0x2113) {check(reg.sub<=9);++profiles;return 100;}
        ++limits;return reg.bytes==4?0xffffff9c:4500;
    }
};
struct LoadingTransport {
    std::map<std::uint64_t,std::uint32_t> values;
    std::vector<RegisterWrite> writes,ref_writes;
    std::vector<std::pair<unsigned,std::uint16_t>> order; // every write, either kind: node and object
    bool enabled=false,mismatch=false,fail=false,retain_bias=false;
    bool in_session=false; // a force-feel session's mode (bit 13) may be written back with only bit 4 changed
    std::uint32_t stopped_status=0x0431;
    unsigned running_node=0; // one motor found running, the other off
    unsigned reads=0; // every read, either kind
    static std::uint64_t key(unsigned node,Register reg) {return (std::uint64_t(node)<<32)|(std::uint64_t(reg.index)<<8)|reg.sub;}
    LoadingTransport() {
        for (unsigned node=1;node<=2;++node) {
            values[key(node,force_scale)]=100;values[key(node,force_limit)]=4500;values[key(node,torque_limit)]=4500;
            values[key(node,force_mode)]=0x0205;
            for (unsigned sub=1;sub<=9;++sub) values[key(node,{0x2113,static_cast<std::uint8_t>(sub),2})]=bench_curve[sub-1];
            // Switch-on drag gains (pitch 1/6, roll 1/8, move-back 180) and constant-force cut-off (40).
            values[key(node,force_gain_numerator)]=1;values[key(node,force_gain_denominator)]=node==2?6:8;
            values[key(node,move_back_gain)]=180;values[key(node,manual_offset_off)]=40;
        }
    }
    std::uint32_t read(unsigned node,Object,const std::atomic<bool>&) {++reads;return enabled || running_node==node?0x1437:stopped_status;}
    std::uint32_t ref_read(unsigned node,Register reg,const std::atomic<bool>&) {
        if (fail) throw std::runtime_error("Lost transport");
        ++reads;return values[key(node,reg)];
    }
    void load_write(unsigned node,RegisterWrite op,const std::atomic<bool>&) {
        check(loading_write_allowed(node,op));writes.push_back(op);order.push_back({node,op.reg.index});
        if (!mismatch && !(retain_bias && same_register(op.reg,manual_force_bias))) values[key(node,op.reg)]=op.value;
    }
    void ref_write(unsigned node,RegisterWrite op,const std::atomic<bool>&) {
        ref_writes.push_back(op);order.push_back({node,op.reg.index});
        if (same_register(op.reg,controlword)) {
            check(!(op.value&1));
            if (op.value&0x80) stopped_status=0x431;
        } else check((same_register(op.reg,force_mode) && (!(op.value&0x2000) || (in_session && (op.value&~0x0010u)==0x2205)))
            || (same_register(op.reg,center_mode) && op.value==0));
        if (!same_register(op.reg,controlword) && !mismatch) values[key(node,op.reg)]=op.value;
    }
};
// Decision 16: after every live write, each drive's line (spring, centre and,
// on pitch, constant force) stays within the margin over its whole travel.
struct MarginTransport:LoadingTransport {
    double worst=0,worst_with_shake=0; // the line alone, and the line plus the slow vibration amplitude (buffet.hpp)
    bool kept=true; // pitch never above the constant-force cut-off with a constant force
    double now(unsigned node) {
        const auto travel=node==2?pitch_travel_bound():roll_travel_bound();
        return worst_force_units(static_cast<int>(values[key(node,force_scale)]),signed32(values[key(node,trim_position)]),
            -double(decode_bias(values[key(node,manual_force_bias)])),travel);
    }
    void load_write(unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
        LoadingTransport::load_write(node,op,stop);
        worst=(std::max)(worst,now(node));
        worst_with_shake=(std::max)(worst_with_shake,now(node)+values[key(node,vibration_slow_force)]);
        if (node==2) kept=kept && keeps_constant_force(static_cast<int>(values[key(2,force_scale)]),decode_bias(values[key(2,manual_force_bias)]));
    }
};
int main() {
    try {
        for (unsigned node:{1u,2u}) for (auto object:{Object::firmware,Object::status,Object::encoder,Object::position}) {
            auto r=read_request(node,object,0xabcd);
            check(r[0]==0x3f && r[1]==0x11 && r[2]==0xcd && r[3]==0xab && r[4]==node && r[5]==0x86 && r[6]==0x40);
            for (unsigned kind:{1u,2u}) {
                auto p=reply(node,object,width(object)==2?0x1437:0xffffff9c,kind);
                auto v=read_reply(p,node,object); check(v.has_value());
                check(*v==(width(object)==2?0x1437:0xffffff9c));
                check(!read_reply(p,3-node,object));
            }
        }
        check(signed32(0xffffff9c)==-100); check(signed32(0x80000000)==std::numeric_limits<std::int32_t>::min());
        rejects([]{read_request(3,Object::status,0);});
        rejects([]{read_request(1,static_cast<Object>(0x6040),0);});
        auto good=reply(1,Object::status,0x431);
        for (unsigned bad:{0x17u,0x25u,0x31u,0x01u}) {auto p=good;p[1]=static_cast<std::uint8_t>(bad);rejects([&]{read_reply(p,1,Object::status);});}
        auto p=good;p[0]=0;rejects([&]{read_reply(p,1,Object::status);});
        p=good;p[5]=0x95;rejects([&]{read_reply(p,1,Object::status);});
        p=good;p[6]=0x80;rejects([&]{read_reply(p,1,Object::status);});
        p=good;p[6]=0x43;rejects([&]{read_reply(p,1,Object::status);});
        p=good;p[8]=0x30;check(!read_reply(p,1,Object::status));
        p=good;p[9]=1;check(!read_reply(p,1,Object::status));
        p=good;p[1]=0x12;std::copy(p.begin()+4,p.begin()+14,p.begin()+14);rejects([&]{read_reply(p,1,Object::status);});
        Report empty{};empty[0]=0x3f;check(!read_reply(empty,1,Object::status));

        const auto now=Clock::now();
        Snapshot s; s.connected=true;s.cycles=1;s.sampled=now;
        s.axes={Axis{367,0x1437,0,-50},Axis{367,0x1437,0,100}};
        const std::array<Calibration,2> cal{{{-100,200,true,true},{-100,200,false,true}}};
        auto input=flight_input(s,cal,true,now);check(input.ready && input.roll==.5f && input.pitch==.5f);
        check(!flight_input(s,cal,false,now).ready);
        check(!flight_input(s,cal,true,now+std::chrono::milliseconds(251)).ready);
        check(!flight_input(s,cal,true,now-std::chrono::milliseconds(1)).ready);
        for (auto word:{0x431,0x143f,0x1537}) {auto bad=s;bad.axes[1].status=static_cast<std::uint16_t>(word);check(!flight_input(bad,cal,true,now).ready);}
        auto bad=s;bad.axes[0].firmware=808;check(flight_input(bad,cal,true,now).ready); // firmware never gates; the model's preset and the owner's acknowledgement do
        bad=s;bad.connected=false;check(!flight_input(bad,cal,true,now).ready);
        bad=s;bad.cycles=0;check(!flight_input(bad,cal,true,now).ready);
        auto unverified=cal;unverified[1].verified=false;check(!flight_input(s,unverified,true,now).ready);
        check(normalize(-500,cal[0])==1 && normalize(500,cal[0])==-1);
        rejects([]{normalize(1,Calibration{});});
        // Original-model initialization/stop recovered from the installed vendor code.
        const auto start=reference_start(0x000e,0x0a05);
        check(start.size()==6 && start[0].value==0x0205 && start[1].value==0x000e);
        check(start[2].value==0x008e && start[3].value==0x000f && start[4].value==0x000f && start[5].value==0x2205);
        const auto stop=reference_stop(0x000f,0x2205);
        check(stop.size()==2 && stop[0].value==0x000e && stop[1].value==0x0205);
        for (unsigned word=0;word<65536;++word) {
            const auto off=stopped_controlword(static_cast<std::uint16_t>(word));
            check(!(off&0x81) && (off&6)==6 && (off&~0x87u)==(word&~0x87u));
        }
        const auto write=reference_write_request(2,start[0],0x1234);
        check(write[4]==2 && write[6]==0x2b && write[7]==1 && write[8]==0x21 && write[9]==1 && write[10]==5 && write[11]==2);
        rejects([]{reference_write_request(1,{{0x6071,0,2},20,0},0);});
        rejects([]{reference_write_request(1,{controlword,65536,0},0);});
        rejects([]{reference_write_request(1,{center_mode,2,0},0);});
        rejects([]{reference_write_request(1,{center_position,1,0},0);});
        for (unsigned kind:{1u,2u}) {
            auto ack=reference_write_request(1,{center_mode,1,0},0);
            ack[1]=static_cast<std::uint8_t>((kind<<4)|1);ack[4]=0x81;ack[5]=0x85;ack[6]=0x60;
            check(reference_reply(ack,1,center_mode,true)==0u);
            check(!reference_reply(ack,2,center_mode,true));
            check(!reference_reply(ack,1,force_mode,true));
            rejects([&]{reference_reply(ack,1,center_mode,false);});
            ack[6]=0x4b;check(reference_reply(ack,1,center_mode,false)==1u);
            ack[6]=0x80;rejects([&]{reference_reply(ack,1,center_mode,true);});
        }
        const auto center=reference_write_request(2,{center_position,0,0},0);
        check(center[6]==0x23 && center[7]==0x17 && center[8]==0x21 && center[9]==0);
        const std::array<std::uint16_t,2> words{{0xf,0xf}},modes{{0x2205,0x2205}};
        StopTransport transport;check(stop_reference(transport,words,modes));
        check(transport.writes==std::vector<unsigned>({1,1,1,2,2,2}));
        transport={};transport.fail_node=1;check(!stop_reference(transport,words,modes));
        check(transport.writes==std::vector<unsigned>({1,1,1,2,2,2}) && transport.reads==std::vector<unsigned>({1,2}));
        transport={};transport.remains_enabled=true;check(!stop_reference(transport,words,modes));
        CenterEvidence evidence;
        check(!evidence.observe(10153,now));
        check(!evidence.observe(0,now+std::chrono::milliseconds(100)));
        check(!evidence.observe(0,now+std::chrono::milliseconds(200)));
        check(!evidence.observe(0,now+std::chrono::milliseconds(699)));
        check(evidence.observe(0,now+std::chrono::milliseconds(700)));
        check(!evidence.observe(300,now+std::chrono::milliseconds(710)));
        auto stopped=s;
        for (auto& axis:stopped.axes) {axis.status=0x431;axis.encoder=100;axis.position=99999;}
        check(!stopped_flight_input(stopped,cal,now).ready);
        stopped.session_reference=true;
        auto direct=stopped_flight_input(stopped,cal,now);
        check(direct.ready && direct.roll==-.5f && direct.pitch==.5f); // live encoder, not frozen control-position
        stopped.reference_active=true;check(!stopped_flight_input(stopped,cal,now).ready);stopped.reference_active=false;
        check(!stopped_flight_input(stopped,cal,now+std::chrono::milliseconds(251)).ready);
        stopped.axes[0].status=0x0c31;
        check(stopped_flight_input(stopped,cal,now).ready); // reaching the limit does not lose input
        stopped.axes[0].status=0x0c39;check(!stopped_flight_input(stopped,cal,now).ready); // fault still rejected
        stopped.axes[0].status=0x31;check(!stopped_flight_input(stopped,cal,now).ready);
        for (unsigned sub=1;sub<=18;++sub) {
            Register reg{0x2113,static_cast<std::uint8_t>(sub),2};
            auto read=reference_read_request(1,reg,0);check(read[6]==0x40 && read[9]==sub);
            rejects([&]{reference_write_request(1,{reg,1,0},0);}); // loading survey cannot enable force writes
        }
        rejects([]{reference_read_request(1,{0x2113,19,2},0);});
        SurveyTransport survey;
        const std::atomic<bool> not_stopped{false};
        auto details=inspect_loading(survey,not_stopped);
        check(survey.profiles==18 && survey.limits==18);
        check(details.find("node=2 profile=")!=std::string::npos);
        check(details.find("2115:-100")!=std::string::npos);
        check(details.find("unsupported(0x6020000)")!=std::string::npos);
        survey.fail_io=true;rejects([&]{inspect_loading(survey,not_stopped);});
        // Protection limits are checked, never written, whatever the value.
        for (const auto reg:{force_limit,torque_limit}) for (std::uint32_t value:{0u,100u,101u,4500u})
            rejects([&]{loading_write_request(1,{reg,value,0},0);});
        // The constant force (decision 15): signed 16-bit, within half the protection limit.
        for (std::int32_t bias:{0,1,-1800,max_pitch_bias(),-max_pitch_bias()}) check(loading_write_allowed(2,{manual_force_bias,encode_bias(bias),0}));
        check(loading_write_allowed(1,{manual_force_bias,0,0})); // roll's is only ever cleared
        for (std::int32_t bias:{1,-1,-1800}) rejects([&]{loading_write_request(1,{manual_force_bias,encode_bias(bias),0},0);});
        check(encode_bias(-1800)==0xf8f8u && decode_bias(0xf8f8u)==-1800);
        for (std::uint32_t value:{encode_bias(max_pitch_bias()+1),encode_bias(-max_pitch_bias()-1),0x10000u,0xffffffffu})
            rejects([&]{loading_write_request(2,{manual_force_bias,value,0},0);});
        // Its cut-off, pitch's alone: raised above any strength, or put back to a strength;
        // drag gains only the switch-on value or the untested ladder above it.
        for (std::uint32_t v:{0u,30u,40u,100u,bias_offset_off}) check(loading_write_allowed(2,{manual_offset_off,v,0}));
        check(!loading_write_allowed(2,{manual_offset_off,102,0}) && !loading_write_allowed(1,{manual_offset_off,40,0}) && !loading_write_allowed(1,{manual_offset_off,bias_offset_off,0}));
        check(loading_write_allowed(2,{force_gain_numerator,1,0}) && !loading_write_allowed(2,{force_gain_numerator,2,0}));
        for (std::uint32_t d:{1u,6u}) check(loading_write_allowed(2,{force_gain_denominator,d,0}));
        for (std::uint32_t d:{0u,7u,8u}) check(!loading_write_allowed(2,{force_gain_denominator,d,0}));
        for (std::uint32_t m:{180u,720u}) check(loading_write_allowed(2,{move_back_gain,m,0}));
        for (std::uint32_t m:{0u,179u,721u}) check(!loading_write_allowed(2,{move_back_gain,m,0}));
        check(loading_write_request(2,{move_back_gain,360,0},0)[6]==0x23); // four bytes
        check(loading_write_request(1,{{0x2113,1,2},100,0},0)[6]==0x2b); // the plugin's own curve, bounded
        rejects([]{loading_write_request(1,{{0x2113,1,2},1001,0},0);});
        rejects([]{loading_write_request(1,{{0x2113,1,2},4,0},0);});
        for (std::uint32_t value:{0u,1u,20u,51u,4500u}) rejects([&]{loading_write_request(1,{target_torque,value,0},0);}); // no raw target torque
        check(loading_write_request(2,{target_torque,fixed_target_torque(),0},0)[7]==0x71); // only the owner's fixed 50
        rejects([]{loading_write_request(1,{{0x2113,10,2},1,0},0);});
        rejects([]{loading_write_request(1,{force_scale,static_cast<std::uint32_t>(strongest_spring_percent()+1),0},0);});
        check(loading_write_allowed(1,{force_scale,static_cast<std::uint32_t>(strongest_spring_percent()),0}) && strongest_spring_percent()==400 && strongest_spring_percent()<=1000);
        // Roll's centre may reach the wheel's stops (13000), pitch's the yoke's (12000).
        rejects([]{loading_write_request(1,{trim_position,13001,0},0);});
        check(loading_write_request(1,{trim_position,13000,0},0)[6]==0x23 && loading_write_allowed(1,{trim_position,static_cast<std::uint32_t>(-13000),0}));
        rejects([]{loading_write_request(2,{trim_position,12001,0},0);});
        const auto negative_trim=loading_write_request(2,{trim_position,static_cast<std::uint32_t>(-600),0},0);
        // Regression: simulator trim must address the vendor's SDOTrimPosition
        // (2116), not the independent SDOTrimOffset (2115). Assert literal bytes.
        check(negative_trim[6]==0x23 && negative_trim[7]==0x16 && negative_trim[8]==0x21 && negative_trim[9]==0);
        check(signed32(u32(negative_trim.data()+10))==-600);
        for (auto value:{1u,600u,static_cast<std::uint32_t>(-600)})
            rejects([&]{loading_write_request(2,{{0x2115,0,4},value,0},0);});
        check(loading_write_request(2,{{0x2115,0,4},0,0},0)[7]==0x15);
        // The plugin writes its own spring whatever state the yoke woke in:
        // switch-on (strength 200, its own curves, no bias, reference request
        // set; surveyed 2026-11-07), CLS2Sim's Nov 6 setup, or arbitrary
        // leftovers. Protection limits are never written.
        const auto switch_on=[]{
            LoadingTransport t;
            const std::array<std::uint16_t,9> roll{{40,100,160,220,300,400,500,600,700}},pitch{{100,200,300,400,500,600,700,800,1000}};
            for (unsigned node=1;node<=2;++node) {
                t.values[LoadingTransport::key(node,force_scale)]=200;t.values[LoadingTransport::key(node,force_mode)]=0x2205;
                t.values[LoadingTransport::key(node,target_torque)]=node==1?20:50;
                for (unsigned sub=1;sub<=9;++sub) t.values[LoadingTransport::key(node,{0x2113,static_cast<std::uint8_t>(sub),2})]=(node==1?roll:pitch)[sub-1];
            }
            return t;
        };
        const auto cls2sim=[]{
            LoadingTransport t;t.values[LoadingTransport::key(2,manual_force_bias)]=static_cast<std::uint16_t>(-1800);
            for (unsigned node=1;node<=2;++node) t.values[LoadingTransport::key(node,target_torque)]=50;
            return t;
        };
        const auto leftovers=[]{
            LoadingTransport t;
            for (unsigned node=1;node<=2;++node) {
                t.values[LoadingTransport::key(node,force_scale)]=150;t.values[LoadingTransport::key(node,force_mode)]=0x0a05;
                t.values[LoadingTransport::key(node,trim_offset)]=static_cast<std::uint32_t>(-600);
                t.values[LoadingTransport::key(node,trim_position)]=777;t.values[LoadingTransport::key(node,manual_force_bias)]=1;
                t.values[LoadingTransport::key(node,center_mode)]=1;t.values[LoadingTransport::key(node,target_torque)]=7;
                for (unsigned sub=1;sub<=9;++sub) t.values[LoadingTransport::key(node,{0x2113,static_cast<std::uint8_t>(sub),2})]=999;
            }
            return t;
        };
        const auto settled=[](LoadingTransport& t,std::uint32_t roll,std::uint32_t pitch) {
            for (unsigned node=1;node<=2;++node) {
                check(t.values[LoadingTransport::key(node,force_scale)]==(node==1?roll:pitch));
                check(t.values[LoadingTransport::key(node,manual_force_bias)]==0 && t.values[LoadingTransport::key(node,trim_offset)]==0
                    && t.values[LoadingTransport::key(node,trim_position)]==0);
                for (unsigned sub=1;sub<=9;++sub) check(t.values[LoadingTransport::key(node,{0x2113,static_cast<std::uint8_t>(sub),2})]==bench_curve[sub-1]);
                check(t.values[LoadingTransport::key(node,force_limit)]==4500 && t.values[LoadingTransport::key(node,torque_limit)]==4500);
                check(t.values[LoadingTransport::key(node,center_mode)]==0 && t.values[LoadingTransport::key(node,target_torque)]==fixed_target_torque());
            }
            for (const auto& op:t.writes) check(!same_register(op.reg,force_limit) && !same_register(op.reg,torque_limit));
        };
        using MakeTransport=LoadingTransport(*)();
        const MakeTransport makers[]={switch_on,cls2sim,leftovers};
        for (const auto make:makers) {
            const Tuning flying{30,25,0};
            auto setup=make();
            prepare_loading(setup,not_stopped,flying);settled(setup,30,25);
            verify_loading(setup,1,not_stopped,flying);verify_loading(setup,2,not_stopped,flying);
            check(rest_after_stop(setup));settled(setup,resting_scale,resting_scale);
            auto rest=make();
            rest_controllers(rest,not_stopped);settled(rest,resting_scale,resting_scale);
            auto recover=make();recover.stopped_status=0x2038;
            recover_controllers(recover,not_stopped);settled(recover,resting_scale,resting_scale);check(recover.stopped_status==0x431);
            // Untouched, before any write. Refused (only a switch-on clears it):
            // protection limits that differ from switch-on, or a spring mode the
            // plugin does not know. A fault, never a refusal: a motor found
            // running, on either node, even alongside a refusable setting.
            for (int c=0;c<6;++c) {
                auto t=make();
                if (c==0) t.values[LoadingTransport::key(1,force_limit)]=100;
                if (c==1) t.values[LoadingTransport::key(2,torque_limit)]=4501;
                if (c==2) t.values[LoadingTransport::key(2,force_mode)]=0x0207;
                if (c==3) t.enabled=true;
                if (c==4) t.running_node=2;
                if (c==5) {t.values[LoadingTransport::key(1,force_limit)]=100;t.running_node=2;}
                const bool refusable=c<3;
                for (int step=0;step<3;++step) {
                    auto x=t;bool refused=false,failed=false;
                    try {
                        if (step==0) prepare_loading(x,not_stopped);
                        else if (step==1) rest_controllers(x,not_stopped);
                        else recover_controllers(x,not_stopped);
                    } catch (const SettingsRefused&) {refused=true;} catch (const std::exception&) {failed=true;}
                    check(refused==refusable && failed==!refusable);
                    check(x.writes.empty() && x.ref_writes.empty() && !rest_after_stop(x));
                    // After a session only pitch's constant force is cleared, whatever the checks say.
                    check(x.writes.size()==1 && same_register(x.writes[0].reg,manual_force_bias) && x.writes[0].value==0 && x.ref_writes.empty());
                }
            }
        }
        // A readback that does not confirm stops the setup at that write, and is
        // a failure, not a refusal: something was written.
        auto unconfirmed=switch_on();unconfirmed.mismatch=true;
        bool refusal=false,failed=false;
        try {prepare_loading(unconfirmed,not_stopped);} catch (const SettingsRefused&) {refusal=true;} catch (const std::exception&) {failed=true;}
        check(failed && !refusal && unconfirmed.writes.size()==4); // bias, trim offset, trim, then roll's target torque (20)
        // Regression: a pitch bias that will not clear stops the setup at that
        // write: roll's fourteen settings, then pitch's bias, and nothing after.
        auto bias=cls2sim();bias.retain_bias=true;
        rejects([&]{prepare_loading(bias,not_stopped);});
        check(bias.writes.size()==15 && same_register(bias.writes.back().reg,manual_force_bias));
        auto lost=cls2sim();lost.fail=true;check(!rest_after_stop(lost));
        // Target torque is read back again once the motors run.
        auto drifted=cls2sim();const Tuning flying{30,25,0};prepare_loading(drifted,not_stopped,flying);
        drifted.values[LoadingTransport::key(2,target_torque)]=20;
        verify_loading(drifted,1,not_stopped,flying);rejects([&]{verify_loading(drifted,2,not_stopped,flying);});
        // How a session that was not refused ends: failed before any motor ran
        // keeps the reference (force feel unavailable); after, it is a fault.
        {
            const auto ok=classify_session_end(false,false,false,true);check(ok.keep_reference && !ok.fault && !ok.force_unavailable);
            const auto early=classify_session_end(true,false,false,false);check(early.keep_reference && !early.fault && early.force_unavailable);
            const auto late=classify_session_end(true,false,false,true);check(!late.keep_reference && late.fault && !late.force_unavailable);
            const auto paused=classify_session_end(true,true,false,true);check(paused.keep_reference && !paused.fault && !paused.force_unavailable);
            const auto quitting=classify_session_end(true,false,true,true);check(!quitting.fault && !quitting.force_unavailable);
        }
        // Pitch and roll after a refusal, through the input check X-Plane's
        // axes actually use: a refusal keeps them; any other failure does not.
        {
            Snapshot flying_yoke;flying_yoke.connected=true;flying_yoke.cycles=1;flying_yoke.sampled=now;flying_yoke.session_reference=true;
            flying_yoke.axes={Axis{367,0x0431,0,0},Axis{367,0x0431,0,0}};
            check(stopped_flight_input(flying_yoke,cal,now).ready);
            auto refused=flying_yoke;refused.reference_active=true;record_force_refusal(refused,true,"Protection limits differ from switch-on");
            check(stopped_flight_input(refused,cal,now).ready && refused.force_unavailable=="Force feel unavailable - switch the yoke off and on" && !refused.loading_fault);
            auto centring_refused=flying_yoke;record_request_failure(centring_refused,true,"Centring failed: limits");
            check(stopped_flight_input(centring_refused,cal,now).ready && centring_refused.power_cycle_needed && centring_refused.failures==1);
            record_request_failure(centring_refused,true,"Recovery failed: limits");check(centring_refused.failures==2);
            auto broken=flying_yoke;record_request_failure(broken,false,"Centring failed: timeout");
            check(!stopped_flight_input(broken,cal,now).ready && !broken.power_cycle_needed && broken.loading_fault);
        }
        stopped.axes[0].status=0x1437;stopped.axes[1].status=0x1437;stopped.loading_active=true;
        check(stopped_flight_input(stopped,cal,now).ready);
        stopped.axes[0].status=0x143f;check(!stopped_flight_input(stopped,cal,now).ready);
        for (unsigned status:{0x431u,0x438u,0x470u,0x2038u}) check(stopped_for_recovery(status));
        for (unsigned status:{0x1437u,0x0000u,0x0437u,0x000fu}) check(!stopped_for_recovery(status));
        check(base_spring_mode(0x2205)==spring_mode() && base_spring_mode(0x0a05)==spring_mode() && base_spring_mode(0x0207)!=spring_mode());
        check(advance_trim(0,600,0.02)==12);
        check(advance_trim(0,-600,0.02)==-12);
        check(advance_trim(595,600,0.02)==600);
        check(advance_trim(-595,-600,0.02)==-600);
        check(advance_trim(0,600,10)==30);
        check(advance_trim(0,600,0)==0);
        rejects([]{advance_trim(0,601,0.02);});
        rejects([]{advance_trim(601,0,0.02);});
        rejects([]{advance_trim(0,600,std::numeric_limits<double>::quiet_NaN());});
        rejects([]{advance_trim(0,600,-0.01);});
        Tuning requested;
        check(set_tuning(requested,TuningField::roll_percent,20));
        check(set_tuning(requested,TuningField::pitch_percent,100));
        check(set_tuning(requested,TuningField::pitch_trim,-600));
        check(tuning_value(requested,TuningField::roll_percent)==20 && requested.pitch_percent==100 && requested.pitch_trim==-600);
        const auto unchanged=requested;
        for (int value:{-1,0,19,101,std::numeric_limits<int>::max()}) {
            check(!set_tuning(requested,TuningField::roll_percent,value));
            check(same_tuning(requested,unchanged));
        }
        check(!set_tuning(requested,TuningField::pitch_percent,101));
        check(!set_tuning(requested,TuningField::pitch_trim,601));
        check(!set_tuning(requested,TuningField::pitch_trim,-601));
        check(!set_tuning(requested,static_cast<TuningField>(99),40));
        check(same_tuning(requested,unchanged));
        TuningRamp ramp({40,40,0});
        Tuning ramped;
        for (int i=0;i<100;++i) ramped=ramp.advance({60,20,600},0.02);
        check(same_tuning(ramped,{60,20,600})); // 20 percentage points in two seconds, independent directions
        check(same_tuning(ramp.advance({60,20,600},100),ramped)); // idle time cannot bank a jump
        const auto first=ramp.advance({100,100,-600,-900},10);
        check(first.roll_percent==60 && first.pitch_percent==20 && first.pitch_trim==570 && first.pitch_bias==-100); // 30 counts, 100 units after a stall
        const auto second=ramp.advance({100,100,-600,-900},10);
        check(second.roll_percent==61 && second.pitch_percent==21 && second.pitch_trim==540 && second.pitch_bias==-200);
        TuningRamp slow({20,20,0});Tuning crept;
        for (int i=0;i<50;++i) crept=slow.advance({20,20,3000,-912},0.02);
        check(crept.pitch_trim==600 && crept.pitch_bias==-912); // the light spring test's 600 counts a second
        // A force-feel session follows the model with no added lag; only force
        // feel coming on and an edit of the feel blend in, over half a second.
        {
            // It starts where the drive is: no pitch force, the softest spring
            // the drive takes centred at the yoke, not the firmest spring at 0
            // (which pulled a yoke on a stop away). Never 0: the drive refuses it.
            // Roll the same, centred where the wheel is (decision 15).
            const auto engaged_from=session_start({30,40,0,0},6000.4,-2500.6);
            check(engaged_from.roll_percent==softest_spring_percent && engaged_from.roll_trim==-2501
                && engaged_from.pitch_percent==softest_spring_percent && engaged_from.pitch_trim==6000 && engaged_from.pitch_bias==0);
            check(valid_tuning(engaged_from) && !valid_tuning({30,0,0,0}) && !valid_tuning({30,softest_spring_percent-1,0,0}));
            check(session_start({30,40,0,0},std::numeric_limits<double>::quiet_NaN(),NAN).pitch_trim==0 && session_start({30,40,0,0},20000,0).pitch_trim==12000);
            check(session_start({30,40,0,0},0,NAN).roll_trim==0 && session_start({30,40,0,0},0,-20000).roll_trim==-13000);
            Tuning past_stop{};past_stop.roll_trim=13001;check(!valid_tuning(past_stop));
            // What the Windows session writes first: the same values with the
            // trims held back for after the springs (it once kept its own 0%).
            const auto load=session_load({30,40,0,0},6000.4,-2500.6);
            check(load.centre==6000 && load.roll_centre==-2501 && load.written.pitch_percent==softest_spring_percent && load.written.pitch_trim==0
                && load.written.pitch_bias==0 && load.written.roll_percent==softest_spring_percent && load.written.roll_trim==0 && valid_tuning(load.written));
            FeelBlend blend(engaged_from,5);
            const Tuning parked{30,20,12000,-912};
            auto e=blend.advance(parked,5,0.02); // a 50 Hz update: a twenty-fifth of the blend
            check(blend.blending() && e.pitch_percent==20 && e.pitch_trim==6240 && e.pitch_bias==-36 && e.roll_percent==20 && e.roll_trim==-2401);
            // A stall, however long, moves at most an eighth of the way (the
            // review's probe: a 0.25 s stall wrote 7200 -> 10200 in one update).
            e=blend.advance(parked,5,0.25);
            check(blend.blending() && e.pitch_percent==20 && e.pitch_trim==6990 && e.pitch_bias==-150);
            int updates=2;
            for (auto last=e;blend.blending() && updates<20;++updates,last=e) {
                e=blend.advance(parked,5,10.0);
                check(std::abs(e.pitch_trim-last.pitch_trim)<=6000/8+1 && std::abs(e.pitch_bias-last.pitch_bias)<=912/8+1);
            }
            check(!blend.blending() && same_tuning(e,parked) && updates==9); // seven eighths after the first two
            // Then what the aircraft does reaches the drive in the same update,
            // however far it moves the rest or the force.
            check(same_tuning(blend.advance({30,20,-6000,-500},5,0.02),{30,20,-6000,-500}));
            check(same_tuning(blend.advance({45,55,7000,2250},5,0),{45,55,7000,2250}));
            // An edit (the workbench's count moves) blends from the line the
            // drive has: the reviewer's probe, parked, unbalance 0 to 30 in·lb.
            const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
            auto light=c172;light.unbalance_inlb=0;
            const ElevatorInputs still{0,0,1};
            const auto before=solved_tuning({30,40,0,0},solve_pitch(light,still,0,222)),after=solved_tuning({30,40,0,0},solve_pitch(c172,still,0,222));
            check(std::abs(after.pitch_bias-before.pitch_bias)>500); // what the solver alone would send in one update
            FeelBlend edits(before,7);
            check(same_tuning(edits.advance(before,7,0.1),before));
            e=edits.advance(after,8,0.1);
            check(edits.blending() && std::abs(e.pitch_bias-before.pitch_bias)<=std::abs(after.pitch_bias-before.pitch_bias)/8+1);
            for (int i=0;i<7;++i) {check(edits.blending());e=edits.advance(after,8,0.1);}
            check(!edits.blending() && same_tuning(e,after));
            rejects([&]{blend.advance({30,strongest_spring_percent()+1,0,0},5,0.02);});
            rejects([&]{blend.advance({30,40,0,max_pitch_bias()+1},5,0.02);});
            rejects([&]{blend.advance({30,40,0,0},5,std::numeric_limits<double>::quiet_NaN());});
            rejects([]{FeelBlend({19,40,0,0},0);});
        }
        check(!valid_tuning({20,20,0,max_pitch_bias()+1}) && valid_tuning({20,20,0,-max_pitch_bias()}));
        rejects([&]{ramp.advance({strongest_spring_percent()+1,40,0},0.02);});
        rejects([&]{ramp.advance({},std::numeric_limits<double>::quiet_NaN());});
        rejects([&]{ramp.advance({},-0.1);});
        LoadingTransport live;prepare_loading(live,not_stopped);live.writes.clear();
        Tuning applied;
        rejects([&]{apply_live_tuning(live,applied,{41,39,-12},not_stopped);});
        check(live.writes.empty() && same_tuning(applied,Tuning{})); // no live changes with either drive disabled
        live.enabled=true;
        apply_live_tuning(live,applied,{41,39,-12},not_stopped);
        check(same_tuning(applied,{41,39,-12}) && live.writes.size()==3);
        verify_loading(live,1,not_stopped,applied);verify_loading(live,2,not_stopped,applied);
        check(live.values[LoadingTransport::key(1,trim_position)]==0);
        check(signed32(live.values[LoadingTransport::key(2,trim_position)])==-12);
        for (const auto& op:live.writes) check(same_register(op.reg,force_scale) || same_register(op.reg,trim_position));
        check(live.values[LoadingTransport::key(2,{0x2115,0,4})]==0);
        live.writes.clear();
        live.values[LoadingTransport::key(2,{0x2115,0,4})]=1;
        rejects([&]{verify_loading(live,2,not_stopped,applied);});
        rejects([&]{apply_live_tuning(live,applied,{42,40,0},not_stopped);});
        check(live.writes.empty() && same_tuning(applied,{41,39,-12}));
        live.values[LoadingTransport::key(2,{0x2115,0,4})]=0;
        live.values[LoadingTransport::key(2,manual_force_bias)]=static_cast<std::uint16_t>(-1800);
        rejects([&]{verify_loading(live,2,not_stopped,applied);});
        rejects([&]{apply_live_tuning(live,applied,{42,40,0},not_stopped);});
        check(live.writes.empty());
        live.values[LoadingTransport::key(2,manual_force_bias)]=0;
        apply_live_tuning(live,applied,applied,not_stopped);check(live.writes.empty());
        // Roll's centre moves under its deadband without a write, or even a
        // read, so pitch keeps its update rate; when roll is written, every
        // pitch write in that update has gone first.
        {
            using Op=std::pair<unsigned,std::uint16_t>;
            LoadingTransport d;prepare_loading(d,not_stopped);d.enabled=true;d.writes.clear();d.order.clear();
            Tuning at;const auto reads=d.reads;
            Tuning moved=at;moved.roll_trim=roll_centre_deadband-1;
            check(!update_live_tuning(d,at,moved,not_stopped));
            moved.roll_trim=-(roll_centre_deadband-1);
            check(!update_live_tuning(d,at,moved,not_stopped));
            check(d.writes.empty() && d.reads==reads && at.roll_trim==0);
            moved.pitch_trim=7; // something else to write: the held centre still is not
            check(update_live_tuning(d,at,moved,not_stopped) && d.writes.size()==1 && (d.order.back()==Op{2,trim_position.index})
                && at.roll_trim==0 && at.pitch_trim==7);
            d.writes.clear();d.order.clear();
            const Tuning all{41,41,9,-40,roll_centre_deadband};
            check(update_live_tuning(d,at,all,not_stopped) && same_tuning(at,all) && d.order.size()==5);
            for (std::size_t i=0;i<3;++i) check(d.order[i].first==2);
            check((d.order[3]==Op{1,force_scale.index}) && (d.order[4]==Op{1,trim_position.index}));
            check(signed32(d.values[LoadingTransport::key(1,trim_position)])==roll_centre_deadband);
            {const Tuning zero{41,41,9,-40,0},one{41,41,9,-40,1};check(hold_roll_centre(at,zero).roll_trim==0 && hold_roll_centre(at,one).roll_trim==roll_centre_deadband);}
        }
        {
            // Decision 16: no write leaves a drive asking for more than the
            // margin anywhere in its travel, pitch's constant force counted:
            // whatever the update, in whatever order its registers go.
            check(drive_margin_units()<protection_limit_units() && strongest_spring_percent()==400);
            check(!valid_tuning({20,100,12000,-max_pitch_bias()}) && valid_tuning({20,87,12000,-max_pitch_bias()}) && !valid_tuning({20,88,12000,-max_pitch_bias()}));
            check(valid_tuning({400,400,0,0}) && !valid_tuning({400,400,1,0}) && !valid_tuning({20,400,0,-1}));
            Tuning wheel_off{};wheel_off.roll_percent=400;wheel_off.roll_trim=1;check(!valid_tuning(wheel_off));
            check(within_drive(wheel_off).roll_percent==399 && valid_tuning(within_drive(wheel_off)));
            std::mt19937 rng(16);
            const auto pick=[&](int lo,int hi) {return std::uniform_int_distribution<int>(lo,hi)(rng);};
            const auto any=[&]() {
                for (;;) {
                    Tuning t;t.pitch_percent=pick(20,400);t.pitch_trim=pick(-12000,12000);t.pitch_bias=pick(-max_pitch_bias(),max_pitch_bias());
                    t.roll_percent=pick(20,400);t.roll_trim=pick(-13000,13000);
                    if (pick(0,3)==0) t.pitch_trim=pick(0,1)?12000:-12000; // at a stop, as a parked solution is
                    if (pick(0,3)==0) {t.pitch_trim=t.roll_trim=0;t.pitch_bias=0;}
                    if (valid_tuning(t)) return t;
                }
            };
            MarginTransport m;prepare_loading(m,not_stopped);m.enabled=true;m.worst=0;
            Tuning at;int writes=0;
            for (int i=0;i<3000;++i) {
                const auto next=any();
                apply_live_tuning(m,at,next,not_stopped);
                check(same_tuning(at,next) && m.worst<=drive_margin_units() && m.kept);
                writes+=int(m.writes.size());m.writes.clear();
            }
            check(writes>3000*3);
            // The hardest turns: firmest at the centre to softest with all the
            // constant force at the far stop, and back.
            const Tuning firm{400,400,0,0},parked{20,20,12000,-max_pitch_bias()};
            apply_live_tuning(m,at,firm,not_stopped);apply_live_tuning(m,at,parked,not_stopped);apply_live_tuning(m,at,firm,not_stopped);
            check(same_tuning(at,firm) && m.worst<=drive_margin_units() && m.kept);
            // Firm with no constant force to a constant force: the strength
            // comes down to the cut-off before the constant force goes on.
            const Tuning pushed{20,90,12000,-400},strong{20,300,0,0};
            apply_live_tuning(m,at,strong,not_stopped);apply_live_tuning(m,at,pushed,not_stopped);apply_live_tuning(m,at,strong,not_stopped);
            check(same_tuning(at,strong) && m.kept && m.worst<=drive_margin_units());
            check(!valid_tuning({20,strongest_with_constant_force_percent+1,12000,-1}) && valid_tuning({20,strongest_with_constant_force_percent,12000,-1})
                && valid_tuning({20,strongest_with_constant_force_percent+1,0,0}) && strongest_with_constant_force_percent==int(bias_offset_off)-1);
            check(within_drive({20,200,12000,-400}).pitch_percent==strongest_with_constant_force_percent);
            // A blend between two tunings within the margin can sit over it;
            // FeelBlend lowers the strength, so every step stays inside.
            for (int i=0;i<500;++i) {
                const auto from=any(),to=any();
                FeelBlend b(from,0);Tuning step=from;
                for (int k=0;k<10;++k) {step=b.advance(to,1,0.05);check(valid_tuning(step));}
                check(same_tuning(step,to));
                update_live_tuning(m,at,step,not_stopped);check(m.worst<=drive_margin_units() && m.kept);
            }
            // Roll's centre held inside its deadband while its strength rose
            // to a limit set by the new centre: the strength gives way.
            apply_live_tuning(m,at,firm,not_stopped);
            Tuning back=at;back.roll_trim=-(roll_centre_deadband-1);back.roll_percent=20;apply_live_tuning(m,at,back,not_stopped);
            check(valid_tuning(firm)); // 400 % is within the margin only centred
            update_live_tuning(m,at,firm,not_stopped); // the held centre (-19) with 400 % would be over: lowered to 399
            check(at.roll_trim==-(roll_centre_deadband-1) && at.roll_percent==399 && valid_tuning(at) && m.worst<=drive_margin_units());
            // Buffet's reserve (decision 19 within decision 16): with its knob
            // on, spring + constant force + shake stays within the margin on
            // each axis; with it off, everything is as without buffet.
            check(drive_budget_units(0)==drive_margin_units() && drive_budget_units(450)==3550 && max_drive_reserve_units()==1350
                && drive_budget_units(-1)==drive_margin_units()-max_drive_reserve_units());
            const auto centred=[](int) {return std::pair<double,double>{0.0,0.0};};
            check(drive_ceiling(pitch_travel_bound(),centred)==400 && drive_ceiling(pitch_travel_bound(),centred,0)==400);
            check(drive_ceiling(pitch_travel_bound(),centred,450)==355 && drive_ceiling(roll_travel_bound(),centred,45)==395); // the ceiling drops by the reserve
            // A non-finite want is the softest spring, never lround(NaN); a finite one rounds within the floor and the ceiling.
            check(spring_percent(std::nan(""),400)==softest_spring_percent && spring_percent(HUGE_VAL,400)==softest_spring_percent
                && spring_percent(-HUGE_VAL,400)==softest_spring_percent);
            check(spring_percent(-5,400)==softest_spring_percent && spring_percent(0,400)==softest_spring_percent && spring_percent(57.4,400)==57
                && spring_percent(1e12,400)==400 && spring_percent(57.4,30)==30 && spring_percent(57.4,10)==softest_spring_percent);
            {
                // X-Plane's stalled share, slewed at full scale in 0.25 s on
                // the flight clock: it starts at its first reading, holds
                // through a NaN, a missing reading and a paused clock, and
                // restarts when the clock runs backwards.
                const auto same=[](double got,double want) {return std::abs(got-want)<1e-12;};
                ShareSlew share_slew;
                check(std::isnan(share_slew.at(std::nan(""),10)));        // nothing yet: passes through
                check(same(share_slew.at(1,10),1));                      // the first reading, at once
                check(same(share_slew.at(0,10.05),0.8) && same(share_slew.at(0,10.1),0.6));
                check(same(share_slew.at(0,10.1),0.6));                  // the same frame again: no time, no move
                check(same(share_slew.at(std::nan(""),10.15),0.6));     // NaN holds
                check(same(share_slew.at(0,10.2),0.4));                  // and its clock counted: 0.05 s since
                check(same(share_slew.at(0.5,10.5),0.5));                // reaches the reading, no overshoot
                check(same(share_slew.at(3,10.55),0.7));                 // a reading over 1 is 1
                check(same(share_slew.at(0,1),0));                       // the clock ran backwards: a new flight
                check(same(share_slew.at(1,HUGE_VAL),1) && same(share_slew.at(0,std::nan("")),0)); // a bad clock restarts
                share_slew.reset();check(same(share_slew.at(0.3,20),0.3));
                check(std::isnan(share_slew.at(std::nan(""),19)) && same(share_slew.at(0.9,19.01),0.9)); // a gap across a new flight forgets it
                check(std::isnan(share_slew.at(std::nan(""),std::nan(""))) && same(share_slew.at(0.2,30),0.2)); // as does a bad clock
                // The sample's three shares, each its own; everything else as read.
                StallSlews stall_slews;
                FlightSample raw_sample;raw_sample.physics_time=5;raw_sample.trim=0.25;
                raw_sample.tail=AileronSide{600,4,1};raw_sample.ailerons=AileronInputs{{500,3,0},{500,3,1}};
                auto slewed_sample=stall_slews.slewed(raw_sample);
                check(same(slewed_sample.tail->stalled,1) && same(slewed_sample.ailerons->left.stalled,0) && same(slewed_sample.ailerons->right.stalled,1));
                raw_sample.physics_time=5.1;raw_sample.tail->stalled=0;raw_sample.ailerons->left.stalled=1;raw_sample.ailerons->right.stalled=1;
                slewed_sample=stall_slews.slewed(raw_sample);
                check(same(slewed_sample.tail->stalled,0.6) && same(slewed_sample.ailerons->left.stalled,0.4) && same(slewed_sample.ailerons->right.stalled,1));
                check(same(raw_sample.tail->stalled,0) && same(slewed_sample.trim,0.25) && same(slewed_sample.tail->alpha_deg,4) && same(slewed_sample.ailerons->left.pa,500));
                // A frame without the tail holds its share and counts the
                // clock: back, it moves on from 0.6 at the same pace, no step.
                raw_sample.tail.reset();raw_sample.physics_time=5.2;
                slewed_sample=stall_slews.slewed(raw_sample);
                check(!slewed_sample.tail && same(stall_slews.tail.value,0.6) && same(slewed_sample.ailerons->left.stalled,0.8));
                raw_sample.tail=AileronSide{600,4,1};raw_sample.physics_time=5.25;
                check(same(stall_slews.slewed(raw_sample).tail->stalled,0.8));
                // Likewise the ailerons, both at 1 by 5.25: held through the
                // gap, then 0.2 down in 0.05 s, not straight to 0.
                raw_sample.ailerons.reset();raw_sample.physics_time=5.3;
                slewed_sample=stall_slews.slewed(raw_sample);
                check(!slewed_sample.ailerons && same(stall_slews.left.value,1) && same(stall_slews.right.value,1));
                raw_sample.ailerons=AileronInputs{{500,3,0},{500,3,0}};raw_sample.physics_time=5.35;
                slewed_sample=stall_slews.slewed(raw_sample);
                check(same(slewed_sample.ailerons->left.stalled,0.8) && same(slewed_sample.ailerons->right.stalled,0.8));
            }
            const DriveReserve full{450,450},none{};
            check(valid_tuning(firm) && valid_tuning(firm,none) && !valid_tuning(firm,full));
            check(within_drive(firm,full).roll_percent==355 && within_drive(firm,full).pitch_percent==355 && valid_tuning(within_drive(firm,full),full));
            check(within_drive(firm,DriveReserve{0,450}).roll_percent==400 && within_drive(firm,DriveReserve{0,450}).pitch_percent==355);
            {
                const auto r=buffet_reserve(BuffetKnob{true,10,80});const auto o=buffet_reserve(BuffetKnob{});
                check(r.roll==45 && r.pitch==45 && o.roll==0 && o.pitch==0 && buffet_reserve(BuffetKnob{true,100,80}).pitch==450);
                check(buffet_reserve(BuffetKnob{true,101,80}).pitch==0); // an invalid knob is off
            }
            // Knob off: the same answers and the same writes as no reserve at all.
            for (int i=0;i<2000;++i) {
                Tuning raw;raw.pitch_percent=pick(20,400);raw.pitch_trim=pick(-12000,12000);raw.pitch_bias=pick(-max_pitch_bias(),max_pitch_bias());
                raw.roll_percent=pick(20,400);raw.roll_trim=pick(-13000,13000);
                check(valid_tuning(raw,none)==valid_tuning(raw) && same_tuning(within_drive(raw,none),within_drive(raw)));
                const auto to=any();
                const AxisState a{raw.pitch_percent,raw.pitch_trim,raw.pitch_bias},b{to.pitch_percent,to.pitch_trim,to.pitch_bias};
                const auto plain=axis_writes(a,b,pitch_travel_bound()),reserved=axis_writes(a,b,pitch_travel_bound(),0);
                check(plain.size()==reserved.size());
                for (std::size_t j=0;j<plain.size() && j<reserved.size();++j) check(plain[j].reg==reserved[j].reg && plain[j].value==reserved[j].value);
            }
            {
                MarginTransport plain,offed;prepare_loading(plain,not_stopped);prepare_loading(offed,not_stopped);plain.enabled=offed.enabled=true;
                Tuning at_plain,at_off;const BuffetDrive idle;
                for (int i=0;i<500;++i) {
                    const auto next=any();
                    const bool wrote_plain=update_live_tuning(plain,at_plain,next,not_stopped);
                    const bool wrote_off=update_live_tuning(offed,at_off,next,not_stopped,idle.reserve(BuffetKnob{}));
                    check(wrote_plain==wrote_off && same_tuning(at_plain,at_off));
                }
                check(plain.writes.size()==offed.writes.size() && plain.order==offed.order);
                for (std::size_t j=0;j<plain.writes.size() && j<offed.writes.size();++j)
                    check(same_register(plain.writes[j].reg,offed.writes[j].reg) && plain.writes[j].value==offed.writes[j].value);
            }
            // Knob on, as a session runs it: the reserve first, the spring
            // writes within the margin less it, then the shake within what is
            // left. After every single write, each drive's line plus its
            // shake stays within the margin, the knob turned on, up, down and
            // off on the way.
            {
                MarginTransport d;prepare_loading(d,not_stopped);d.enabled=true;d.in_session=true;
                for (unsigned node=1;node<=2;++node) d.values[LoadingTransport::key(node,force_mode)]=0x2205;
                d.worst=d.worst_with_shake=0;
                const std::array<std::uint16_t,2> held_modes{{0x2205,0x2205}};
                Tuning at_d;BuffetDrive shaker;std::uint32_t most=0;
                for (int i=0;i<3000;++i) {
                    const auto next=any();
                    const BuffetKnob knob=(i/150)%4==3?BuffetKnob{}:BuffetKnob{true,pick(0,100),80};
                    const auto r=shaker.reserve(knob);
                    update_live_tuning(d,at_d,next,not_stopped,r);
                    check(valid_tuning(at_d,r));
                    shaker.update(d,BuffetTarget{knob,static_cast<std::uint32_t>(pick(0,450)),static_cast<std::uint32_t>(pick(0,450))},at_d,held_modes,not_stopped);
                    most=(std::max)(most,(std::max)(shaker.units()[0],shaker.units()[1]));
                }
                check(d.worst_with_shake<=drive_margin_units() && d.kept && most>=300);
            }
        }
        live.mismatch=true;
        rejects([&]{apply_live_tuning(live,applied,{42,40,0},not_stopped);});
        check(live.writes.size()==1 && same_tuning(applied,{41,39,-12})); // unconfirmed readback cannot become applied telemetry
        live.mismatch=false;live.enabled=false;
        live.values[LoadingTransport::key(2,{0x2115,0,4})]=static_cast<std::uint32_t>(-600);
        recover_controllers(live,not_stopped); // any live state is recoverable while stopped
        check(live.values[LoadingTransport::key(1,force_scale)]==resting_scale);
        check(live.values[LoadingTransport::key(2,trim_position)]==0);
        check(live.values[LoadingTransport::key(2,{0x2115,0,4})]==0);
        // Schema 2, the oldest still read: the shared floor and pressure are roll's.
        const std::string flight_config="auto_start=1\nreference_airspeed_knots=316\nminimum_spring_percent=20\nmaximum_trim_counts=6000\n";
        const std::string config="schema_version=2\nroll_spring_percent=35\npitch_spring_percent=40\npitch_trim_counts=0\n"+flight_config;
        std::istringstream startup(config);
        const auto managed=parse_configuration(startup);
        // Schema 6 retired each axis's firmest (decision 16): the springs stay
        // the commissioning channel's defaults whatever an old file says.
        check(same_tuning(managed.tuning,{40,40,0}) && managed.flight.auto_start==1
            && managed.flight.reference_airspeed==316 && managed.flight.roll_minimum_percent==20);
        // The firmness pitch's and roll's retired floor (decision 6), logged and ignored.
        check((managed.retired==std::vector<std::string>{"maximum_trim_counts","minimum_spring_percent","pitch_spring_percent","roll_spring_percent"}));
        std::istringstream crlf("# managed\r\n schema_version = 2\r\nroll_spring_percent=20\r\npitch_spring_percent=100\r\npitch_trim_counts=-600\r\n"+flight_config);
        check(same_tuning(parse_configuration(crlf).tuning,{40,40,-600}));
        // Buffet: off unless the file turns it on; its knobs held to their bounds.
        check(managed.flight.buffet==0 && managed.flight.buffet_percent==10 && managed.flight.buffet_period==80);
        {
            std::istringstream on(config+"buffet=1\nbuffet_percent=5\nbuffet_period=120\n");
            const auto c=parse_configuration(on);
            check(c.flight.buffet==1 && c.flight.buffet_percent==5 && c.flight.buffet_period==120);
            for (const auto* refused_line:{"buffet=2\n","buffet_percent=101\n","buffet_period=9\n","buffet_period=141\n"})
                rejects([&]{std::istringstream stream(config+refused_line);parse_configuration(stream);});
        }
        for (const auto& invalid:{std::string(),config+"roll_spring_percent=40\n",config+"auto_enable=1\n",config+"malformed\n",
                std::string("schema_version=2\nroll_spring_percent=35\npitch_spring_percent=40\npitch_trim_counts=0\n")}) {
            rejects([&]{std::istringstream stream(invalid);parse_configuration(stream);});
        }
        // A retired firmest is ignored at any integer, still read as one.
        for (const auto& value:{"101","0","400"}) {
            auto old=config;old.replace(old.find("35"),2,value);std::istringstream stream(old);check(same_tuning(parse_configuration(stream).tuning,{40,40,0}));
        }
        for (const auto& value:{"40oops","2147483648","", "40.5"}) {
            auto invalid=config;invalid.replace(invalid.find("35"),2,value);
            rejects([&]{std::istringstream stream(invalid);parse_configuration(stream);});
        }
        for (const auto& replacement:std::array<std::pair<std::string,std::string>,5>{{
            {"auto_start=1","auto_start=2"},{"knots=316","knots=0"},{"knots=316","knots=401"},{"pitch_trim_counts=0","pitch_trim_counts=601"},
            {"schema_version=2","schema_version=1"}}}) {
            auto invalid=config;invalid.replace(invalid.find(replacement.first),replacement.first.size(),replacement.second);
            rejects([&]{std::istringstream stream(invalid);parse_configuration(stream);});
        }
        {
            // Main's schema 3, as Install-Brunner wrote it before the elevator
            // model: the new plugin still starts on it (deployment safety).
            const std::string main3="schema_version=3\nroll_spring_percent=40\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\n"
                "full_trim_pa=51\npitch_half_gain_pa=200\nroll_half_gain_pa=200\nreference_airspeed_knots=316\npitch_minimum_spring_percent=20\n"
                "roll_minimum_spring_percent=20\nmaximum_trim_counts=6000\nforce_feel=0\nplain_spring_percent=20\nelevator_unbalance_inlb=30\nworkbench_port=8087\n";
            std::istringstream old(main3);const auto c=parse_configuration(old);
            check(c.retired==std::vector<std::string>({"elevator_unbalance_inlb","full_trim_pa","maximum_trim_counts","pitch_half_gain_pa","pitch_minimum_spring_percent",
                "pitch_spring_percent","plain_spring_percent","roll_half_gain_pa","roll_minimum_spring_percent","roll_spring_percent"}));
            check(c.workbench_port==8087 && c.flight.roll_torque_units_per_nm==1000 && c.flight.pitch_force_units_per_lb==222 && c.flight.pitch_force_gain_divisor==6);
            // Schema 4: the drive's calibration and drag are required.
            const std::string v4="schema_version=4\nroll_spring_percent=40\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\nreference_airspeed_knots=316\n"
                "roll_minimum_spring_percent=20\nroll_half_gain_pa=200\nforce_feel=0\nplain_spring_percent=20\nworkbench_port=8087\n"
                "pitch_force_units_per_lb=300\npitch_force_gain_divisor=3\npitch_move_back_gain=360\n";
            std::istringstream fresh(v4);const auto d=parse_configuration(fresh);
            // Roll's airflow and plain springs are retired (decision 6): logged, ignored.
            check((d.retired==std::vector<std::string>{"pitch_spring_percent","plain_spring_percent","roll_half_gain_pa","roll_minimum_spring_percent","roll_spring_percent"}));
            check(d.flight.roll_torque_units_per_nm==1000 && d.flight.pitch_force_units_per_lb==300 && d.flight.pitch_force_gain_divisor==3 && d.flight.pitch_move_back_gain==360);
            for (const char* key:{"pitch_force_units_per_lb=300\n","pitch_force_gain_divisor=3\n","pitch_move_back_gain=360\n"}) {
                auto missing=v4;missing.erase(missing.find(key),std::strlen(key));
                rejects([&]{std::istringstream stream(missing);parse_configuration(stream);});
            }
            for (const char* bad_line:{"pitch_force_units_per_lb=49\n","pitch_force_gain_divisor=7\n","pitch_move_back_gain=179\n"}) {
                auto changed=v4;const std::string line(bad_line);const auto key=line.substr(0,line.find('=')+1);
                const auto at=changed.find(key);changed.replace(at,changed.find('\n',at)-at+1,line);
                rejects([&]{std::istringstream stream(changed);parse_configuration(stream);});
            }
            std::istringstream leftover(v4+"elevator_model=1\n");check(parse_configuration(leftover).retired.front()=="elevator_model");
            rejects([&]{std::istringstream stream(v4+"pitch_floor=20\n");parse_configuration(stream);});
            // Schema 5: roll's drive scale for the aileron model is required.
            const std::string body=v4.substr(v4.find('\n')+1);
            rejects([&]{std::istringstream stream("schema_version=5\n"+body);parse_configuration(stream);});
            std::istringstream v5("schema_version=5\n"+body+"roll_torque_units_per_nm=1500\n");
            check(parse_configuration(v5).flight.roll_torque_units_per_nm==1500);
            for (const char* out_of_range:{"99","10001"})
                rejects([&]{std::istringstream stream("schema_version=5\n"+body+"roll_torque_units_per_nm="+out_of_range+"\n");parse_configuration(stream);});
            // Schema 6 (decision 16): no firmest keys; an old one is ignored and logged.
            std::string bare=body;
            for (const char* key:{"roll_spring_percent=40\n","pitch_spring_percent=40\n","roll_minimum_spring_percent=20\n","roll_half_gain_pa=200\n","plain_spring_percent=20\n"})
                bare.erase(bare.find(key),std::strlen(key));
            std::istringstream v6("schema_version=6\n"+bare+"roll_torque_units_per_nm=1000\n");const auto six=parse_configuration(v6);
            check(six.retired.empty() && same_tuning(six.tuning,{40,40,0}) && six.flight.pitch_force_units_per_lb==300);
            std::istringstream v6old("schema_version=6\n"+bare+"roll_torque_units_per_nm=1000\npitch_spring_percent=100\n");
            check(parse_configuration(v6old).retired==std::vector<std::string>{"pitch_spring_percent"});
            rejects([&]{std::istringstream stream("schema_version=6\n"+bare);parse_configuration(stream);});
            rejects([&]{std::istringstream stream("schema_version=7\n"+bare+"roll_torque_units_per_nm=1000\n");parse_configuration(stream);});
        }
        DiagnosticTrim lab;
        const auto lab_now=Clock::now();
        check(lab.set_target(500) && !lab.set_target(501) && lab.target()==500);
        check(!lab.renew(5001,true,lab_now) && !lab.renew(1000,false,lab_now));
        check(!lab.active(true,lab_now));
        check(lab.renew(1000,true,lab_now) && lab.active(true,lab_now+std::chrono::milliseconds(999)));
        check(!lab.active(true,lab_now+std::chrono::milliseconds(1000)));
        check(lab.renew(500,true,lab_now) && !lab.active(false,lab_now));
        check(!lab.active(true,lab_now)); // connection revokes the lease; it cannot revive
        check(lab.renew(1000,true,lab_now) && lab.renew(0,false,lab_now) && !lab.active(true,lab_now));
        FlightSettings flight_defaults;
        check(flight_defaults.auto_start==0 && flight_defaults.pitch_force_units_per_lb==222 && valid_flight_settings(flight_defaults));
        {
            // The pitch solver (elevator.hpp, decision 15). Test figures: the
            // C172 profile's in ansible/brunner.yml when this was written;
            // workbench_tests.cpp checks the solver against elevator.html itself.
            const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
            check(valid_figures(c172));
            {
                // A stop says what failed, in words, and whether Retry can help:
                // the drive's refusal of a spring strength of 0 (2026-11-08).
                const auto refused=failure_reason(DriveWriteFailed("Write rejected",2,{force_scale,0,0},0x06000000u,{}));
                check(refused.text.find("pitch drive refused spring strength 0% (code 06000000)")!=std::string::npos && !refused.retry_helps);
                check(refused.short_text=="Pitch drive refused spring strength 0%" && refused.short_text.size()<=45);
                const auto kept=failure_reason(DriveWriteFailed("Readback mismatch",1,{trim_position,static_cast<std::uint32_t>(-600),0},{},0u));
                check(kept.text.find("roll drive kept resting point 0 counts when the plugin wrote resting point -600 counts")!=std::string::npos && kept.retry_helps);
                const auto held=failure_reason(DriveWriteFailed("held",2,{manual_force_bias,encode_bias(-3000),0},{},{}));
                check(held.text.find("held back constant force -3000 for the pitch drive")!=std::string::npos && !held.retry_helps);
                check(failure_reason(std::runtime_error("Controller state changed during loading")).retry_helps);
                check(failure_reason(std::runtime_error("odd")).text=="Force feel stopped: odd. Retry starts it again; X-Plane's Log.txt has the details.");
                // The plugin's own check refusing (the session start that asked
                // for a 0% pitch spring, 2026-11-08): a plugin fault, Retry no help.
                const auto own=failure_reason(std::invalid_argument("Loading asked for too little"));
                check(!own.retry_helps && own.text.find("plugin's own check stopped force feel: Loading asked for too little.")!=std::string::npos);
                check(own.short_text.size()<=45);
                check(!loading_write_allowed(2,{force_scale,0,0}) && loading_write_allowed(2,{force_scale,static_cast<std::uint32_t>(softest_spring_percent),0}));
            }
            // The drive's force at x0 is the model's within half a percent of
            // the curve over the displacement, plus half a unit; unless saturated.
            const auto matches=[](const PitchSolution& p,double x,int units) {
                return std::abs(p.drive_force_lb-p.force_lb)<=(0.5*curve_units_per_count_percent()*std::abs(x-p.centre)+0.5)/units+1e-9;
            };
            // It rests where the model does, within half a unit of constant force.
            const auto rests=[](const PitchSolution& p) {
                const double ks=p.drive_slope_lb_per_count;
                return ks>0?std::abs(p.drive_rest_counts-p.rest_counts)<=0.5/222/ks+0.5:true;
            };
            // Parked: no air, no spring. The weight presses the yoke onto the
            // forward stop; the line is anchored there with the weight's push.
            auto solution=solve_pitch(c172,{0,0,1},0,222);
            check(solution.at_stop && solution.rest_counts==12000 && solution.centre==12000 && std::abs(solution.weight_lb-4.11)<1e-9 && solution.stiffness_lb_per_in==0);
            check(solution.bias==-static_cast<int>(std::lround(4.11*std::cos(23*radians_per_degree)*222)) && !solution.bias_limited && solution.bias_lb>0);
            // The cosine wants a little spring, heavier toward neutral; under the
            // drive's softest, so floored: firmer than the model away from the stop.
            check(solution.floored && !solution.saturated && solution.strength==softest_spring_percent && rests(solution) && solution.drive_force_lb>solution.force_lb);
            check(std::abs(solve_pitch(c172,{0,0,1},12000,222).drive_force_lb-solution.bias_lb)<1e-9);
            // Taxiing into a breeze, with a pound worth fewer units: unsaturated,
            // the force at x0 and the rest both the model's.
            for (const double x:{-12000.0,-3000.0,0.0,2500.0,9000.0}) {
                solution=solve_pitch(c172,{0,40,1},x,150);
                check(!solution.at_stop && !solution.saturated && matches(solution,x,150) && solution.drive_rest_counts==solution.centre);
            }
            // At 222 units a pound the same breeze is past 100 % but inside
            // the drive's own limit (decision 16): no firmest holds it back.
            solution=solve_pitch(c172,{0,40,1},0,222);
            check(!solution.saturated && solution.strength>100 && solution.strength<solution.ceiling && solution.ceiling==225 && matches(solution,0,222));
            // The limit is the strongest spring whose line, constant force
            // and centre together, stays within the margin at both stops.
            check(worst_force_units(solution.ceiling,solution.centre,0,12000)<=drive_margin_units()
                && worst_force_units(solution.ceiling+1,solution.centre,0,12000)>drive_margin_units());
            // Cruise: the air is stiffer than the drive can be. Saturated at its
            // limit, the rest stays exact and the force is the most the drive gives.
            const ElevatorInputs cruise{0.15,1988,1};
            solution=solve_pitch(c172,cruise,-6000,222);
            check(!solution.at_stop && solution.saturated && solution.strength==solution.ceiling && solution.ceiling==381 && rests(solution)
                && std::abs(solution.drive_force_lb)<std::abs(solution.force_lb));
            check(within_margin(solution.strength,solution.centre,solution.bias,pitch_travel_bound())
                && !within_margin(solution.strength+1,solution.centre,solution.bias,pitch_travel_bound()));
            check(std::abs(solution.shrink_ratio-solution.drive_slope_lb_per_count/solution.slope_lb_per_count)<1e-12 && solution.shrink_ratio<1);
            check(solution.centre==static_cast<int>(std::lround(solution.rest_counts)) && solution.rest_counts>solution.centre_counts); // forward of trim by the weight
            // At the rest itself: no force, and the slope from a 60-count secant.
            solution=solve_pitch(c172,cruise,std::numeric_limits<double>::quiet_NaN(),222);
            check(std::abs(solution.force_lb)<1e-6 && std::abs(solution.drive_force_lb)<0.5/222+1e-9 && rests(solution));
            // How much force a pound is changes how hard, never where it rests.
            const auto a=solve_pitch(c172,{0,8,1},-4000,222),b=solve_pitch(c172,{0,8,1},-4000,444);
            check(!a.saturated && !b.saturated && std::abs(b.strength-2*a.strength)<=1 && a.centre==b.centre && matches(a,-4000,222) && matches(b,-4000,444));
            check(std::abs(model_rest_counts(c172,cruise)-solve_pitch(c172,cruise,0,222).rest_counts)<1e-9);
            // The constant force's bound: a heavy elevator at 4 g, parked, says so.
            auto heavy=c172;heavy.unbalance_inlb=200;
            solution=solve_pitch(heavy,{0,0,4},0,222);
            check(solution.bias==-max_pitch_bias() && solution.bias_limited);
            // The constant force goes first, holding the rest; the spring has
            // what the margin leaves: 2250 + 20 units a percent across the
            // whole travel from the forward stop, so 87 %.
            check(solution.ceiling==87 && solution.strength<=87 && within_margin(solution.strength,solution.centre,solution.bias,pitch_travel_bound()));
            // Taxiing at 15 kt with full nose-down trim: the rest is at the
            // forward stop, held by a constant force, and the air wants more
            // than 100 %. The strength stays where the firmware keeps the
            // constant force (0x211f's cut-off): the push at the stop is never lost.
            const double taxi_pa=.5*1.225*std::pow(15*0.514444,2);
            for (const double x:{-12000.0,0.0,11900.0}) {
                solution=solve_pitch(c172,{-1,taxi_pa,1},x,222);
                check(solution.at_stop && solution.rest_counts==12000 && solution.bias<0 && solution.saturated
                    && solution.strength==strongest_with_constant_force_percent && solution.ceiling==strongest_with_constant_force_percent
                    && keeps_constant_force(solution.strength,solution.bias) && valid_tuning(solved_tuning(Tuning{},solution)));
            }
            // Inverted, the weight falls the other way: the aft stop.
            check(solve_pitch(c172,{0,0,-1},0,222).rest_counts==-12000);
            // No weight and no air: the yoke rests where trim puts the elevator.
            auto light=c172;light.unbalance_inlb=0;
            solution=solve_pitch(light,{0.5,0,1},0,222);
            check(solution.rest_counts==solution.centre_counts && solution.rest_counts<0 && solution.bias==0 && solution.strength==softest_spring_percent && solution.floored);
            // The session's model, and the registers it asks for.
            PitchModel m{c172,cruise,222,0};
            check(valid_pitch_model(m));
            const auto solved=solve_pitch(m,-6000);
            const auto t=solved_tuning(Tuning{33,20,0,0},solved);
            check(t.roll_percent==33 && t.pitch_percent==solved.strength && t.pitch_trim==solved.centre && t.pitch_bias==solved.bias && valid_tuning(t));
            m.units_per_lb=49;check(!valid_pitch_model(m));m.units_per_lb=222;
            m.figures.gearing_rad_per_in=0;check(!valid_pitch_model(m));
            // The encoder is pull positive; the drive'solved counts are forward positive.
            check(drive_counts_from_encoder(-1200)==1200 && drive_counts_from_encoder(3000)==-3000);
            // The tail'solved air: X-Plane'solved over the profile'solved sections, else airspeed'solved.
            check(tail_air(250,80).pa==250 && tail_air(250,80).source==TailSource::surface);
            check(std::abs(tail_air(-1,100).pa-0.5*1.225*51.4444*51.4444)<1e-6 && tail_air(-1,100).source==TailSource::airspeed);
            check(tail_air(-1,-5).pa==0 && tail_air(-1,std::numeric_limits<double>::quiet_NaN()).pa<0);
            // flight_target: every aircraft, the profile's figures for both axes
            // (decisions 6 and 15); each aileron's air from its panels where
            // supplied, else airspeed's on both sides with no angle of attack.
            const AileronFigures ailerons{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};
            const RollKnobs roll_knobs{1000,0};
            FlightSample flying_sample;flying_sample.airspeed=100;flying_sample.flow={-1,-1,-1,-1};flying_sample.trim=0.15;flying_sample.pitch=0.25;flying_sample.roll=-0.25;
            const PitchKnobs knobs{222,0};
            auto target=flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs);
            check(target && target->pitch && target->roll && target->tuning.roll_percent==40 && target->tuning.pitch_percent==40);
            check(std::abs(target->pitch->inputs.tail_pa-tail_air(-1,100).pa)<1e-9 && target->pitch->inputs.trim==0.15);
            check(std::abs(target->pitch_error-(0.25+model_rest_counts(c172,target->pitch->inputs,-0.25*12000)/12000))<1e-9);
            check(target->aileron_source==AileronSource::airspeed && target->roll->inputs.left.pa==tail_air(-1,100).pa
                && target->roll->inputs.right.pa==target->roll->inputs.left.pa && target->roll->inputs.left.alpha_deg==0);
            check(std::abs(target->roll_error-(-0.25-wheel_exact_counts(ailerons,wheel_rest_rad(ailerons,target->roll->inputs,wheel_rad(ailerons,-0.25*13000)))/13000))<1e-12);
            check(target->roll->units_per_nm==1000 && valid_roll_model(*target->roll));
            // The panels' air, when the plugin read it, is the model's.
            flying_sample.ailerons=AileronInputs{{900,6,0},{800,-3,1}};
            target=flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs);
            check(target && target->aileron_source==AileronSource::surface && target->roll->inputs.left.pa==900 && target->roll->inputs.right.stalled==1);
            flying_sample.ailerons.reset();
            // The tail's angle and stall (decision 17) come with its panels'
            // pressure; on airspeed's the tail has none.
            flying_sample.tail=AileronSide{1500,7.5,0.25};
            target=flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs);
            check(target && target->pitch->inputs.tail_alpha_deg==0 && target->pitch->inputs.tail_stalled_xp==0);
            flying_sample.flow.tail=1500;
            target=flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs);
            check(target && target->pitch->inputs.tail_pa==1500 && target->pitch->inputs.tail_alpha_deg==7.5 && target->pitch->inputs.tail_stalled_xp==0.25);
            flying_sample.tail.reset();flying_sample.flow.tail=-1;
            {
                // Parked, no air over the tail: X-Plane reports every element
                // stalled at ±160-178° (read live, 2026-11-09). Under the
                // ailerons' gate the read says nothing, and the model's own
                // gate keeps it neither stalled nor reversed either way.
                std::array<float,99> speed{},area{},aoa{},flags{};
                for (const int wing:{8,9}) for (int i=wing*10;i<wing*10+10;++i) {area[i]=0.125f;aoa[i]=i%2?171.0f:-176.0f;flags[i]=1;}
                const auto parked=tail_side(1.225,speed.data(),area.data(),aoa.data(),flags.data(),99,8,9);
                check(parked && parked->pa==0 && parked->alpha_deg==0 && parked->stalled==0);
                ElevatorFigures tailed=c172;tailed.alpha_slope_per_rad=0.2;tailed.stall_factor=0.4;tailed.tau=0.5;tailed.stall_deg=13;tailed.stall_width_deg=4;tailed.k_rev=1;
                const auto still=solve_pitch(tailed,ElevatorInputs{0,0,1,0,0},0,222);
                for (const double pa:{0.0,2.4}) for (const double alpha:{171.0,-176.0}) {
                    const auto raw=solve_pitch(tailed,ElevatorInputs{0,pa,1,alpha,1},-12000,222);
                    check(!raw.tail_reversed && raw.tail_stalled_xp==0 && raw.elevator_stalled==0 && raw.hinge_stalled==0 && raw.tail_alpha_deg==0);
                    if (pa==0) check(raw.rest_counts==still.rest_counts);
                }
                // At the gate the same flags and angle count: air from behind, the tail-plane stalled.
                const auto gated=solve_pitch(tailed,ElevatorInputs{0,2.5,1,171,1},0,222);
                check(gated.tail_reversed && gated.tail_stalled_xp==1 && gated.hinge_stalled==1 && std::abs(gated.rest_counts)==12000);
            }
            check(!flight_target(flying_sample,flight_defaults,Tuning{40,40,0},ElevatorFigures{},knobs,ailerons,roll_knobs)); // no figures, no target
            check(!flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,AileronFigures{},roll_knobs));
            check(!flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,RollKnobs{99,0}));
            auto lost_sample=flying_sample;lost_sample.airspeed=std::numeric_limits<double>::quiet_NaN();
            check(!flight_target(lost_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs));
            lost_sample=flying_sample;lost_sample.trim=NAN;check(!flight_target(lost_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs));
            lost_sample=flying_sample;lost_sample.roll=NAN;check(!flight_target(lost_sample,flight_defaults,Tuning{40,40,0},c172,knobs,ailerons,roll_knobs));
            // The workbench's count of the feel's edits reaches the session.
            const PitchKnobs edited{222,9};
            target=flight_target(flying_sample,flight_defaults,Tuning{40,40,0},c172,edited,ailerons,RollKnobs{1000,4});
            check(target && target->pitch->edit==9 && target->roll->edit==4);
        }
        {
            using K=LoadingTransport;
            FlightSettings model;
            model.pitch_force_units_per_lb=49;check(!valid_flight_settings(model));
            model.pitch_force_units_per_lb=1001;check(!valid_flight_settings(model));
            model.pitch_force_units_per_lb=222;model.pitch_force_gain_divisor=7;check(!valid_flight_settings(model));
            model.pitch_force_gain_divisor=0;check(!valid_flight_settings(model));
            model.pitch_force_gain_divisor=6;model.pitch_move_back_gain=179;check(!valid_flight_settings(model));
            model.pitch_move_back_gain=721;check(!valid_flight_settings(model));
            // A model session's pitch setup, motors off: the linear curve, the
            // drag gains asked for and the constant force kept on; roll as before.
            LoadingTransport m;PitchSetup model_pitch;model_pitch.curve=model_curve;model_pitch.constant_force=true;
            model_pitch.gain_denominator=3;model_pitch.move_back=360;
            const Tuning initial{30,20,0};
            std::optional<std::uint32_t> found;
            prepare_loading(m,not_stopped,initial,model_pitch,&found);
            check(found && *found==40); // the switch-on cut-off, read before anything is written
            for (unsigned sub=1;sub<=9;++sub) {
                check(m.values[K::key(2,{0x2113,static_cast<std::uint8_t>(sub),2})]==model_curve[sub-1]);
                check(m.values[K::key(1,{0x2113,static_cast<std::uint8_t>(sub),2})]==bench_curve[sub-1]);
            }
            check(m.values[K::key(2,manual_offset_off)]==bias_offset_off && m.values[K::key(1,manual_offset_off)]==40);
            check(m.values[K::key(2,force_gain_denominator)]==3 && m.values[K::key(2,move_back_gain)]==360 && m.values[K::key(1,force_gain_denominator)]==8);
            verify_loading(m,1,not_stopped,initial,model_pitch);verify_loading(m,2,not_stopped,initial,model_pitch);
            rejects([&]{verify_loading(m,2,not_stopped,initial);}); // not the curve and gains that were written
            for (const auto changed:{RegisterWrite{force_gain_numerator,2,0},RegisterWrite{manual_offset_off,40,0}}) {
                const auto was=m.values[K::key(2,changed.reg)];m.values[K::key(2,changed.reg)]=changed.value;
                rejects([&]{verify_loading(m,2,not_stopped,initial,model_pitch);}); // a numerator or cut-off the drive changed
                m.values[K::key(2,changed.reg)]=was;
            }
            rejects([&]{prepare_loading(m,not_stopped,Tuning{30,20,0,-100},model_pitch);}); // a session starts with no constant force
            // A session's real start passes the start check and every write;
            // a pitch spring below the drive's softest is named in the refusal.
            {
                LoadingTransport engaging;prepare_loading(engaging,not_stopped,session_load({30,40,0,0},9000,0).written,model_pitch);
                check(engaging.values[K::key(2,force_scale)]==static_cast<std::uint32_t>(softest_spring_percent));
                std::string named;
                try {prepare_loading(engaging,not_stopped,Tuning{30,0,0,0},model_pitch);} catch (const std::invalid_argument& e) {named=e.what();}
                check(named.find("pitch spring 0%")!=std::string::npos);
            }
            PitchSetup wild=model_pitch;wild.gain_denominator=7;rejects([&]{prepare_loading(m,not_stopped,initial,wild);});
            // Live, the constant force goes to pitch alone and is read back.
            m.enabled=true;m.writes.clear();Tuning applied_m=initial;
            apply_live_tuning(m,applied_m,{30,20,0,-912},not_stopped);
            check(applied_m.pitch_bias==-912 && m.writes.size()==1 && decode_bias(m.values[K::key(2,manual_force_bias)])==-912 && m.values[K::key(1,manual_force_bias)]==0);
            verify_loading(m,2,not_stopped,applied_m,model_pitch);
            m.values[K::key(2,manual_force_bias)]=0;m.writes.clear(); // the drive lost it
            rejects([&]{apply_live_tuning(m,applied_m,{30,21,0,-912},not_stopped);});check(m.writes.empty());
            // After the session the resting settings come back: switch-on gains, bench curve, no
            // constant force, and the cut-off the session found. The constant force goes first.
            m.enabled=false;m.order.clear();
            check(rest_after_stop(m,nullptr,found));
            check(!m.order.empty() && m.order.front()==std::make_pair(2u,std::uint16_t{0x211a}));
            check(m.values[K::key(2,force_gain_denominator)]==6 && m.values[K::key(2,move_back_gain)]==180 && m.values[K::key(2,manual_force_bias)]==0);
            check(m.values[K::key(2,force_gain_numerator)]==1 && m.values[K::key(2,manual_offset_off)]==40 && m.values[K::key(1,manual_offset_off)]==40);
            for (unsigned sub=1;sub<=9;++sub) check(m.values[K::key(2,{0x2113,static_cast<std::uint8_t>(sub),2})]==bench_curve[sub-1]);
            // After CLS2Sim (30) it is 30 that comes back.
            LoadingTransport after;after.values[K::key(2,manual_offset_off)]=30;std::optional<std::uint32_t> was;
            prepare_loading(after,not_stopped,initial,model_pitch,&was);
            check(was && *was==30 && after.values[K::key(2,manual_offset_off)]==bias_offset_off);
            check(rest_after_stop(after,nullptr,was) && after.values[K::key(2,manual_offset_off)]==30);
            // A cut-off no strength could be is refused before anything is written.
            LoadingTransport odd;odd.values[K::key(2,manual_offset_off)]=102;std::optional<std::uint32_t> none;
            rejects([&]{prepare_loading(odd,not_stopped,initial,model_pitch,&none);});
            check(odd.writes.empty() && odd.ref_writes.empty() && !none);
            // Pitch's constant force is cleared before anything else, whatever roll or the checks do next.
            LoadingTransport stuck;stuck.values[K::key(2,manual_force_bias)]=encode_bias(-912);stuck.running_node=1;
            std::string why;
            check(!rest_after_stop(stuck,&why) && !why.empty()); // roll's motor found running: resting refused
            check(stuck.order.size()==1 && stuck.order.front()==std::make_pair(2u,std::uint16_t{0x211a}) && stuck.values[K::key(2,manual_force_bias)]==0);
            // Ending a session: the constant force first, then both stops, even if a stop is not confirmed.
            LoadingTransport ending;ending.values[K::key(2,manual_force_bias)]=encode_bias(-912);ending.enabled=true;
            const std::array<std::uint16_t,2> stop_words{{0x0f,0x0f}},stop_modes{{0x2205,0x2205}};
            bool zeroed=false;
            check(!stop_session(ending,stop_words,stop_modes,&zeroed) && zeroed); // the fake stays enabled: not confirmed; the force read back 0
            check(ending.order.size()==7 && ending.order.front()==std::make_pair(2u,std::uint16_t{0x211a}) && ending.values[K::key(2,manual_force_bias)]==0);
            ending.enabled=false;ending.order.clear();
            check(stop_session(ending,stop_words,stop_modes) && ending.order.front()==std::make_pair(2u,std::uint16_t{0x211a}));
            // Over a dead link it is attempted and never throws.
            LoadingTransport dead;dead.fail=true;check(!clear_pitch_force(dead));
            zeroed=true;try {stop_session(dead,stop_words,stop_modes,&zeroed);} catch (const std::exception&) {}
            check(!zeroed); // the stop may throw (link_lost catches it); the force is reported not confirmed cleared
            // The plugin's setup for a session, and the curve it publishes.
            const auto modelled=pitch_setup_for(true,3,360),firmness=pitch_setup_for(false,6,180);
            check(modelled.curve==model_curve && modelled.constant_force && modelled.gain_denominator==3 && modelled.move_back==360);
            check(firmness.curve==bench_curve && !firmness.constant_force && firmness.gain_denominator==6 && firmness.move_back==180);
            // At the defaults the drag gains and the cut-off are only read, never written.
            LoadingTransport plain;std::optional<std::uint32_t> unread;prepare_loading(plain,not_stopped,initial,{},&unread);check(!unread && rest_after_stop(plain));
            for (const auto& op:plain.writes) check(!same_register(op.reg,force_gain_numerator) && !same_register(op.reg,force_gain_denominator)
                && !same_register(op.reg,move_back_gain) && !same_register(op.reg,manual_offset_off));
        }
        {
            const auto tick=Clock::now();using ms=std::chrono::milliseconds;
            check(valid_frame_age(tick,tick+ms(250),false) && !valid_frame_age(tick,tick+ms(251),false));
            check(!valid_frame_age(tick,tick,true) && !valid_frame_age({},tick,false) && valid_frame_age(tick+ms(300),tick+ms(301),false));
            std::array<float,100> v{},area{};
            // Unequal left/right areas and excluded tips make an unweighted or misindexed calculation fail.
            v.fill(1000);area.fill(1);
            for (int i=0;i<9;++i) {v[20+i]=0;v[30+i]=4;area[30+i]=3;v[80+i]=10;v[90+i]=20;}
            const auto flow=c172_flow(2,v.data(),area.data(),99);
            check(flow.left==0 && flow.right==16 && flow.roll==12 && flow.tail==250);
            check(sections_pa(2,v.data(),area.data(),99,8,9)==250 && sections_pa(2,v.data(),area.data(),99,2,3)==12);
            check(sections_pa(2,v.data(),area.data(),98,8,9)<0 && sections_pa(std::numeric_limits<double>::quiet_NaN(),v.data(),area.data(),99,8,9)<0);
            auto missing=area;missing[88]=0;
            check(c172_flow(2,v.data(),missing.data(),99).tail<0 && sections_pa(2,v.data(),missing.data(),99,8,9)<0);
            check(c172_flow(2,v.data(),area.data(),98).tail<0);
            // Each aileron's air (aileron-and-aoa-plan section 3): pressure over
            // every panel by area; angle of attack and stalled share weighted by
            // q*area over the panels with at least 2.5 Pa only.
            std::array<float,100> aoa{},stalled{};
            for (int i=0;i<9;++i) {aoa[20+i]=89;stalled[20+i]=1;aoa[30+i]=i<4?10.f:2.f;stalled[30+i]=i<2?1.f:0.f;}
            auto left=aileron_side(2,v.data(),area.data(),aoa.data(),stalled.data(),99,2),right=aileron_side(2,v.data(),area.data(),aoa.data(),stalled.data(),99,3);
            check(left && left->pa==0 && left->alpha_deg==0 && left->stalled==0); // no air says nothing, however stalled X-Plane calls it
            check(right && right->pa==16 && std::abs(right->alpha_deg-50.0/9)<1e-12 && std::abs(right->stalled-2.0/9)<1e-12);
            auto one_pa=v;one_pa[38]=1;aoa[38]=std::numeric_limits<float>::quiet_NaN(); // 1 Pa: in the pressure, not the angle
            right=aileron_side(2,one_pa.data(),area.data(),aoa.data(),stalled.data(),99,3);
            check(right && std::abs(right->pa-(8*16+1)/9.0)<1e-12 && std::abs(right->alpha_deg-48.0/8)<1e-12 && std::abs(right->stalled-2.0/8)<1e-12);
            aoa[37]=std::numeric_limits<float>::quiet_NaN();check(!aileron_side(2,one_pa.data(),area.data(),aoa.data(),stalled.data(),99,3));
            check(!aileron_side(2,v.data(),area.data(),aoa.data(),stalled.data(),38,3) && !aileron_side(NAN,v.data(),area.data(),aoa.data(),stalled.data(),99,2));
            check(!aileron_side(2,v.data(),missing.data(),aoa.data(),stalled.data(),99,8));
            {
                // Air from behind astride ±180° at 39 Pa (8 m/s): the angles unwrap
                // about their circular mean, so they read near 180°, not the plain mean's 0°.
                std::array<float,100> speed8{},area8{},angle8{},flag8{};
                speed8.fill(8);area8.fill(0.125f);
                const auto tail_at=[&](int plus) {
                    for (int k=0;k<18;++k) angle8[80+(k/9)*10+k%9]=k<plus?179.f:-179.f;
                    return tail_side(1.225,speed8.data(),area8.data(),angle8.data(),flag8.data(),99,8,9);
                };
                const auto t18=tail_at(18),t10=tail_at(10),t9=tail_at(9),t0=tail_at(0);
                check(t18 && std::abs(t18->alpha_deg-179)<1e-9 && t10 && std::abs(t10->alpha_deg-(10*179.0+8*181.0)/18)<1e-9
                    && t9 && std::abs(t9->alpha_deg-180)<1e-9 && t0 && std::abs(t0->alpha_deg+179)<1e-9 && std::abs(t9->pa-39.2)<1e-9);
                // Each reads as air from behind: the elevator runs to a stop, not to trim.
                const ElevatorFigures from_behind{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
                for (const auto& t:{t18,t10,t9,t0}) check(elevator_state(from_behind,{0,t->pa,1,t->alpha_deg,0}).reversed);
                // An aileron's nine: 9/0, 5/4 and an even split (one panel under the gate).
                const auto aileron_at=[&](int plus,bool even) {
                    for (int k=0;k<9;++k) angle8[20+k]=k<plus?179.f:-179.f;
                    auto quiet=speed8;if (even) quiet[28]=1;
                    return aileron_side(1.225,quiet.data(),area8.data(),angle8.data(),flag8.data(),99,2);
                };
                const auto a9=aileron_at(9,false),a5=aileron_at(5,false),a4=aileron_at(4,true);
                check(a9 && std::abs(a9->alpha_deg-179)<1e-9 && a5 && std::abs(a5->alpha_deg-(5*179.0+4*181.0)/9)<1e-9 && a4 && std::abs(a4->alpha_deg-180)<1e-9);
                // Just either side of 90°, the plain mean, as before: continuous where the two meet.
                for (int k=0;k<9;++k) angle8[20+k]=k==0?91.f:80.f;
                const auto across=aileron_side(1.225,speed8.data(),area8.data(),angle8.data(),flag8.data(),99,2);
                check(across && std::abs(across->alpha_deg-(91+8*80)/9.0)<1e-9);
            }
            // Without the panels both sides take airspeed's pressure, level and unstalled.
            const auto by_airspeed=aileron_air(std::nullopt,100);
            check(by_airspeed.valid && by_airspeed.source==AileronSource::airspeed && by_airspeed.inputs.left.pa==tail_air(-1,100).pa && by_airspeed.inputs.right.stalled==0);
            check(!aileron_air(std::nullopt,NAN).valid && aileron_air(AileronInputs{{-1,0,0},{5,0,0}},100).source==AileronSource::airspeed);
            const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
            FlightSample frame_in{0,.08,flow,.25,-.25};
            auto evaluated=flight_target(frame_in,flight_defaults,{},c172,{},AileronFigures{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4},{});
            check(evaluated && evaluated->pitch->inputs.tail_pa==250 && evaluated->roll->inputs.left.pa==0 && evaluated->aileron_source==AileronSource::airspeed);
        }
        TuningRamp flight_ramp({20,20,0});
        Tuning previous{20,20,0};
        for (int i=0;i<500;++i) {
            const auto next=flight_ramp.advance({40,40,-6000},0.02);
            check(std::abs(next.pitch_trim-previous.pitch_trim)<=12 && next.pitch_trim>=-6000);
            previous=next;
        }
        check(same_tuning(previous,{40,40,-6000}));
        rejects([&]{flight_ramp.advance({40,40,-12001},0.02);});
        StartupReadiness startup_ready;
        const auto launch=Clock::now();
        const auto frame=[&](bool running,bool busy,double sim_time,int ms) {
            startup_ready.observe(running,busy,sim_time,launch+std::chrono::milliseconds(ms));
            return startup_ready.ready(launch+std::chrono::milliseconds(ms));
        };
        check(!frame(true,false,0,0));
        check(!frame(true,false,.02,20));
        check(!frame(true,false,.04,40));
        check(frame(true,false,.06,60)); // actual completed frames, not a three-second timer
        check(!frame(true,true,.08,80)); // background loading resets readiness
        check(!frame(true,false,.10,100));
        check(!frame(true,false,.10,120)); // callback without physics progress is insufficient
        check(!frame(true,false,.12,140));
        check(!frame(true,false,.14,160));
        check(frame(true,false,.16,180));
        check(!startup_ready.ready(launch+std::chrono::milliseconds(431))); // stale callback
        check(!frame(true,false,.20,5000)); // startup stall invalidates progress
        check(!frame(false,false,.22,5020)); // pause/control ownership change
        check(!frame(true,false,std::numeric_limits<double>::quiet_NaN(),5040));
        check(!frame(true,false,0,5060)); // reset flight clock
        check(!frame(true,false,.02,5080));
        check(!frame(true,false,.04,5100));
        check(frame(true,false,.06,5120));
        startup_ready.reset();check(!startup_ready.ready(launch+std::chrono::milliseconds(5120)));
        Snapshot notice_state;
        check(startup_notice(true,false,true,notice_state)==StartupNotice::waiting);
        notice_state.reference_active=true;
        check(startup_notice(true,false,true,notice_state)==StartupNotice::centering);
        notice_state.reference_active=false;
        check(startup_notice(true,true,false,notice_state)==StartupNotice::hidden);
        notice_state.loading_fault=true;
        check(startup_notice(true,true,false,notice_state)==StartupNotice::unavailable);
        check(startup_notice(false,false,true,notice_state)==StartupNotice::hidden);
        FlightLifecycle lifecycle;Snapshot flight_state;
        lifecycle.reset(true);
        check(!lifecycle.should_start(true,flight_state));
        flight_state.connected=true;
        check(!lifecycle.should_start(false,flight_state));
        check(lifecycle.should_start(true,flight_state));lifecycle.requested();
        check(!lifecycle.should_start(true,flight_state)); // pending command cannot duplicate initialization
        flight_state.reference_active=true;lifecycle.observe(flight_state);
        check(!lifecycle.should_start(true,flight_state));
        flight_state.reference_active=false;flight_state.loading_active=true;lifecycle.observe(flight_state);
        check(!lifecycle.should_start(true,flight_state));
        flight_state.loading_active=false;flight_state.flight_suspended=true;lifecycle.observe(flight_state);
        check(!lifecycle.should_start(false,flight_state) && lifecycle.should_start(true,flight_state));
        flight_state.flight_suspended=false;flight_state.loading_fault=true;lifecycle.observe(flight_state);
        check(!lifecycle.should_start(true,flight_state));
        flight_state.loading_fault=false;check(!lifecycle.should_start(true,flight_state)); // a cleared fault alone never re-arms
        lifecycle.arm();check(lifecycle.should_start(true,flight_state));
        lifecycle.hold();flight_state.flight_suspended=true;check(!lifecycle.should_start(true,flight_state));
        lifecycle.reset(false);flight_state.flight_suspended=false;check(!lifecycle.should_start(true,flight_state));
        FlightLifecycle automatic_input;
        Snapshot startup_state;startup_state.connected=true;automatic_input.reset(true);
        check(automatic_input.should_start(true,startup_state));automatic_input.requested();
        startup_state.reference_active=true;automatic_input.observe(startup_state);
        startup_state.reference_active=false;startup_state.session_reference=true;automatic_input.observe(startup_state);
        check(!automatic_input.should_start(true,startup_state)); // no repeated calibration or implicit force session
        {
            // Buffet (decision 19): bounds, the share and the drive side.
            check(loading_write_allowed(2,{vibration_slow_force,450,0}) && !loading_write_allowed(2,{vibration_slow_force,451,0}));
            check(loading_write_allowed(1,{vibration_slow_force,0,0}) && loading_write_allowed(1,{vibration_slow_time,10,0}));
            check(!loading_write_allowed(1,{vibration_slow_time,9,0}) && !loading_write_allowed(1,{vibration_slow_time,141,0}));
            check(!loading_write_allowed(1,{vibration_fast_force,0,0}) && !loading_write_allowed(2,{vibration_fast_time,25,0}));
            check(loading_read_register(vibration_slow_force) && loading_read_register(vibration_fast_time));
            check(buffet_share(AileronSide{400,15,0.5})==0.5);
            check(buffet_share(AileronSide{100,15,1})==0); // too little air
            check(buffet_share(AileronSide{400,4,1})==0); // a flag at a small angle
            check(buffet_share(AileronSide{400,-12,1})==1); // either way
            check(buffet_share(AileronSide{400,15,0.25},AileronSide{400,15,0.75})==0.75);
            BuffetKnob off;check(!off.on && buffet_units(off,1)==0);
            BuffetKnob on{true,100,80};
            check(buffet_units(on,1)==450 && buffet_units(on,0.5)==225 && buffet_units(on,0.04)==0 && buffet_units(on,0.07)==56);
            check(buffet_units(BuffetKnob{true,101,80},1)==0 && buffet_units(BuffetKnob{true,50,9},1)==0);
            // The room the spring line leaves under drive_margin_units(), anywhere in the travel.
            check(buffet_headroom(100,0,2250,pitch_travel_bound())==750 && buffet_headroom(400,0,0,pitch_travel_bound())==0
                && buffet_headroom(355,0,0,pitch_travel_bound())==450 && buffet_headroom(20,0,0,roll_travel_bound())==3800);
            const std::atomic<bool> go{false};
            const Tuning soft{20,20,0,0,0};
            // Knob off: nothing written or even read, however hard the stall.
            const std::array<std::uint16_t,2> session{{0x2205,0x2205}}; // what reference_start leaves: spring mode, bit 13
            const auto in_session=[] {
                LoadingTransport t;t.in_session=true;
                for (unsigned node=1;node<=2;++node) t.values[LoadingTransport::key(node,force_mode)]=0x2205;
                return t;
            };
            const auto mode_of=[](LoadingTransport& t,unsigned node) {return t.values[LoadingTransport::key(node,force_mode)];};
            {
                auto t=in_session();BuffetDrive b;
                for (int i=0;i<5;++i) b.update(t,BuffetTarget{off,450,450},soft,session,go);
                check(t.writes.empty() && t.ref_writes.empty() && t.reads==0 && !b.dirty());
                check(b.clear(t) && t.writes.empty() && t.ref_writes.empty());
            }
            // Knob on: bounded writes, each read back; bit 4 set last and
            // cleared first, on the session's own mode; the end clears all of it.
            {
                auto t=in_session();BuffetDrive b;
                for (unsigned node=1;node<=2;++node) t.values[LoadingTransport::key(node,vibration_slow_time)]=40;
                b.update(t,BuffetTarget{on,9000,0},soft,session,go);
                check(b.dirty() && b.units()[1]==450 && b.units()[0]==0);
                check(t.values[LoadingTransport::key(2,vibration_slow_force)]==450 && t.values[LoadingTransport::key(2,vibration_slow_time)]==80);
                check(mode_of(t,2)==(0x2205|vibration_enable_bit) && mode_of(t,1)==0x2205);
                check(t.order.back().second==0x2101 && t.ref_writes.size()==1 && t.ref_writes[0].value==0x2215);
                for (const auto& w:t.writes) check(vibration_write_allowed(2,w));
                const auto writes=t.writes.size();
                b.update(t,BuffetTarget{on,9000,0},soft,session,go); // unchanged: nothing more
                check(t.writes.size()==writes && t.ref_writes.size()==1);
                b.update(t,BuffetTarget{on,0,0},soft,session,go); // the stall ends: bit off, then zero
                check(mode_of(t,2)==0x2205 && t.values[LoadingTransport::key(2,vibration_slow_force)]==0);
                check(t.order[t.order.size()-2].second==0x2101 && t.order.back().second==0x2108);
                b.update(t,BuffetTarget{on,200,120},Tuning{20,100,0,2250,0},session,go); // pitch held to its headroom
                check(t.values[LoadingTransport::key(2,vibration_slow_force)]==200 && t.values[LoadingTransport::key(1,vibration_slow_force)]==120);
                b.update(t,BuffetTarget{on,450,450},Tuning{100,100,0,2250,0},session,go);
                check(b.units()[1]==450 && b.units()[0]==450 && mode_of(t,1)==0x2215 && mode_of(t,2)==0x2215);
                // With the motors on, a clear refuses the bit (not the stopped
                // spring mode) but still zeroes the amplitude, and stays owed.
                check(!b.clear(t) && b.dirty() && mode_of(t,1)==0x2215 && t.values[LoadingTransport::key(1,vibration_slow_force)]==0);
                // The stop: pitch as the session's own stop leaves it, roll as
                // a reconnect's stop leaves a drive whose bit stuck.
                t.values[LoadingTransport::key(2,force_mode)]=0x0205;t.values[LoadingTransport::key(1,force_mode)]=0x0215;
                check(b.clear(t) && !b.dirty());
                for (unsigned node=1;node<=2;++node)
                    check(mode_of(t,node)==0x0205 && t.values[LoadingTransport::key(node,vibration_slow_force)]==0);
                for (unsigned node=1;node<=2;++node) check(t.values[LoadingTransport::key(node,vibration_slow_time)]==40); // the time as found
                check(!b.time_unrestored()[0] && !b.time_unrestored()[1]);
                const auto after=t.writes.size()+t.ref_writes.size();
                check(b.clear(t) && t.writes.size()+t.ref_writes.size()==after); // nothing left to clear
            }
            // Turned off mid-session: what it set is cleared, and nothing new written.
            {
                auto t=in_session();BuffetDrive b;
                b.update(t,BuffetTarget{on,450,450},soft,session,go);
                b.update(t,BuffetTarget{off,450,450},soft,session,go);
                for (unsigned node=1;node<=2;++node)
                    check(mode_of(t,node)==0x2205 && t.values[LoadingTransport::key(node,vibration_slow_force)]==0);
                for (const auto& w:t.writes) check(!same_register(w.reg,vibration_slow_force) || w.value<=450);
            }
            // A write the drive does not keep stops the session; the clear still runs.
            {
                auto t=in_session();BuffetDrive b;t.mismatch=true;
                rejects([&]{b.update(t,BuffetTarget{on,450,0},soft,session,go);});
                check(b.dirty());
                t.fail=true;check(!b.clear(t) && b.dirty()); // a lost link: still owed
                t.fail=false;t.mismatch=false;t.values[LoadingTransport::key(2,force_mode)]=0x0205;
                check(b.clear(t) && !b.dirty());
            }
            // Bit 4 goes only on the mode the session wrote: any other bit, or
            // a mode the session did not write, is refused before writing.
            for (const std::uint32_t found:{0x2207u,0x0205u,0x2225u}) {
                auto t=in_session();BuffetDrive b;t.values[LoadingTransport::key(2,force_mode)]=found;
                rejects([&]{b.update(t,BuffetTarget{on,450,0},soft,session,go);});
                check(t.ref_writes.empty() && mode_of(t,2)==found);
            }
            // A found time outside 10..140 is never written back: the clear
            // says so, and later sessions keep it as the value to restore.
            for (const std::uint32_t found:{0u,300u}) {
                auto t=in_session();BuffetDrive b;
                t.values[LoadingTransport::key(2,vibration_slow_time)]=found;
                b.update(t,BuffetTarget{on,450,0},soft,session,go);
                t.values[LoadingTransport::key(2,force_mode)]=0x0205;
                check(b.clear(t) && !b.dirty() && t.values[LoadingTransport::key(2,vibration_slow_time)]==80);
                check(b.time_unrestored()[1]==found && !b.time_unrestored()[0]);
                for (const auto& w:t.writes) check(!same_register(w.reg,vibration_slow_time) || w.value==80);
                t.values[LoadingTransport::key(2,force_mode)]=0x2205;
                b.update(t,BuffetTarget{on,450,0},soft,session,go); // a later session
                t.values[LoadingTransport::key(2,force_mode)]=0x0205;
                check(b.clear(t) && b.time_unrestored()[1]==found);
            }
            // A found time equal to the knob's is neither written nor restored.
            {
                auto t=in_session();BuffetDrive b;
                t.values[LoadingTransport::key(2,vibration_slow_time)]=80;
                b.update(t,BuffetTarget{on,450,0},soft,session,go);
                t.values[LoadingTransport::key(2,force_mode)]=0x0205;
                check(b.clear(t) && !b.time_unrestored()[1]);
                for (const auto& w:t.writes) check(!same_register(w.reg,vibration_slow_time));
            }
        }
        std::cout<<"Protocol, signed values, calibration, reference and freshness checks passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
