#pragma once
#include "core.hpp"
#include <vector>
#include <atomic>
#include <thread>
#include <sstream>

namespace brunner {
struct Register { std::uint16_t index; std::uint8_t sub, bytes; };
inline constexpr Register controlword{0x6040,0,2};
inline constexpr Register force_mode{0x2101,1,2};
inline constexpr Register center_mode{0x2101,4,2};
inline constexpr Register center_position{0x2117,0,4};
struct RegisterWrite { Register reg; std::uint32_t value; unsigned settle_ms; };
struct SdoAbort : std::runtime_error {
    std::uint32_t code;
    explicit SdoAbort(std::uint32_t value):std::runtime_error("Controller rejected SDO transaction"),code(value) {}
};
inline bool reference_register(Register r) {
    return (r.bytes==2 && ((r.index==controlword.index && r.sub==controlword.sub)
        || (r.index==force_mode.index && r.sub==force_mode.sub)
        || (r.index==center_mode.index && r.sub==center_mode.sub)))
        || (r.bytes==4 && r.index==center_position.index && r.sub==0);
}
inline bool loading_read_register(Register r) {
    return (r.index==0x2113 && r.sub>=1 && r.sub<=18 && r.bytes==2)
        || (r.index==0x20a1 && r.sub>=1 && r.sub<=18 && r.bytes==4)
        || (r.sub==0 && r.bytes==2 && (r.index==0x2114 || r.index==0x211a || r.index==0x211b || r.index==0x211f || r.index==0x6071 || r.index==0x6072))
        || (r.index==0x2100 && (r.sub==1 || r.sub==2) && r.bytes==2) // the pitch drag gains
        || (r.sub==0 && r.bytes==4 && (r.index==0x2102 || r.index==0x2115 || r.index==0x2116))
        || (r.sub==0 && r.bytes==2 && (r.index==0x2108 || r.index==0x2109 || r.index==0x2099 || r.index==0x209a)); // the vibration generator (vibration.hpp)
}
inline Report reference_write_request(unsigned node, const RegisterWrite& op, std::uint16_t counter) {
    if (!reference_register(op.reg) || op.value>65535
        || (op.reg.index==center_position.index && op.value!=0)
        || (op.reg.index==center_mode.index && op.reg.sub==center_mode.sub && op.value>1))
        throw std::invalid_argument("Unsupported reference write");
    auto r=read_request(node,Object::status,counter);
    r[6]=op.reg.bytes==2?0x2b:0x23; r[7]=static_cast<std::uint8_t>(op.reg.index);
    r[8]=static_cast<std::uint8_t>(op.reg.index>>8); r[9]=op.reg.sub;
    r[10]=static_cast<std::uint8_t>(op.value); r[11]=static_cast<std::uint8_t>(op.value>>8);
    return r;
}
inline Report reference_read_request(unsigned node,Register reg,std::uint16_t counter) {
    if (!reference_register(reg) && !loading_read_register(reg)) throw std::invalid_argument("Unsupported controller read register");
    auto r=read_request(node,Object::status,counter);
    r[7]=static_cast<std::uint8_t>(reg.index);r[8]=static_cast<std::uint8_t>(reg.index>>8);r[9]=reg.sub;
    return r;
}
inline std::optional<std::uint32_t> reference_reply(const Report& r,unsigned node,Register reg,bool written) {
    if (node<1 || node>2 || (!reference_register(reg) && !loading_read_register(reg))) throw std::invalid_argument("Unsupported controller transaction");
    // Reuse transport validation for all slots before interpreting an acknowledgement.
    (void)read_reply(r,node,Object::firmware);
    const unsigned kind=r[1]>>4,count=r[1]&15;
    std::optional<std::uint32_t> result;
    for (unsigned i=0;i<count;++i) {
        const auto* slot=r.data()+4+i*(kind==2?14:10);
        const auto header=u16(slot);
        if ((header&0x7ffu)!=0x580u+node) continue;
        const auto* d=slot+2;
        if (u16(d+1)!=reg.index || d[3]!=reg.sub) continue;
        if (d[0]==0x80) throw SdoAbort(u32(d+4));
        const auto command=written?0x60:reg.bytes==2?0x4b:0x43;
        if (d[0]!=command) throw std::runtime_error("Unexpected reference acknowledgement");
        if (result) throw std::runtime_error("Duplicate reference acknowledgement");
        result=written?0:reg.bytes==2?u16(d+4):u32(d+4);
    }
    return result;
}
// Original v367 has nine symmetric entries. Optional position registers can
// abort without preventing inspection of the other axis or its force limits.
template<class Transport> std::string inspect_loading(Transport& bridge,const std::atomic<bool>& stop) {
    std::ostringstream report;
    for (unsigned node=1;node<=2;++node) {
        report<<"node="<<node<<" profile=";
        for (unsigned sub=1;sub<=9;++sub)
            report<<bridge.ref_read(node,{0x2113,static_cast<std::uint8_t>(sub),2},stop)<<',';
        for (const auto reg:{Register{0x2114,0,2},Register{0x211a,0,2},Register{0x211b,0,2},Register{0x6072,0,2},Register{0x2115,0,4},Register{0x2116,0,4},Register{0x6071,0,2},force_mode,center_mode}) {
            report<<" reg="<<std::hex<<reg.index<<std::dec<<":";
            try {
                const auto value=bridge.ref_read(node,reg,stop);
                if (reg.bytes==4) report<<signed32(value);
                else if (reg.index==0x211a) report<<(value>=0x8000?static_cast<int>(value)-0x10000:static_cast<int>(value));
                else report<<value;
            } catch (const SdoAbort& e) {report<<"unsupported(0x"<<std::hex<<e.code<<std::dec<<')';}
        }
        report<<" positions=";
        for (unsigned sub=1;sub<=9;++sub) {
            try {report<<signed32(bridge.ref_read(node,{0x20a1,static_cast<std::uint8_t>(sub),4},stop))<<',';}
            catch (const SdoAbort& e) {report<<"unsupported(0x"<<std::hex<<e.code<<std::dec<<')';break;}
        }
        report<<"; ";
    }
    return report.str();
}
// The read-only survey of a controller this plugin does not drive: the live
// objects and every register the allowlist can read, as JSON. A register the
// controller lacks (an SDO abort) is recorded for that register and does not end
// the survey. SDO uploads only; nothing is written.
template<class Transport> std::string survey_controllers(Transport& bridge,const std::atomic<bool>& stop) {
    std::ostringstream out;
    const auto hex=[](std::uint32_t v) {std::ostringstream h;h<<"0x"<<std::hex<<v;return h.str();};
    out<<'{';
    for (unsigned node=1;node<=2;++node) {
        out<<(node>1?",":"")<<"\"node"<<node<<"\":{\"live\":{";
        out<<"\"firmware\":"<<bridge.read(node,Object::firmware,stop)<<",\"status\":"<<bridge.read(node,Object::status,stop)
           <<",\"encoder\":"<<signed32(bridge.read(node,Object::encoder,stop))<<",\"position\":"<<signed32(bridge.read(node,Object::position,stop))<<"},\"registers\":{";
        bool first=true;
        const auto read_register=[&](Register reg) {
            out<<(first?"":",")<<"\""<<hex(reg.index)<<"."<<static_cast<unsigned>(reg.sub)<<"\":";
            first=false;
            try {
                const auto value=bridge.ref_read(node,reg,stop);
                if (reg.bytes==4) out<<signed32(value);
                else if (reg.index==0x211a) out<<(value>=0x8000?static_cast<int>(value)-0x10000:static_cast<int>(value));
                else out<<value;
            } catch (const SdoAbort& e) {out<<"\"unsupported "<<hex(e.code)<<"\"";}
        };
        for (unsigned sub=1;sub<=18;++sub) read_register({0x2113,static_cast<std::uint8_t>(sub),2});
        for (unsigned sub=1;sub<=18;++sub) read_register({0x20a1,static_cast<std::uint8_t>(sub),4});
        for (const auto reg:{Register{0x2114,0,2},Register{0x211a,0,2},Register{0x211b,0,2},Register{0x211f,0,2},Register{0x6071,0,2},Register{0x6072,0,2},
                             Register{0x2100,1,2},Register{0x2100,2,2},Register{0x2102,0,4},Register{0x2115,0,4},Register{0x2116,0,4},
                             Register{0x2108,0,2},Register{0x2109,0,2},Register{0x2099,0,2},Register{0x209a,0,2},
                             controlword,force_mode,center_mode,center_position})
            read_register(reg);
        out<<"}}";
    }
    out<<'}';
    return out.str();
}
inline std::uint16_t stopped_controlword(std::uint16_t word) {
    return static_cast<std::uint16_t>((word & ~0x81u) | 0x06u);
}
// Recovered from CLS2Sim 5.9 MicroDriveDevice.PrepareUpdate, RVA 0xb39d0,
// IL 0x938c..0x96a6. This is a testable command plan, not permission to execute.
// The spring mode with the bits CLS2Sim clears before a reference removed; a
// switch-on sets bit 13, the reference request, on top of it.
inline std::uint16_t spring_mode() {return active_drive().spring_mode;}
inline std::uint16_t base_spring_mode(std::uint16_t mode) {
    return static_cast<std::uint16_t>(mode & ~((1u<<13)|(1u<<11)|(1u<<5)|(1u<<6)));
}
inline std::vector<RegisterWrite> reference_start(std::uint16_t word,std::uint16_t mode) {
    mode=base_spring_mode(mode);
    word=stopped_controlword(word);
    return {
        {force_mode,mode,100},
        {controlword,word,100},
        {controlword,static_cast<std::uint16_t>(word|0x80u),100},
        {controlword,static_cast<std::uint16_t>(word|1u),100},
        {controlword,static_cast<std::uint16_t>(word|9u),100},
        {force_mode,static_cast<std::uint16_t>(mode|0x2000u),0}
    };
}
inline std::vector<RegisterWrite> reference_stop(std::uint16_t word,std::uint16_t mode) {
    return {{controlword,stopped_controlword(word),0},
            {force_mode,static_cast<std::uint16_t>(mode&~0x2000u),0}};
}
template<class Transport> bool stop_reference(Transport& bridge,
    const std::array<std::uint16_t,2>& words,const std::array<std::uint16_t,2>& modes) {
    const std::atomic<bool> cleanup{false};
    bool confirmed=true;
    for (unsigned i=0;i<2;++i) {
        // An error must not skip the remaining stop commands or the other axis.
        auto operations=reference_stop(words[i],modes[i]);
        operations.push_back({center_mode,0,0});
        for (const auto& op:operations) {
            try {bridge.ref_write(i+1,op,cleanup);}
            catch (const std::exception&) {confirmed=false;}
        }
        try {
            auto status=bridge.read(i+1,Object::status,cleanup);
            if (status&4) {std::this_thread::sleep_for(std::chrono::milliseconds(100));status=bridge.read(i+1,Object::status,cleanup);}
            if (status&4) confirmed=false;
        } catch (const std::exception&) {confirmed=false;}
    }
    return confirmed;
}
struct CenterEvidence {
    Clock::time_point stable_since{};
    std::int32_t previous=0;
    bool seen=false;
    bool observe(std::int32_t encoder,Clock::time_point now) {
        const auto position=static_cast<std::int64_t>(encoder);
        const auto delta=position-previous;
        if (seen && position>=-200 && position<=200 && delta>=-2 && delta<=2) {
            if (stable_since==Clock::time_point{}) stable_since=now;
        } else stable_since={};
        previous=encoder;seen=true;
        return stable_since!=Clock::time_point{} && now>=stable_since && now-stable_since>=std::chrono::milliseconds(500);
    }
};
}
