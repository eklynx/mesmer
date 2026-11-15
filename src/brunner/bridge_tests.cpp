// Tests for the HID seam: the list of bridge ids (hid/usb_ids.hpp), bridge
// selection (hid/hid_device.hpp) and the shared request/reply layer (brunner/bridge.hpp)
// over a fake HidDevice. No hardware, no OS HID API. Spec: docs/dev/hid-bridge.md.
#include "brunner/bridge.hpp"
#include "hid/usb_ids.hpp"
#include <deque>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

using namespace brunner;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(x) check_at((x),__LINE__)
template<class Error> bool throws(const std::function<void()>& f,const char* contains=nullptr) {
    try {f();} catch (const Error& e) {return !contains || std::string(e.what()).find(contains)!=std::string::npos;}
    catch (...) {return false;}
    return false;
}
Report reply(unsigned node,Object object,std::uint32_t value) {
    auto r=read_request(node,object,0);
    r[1]=static_cast<std::uint8_t>((1u<<4)|1); r[4]=static_cast<std::uint8_t>(0x80+node); r[5]=0x85;
    r[6]=width(object)==2?0x4b:0x43;
    for (unsigned i=0;i<4;++i) r[10+i]=static_cast<std::uint8_t>(value>>(i*8));
    return r;
}
HidCandidate bridge_candidate(std::uint16_t vendor=0x25bb,std::uint16_t product=0x0081) {
    HidCandidate c;c.vendor=vendor;c.product=product;c.usage_page=0xff00;c.usage=0xff02;c.input_length=64;c.output_length=64;
    return c;
}

// Scripted device: each read() pops the next step; an empty script means "unrelated
// traffic every 10 ms" so a caller waiting for a reply runs into its deadline.
struct FakeDevice : HidDevice {
    UsbId id() const override {return {0x25bb,0x0081};}
    std::deque<std::function<void(Report&)>> script;
    std::vector<Report>* writes;
    bool fail_write=false;
    explicit FakeDevice(std::vector<Report>* sent):writes(sent) {}
    void write(const Report& r,int) override {
        if (fail_write) throw TransportError("HID I/O failed");
        writes->push_back(r);
    }
    void read(Report& r,int) override {
        if (script.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            r=reply(2,Object::encoder,0);
            return;
        }
        auto step=std::move(script.front());script.pop_front();step(r);
    }
    int flushes=0;
    void flush() override {++flushes;}
    std::string describe() const override {return "fake OS: 7 reports";}
};
struct Rig {
    std::vector<Report> sent;
    FakeDevice* device=nullptr;
    std::unique_ptr<Bridge> bridge;
    std::atomic<bool> stop{false};
    Rig() {auto d=std::make_unique<FakeDevice>(&sent);device=d.get();bridge=std::make_unique<Bridge>(std::move(d));}
    void will_reply(Report r) {device->script.push_back([r](Report& out){out=r;});}
    void will_fail() {device->script.push_back([](Report&){throw TransportError("HID I/O timed out");});}
    void will_time_out() {device->script.push_back([](Report&){throw ReadTimeout{};});}
};

void the_id_list() {
    bool has_0081=false,has_008b=false;
    std::set<std::pair<int,int>> seen;
    for (const auto& id:bridge_ids) {
        check(seen.insert({id.vendor,id.product}).second); // no duplicates
        has_0081=has_0081 || (id.vendor==0x25bb && id.product==0x0081);
        has_008b=has_008b || (id.vendor==0x25bb && id.product==0x008b);
    }
    check(has_0081 && has_008b);
    check(is_bridge_id(0x25bb,0x0081) && is_bridge_id(0x25bb,0x008b));
    check(!is_bridge_id(0x25bb,0x0082) && !is_bridge_id(0x1234,0x0081) && !is_bridge_id(0,0));
}
void selection() {
    // Each listed id is accepted, alone and among unrelated devices.
    for (const auto& id:bridge_ids) {
        std::vector<HidCandidate> list{HidCandidate{},bridge_candidate(id.vendor,id.product),HidCandidate{}};
        check(select_bridge(list,default_usb_ids())==1);
    }
    // A listed id with the wrong descriptor is not the bridge.
    auto bad=bridge_candidate();bad.usage_page=0x0001;check(throws<BridgeAbsent>([&]{select_bridge({bad},default_usb_ids());}));
    bad=bridge_candidate();bad.usage=0x0004;check(throws<BridgeAbsent>([&]{select_bridge({bad},default_usb_ids());}));
    bad=bridge_candidate();bad.input_length=32;check(throws<BridgeAbsent>([&]{select_bridge({bad},default_usb_ids());}));
    bad=bridge_candidate();bad.output_length=65;check(throws<BridgeAbsent>([&]{select_bridge({bad},default_usb_ids());}));
    // The right descriptor on an id that is not listed is not the bridge either.
    check(throws<BridgeAbsent>([&]{select_bridge({bridge_candidate(0x25bb,0x0082)},default_usb_ids());}));
    check(throws<BridgeAbsent>([&]{select_bridge({},default_usb_ids());}));
    // More than one: refuse, even for two different listed ids.
    check(throws<std::runtime_error>([&]{select_bridge({bridge_candidate(),bridge_candidate()},default_usb_ids());},"found several"));
    check(throws<std::runtime_error>([&]{select_bridge({bridge_candidate(0x25bb,0x0081),bridge_candidate(0x25bb,0x008b)},default_usb_ids());},"found several"));
}
// The ids come from the yoke presets, so a model added to the presets file is found
// and one that is not named there is not, whatever the built-ins say.
void the_id_list_is_the_callers() {
    const std::vector<UsbId> club{{0x1234,0x5678}};
    check(select_bridge({HidCandidate{},bridge_candidate(0x1234,0x5678)},club)==1);          // an id the built-ins lack
    check(throws<BridgeAbsent>([&]{select_bridge({bridge_candidate(0x25bb,0x0081)},club);})); // a built-in id not in this list
    check(throws<BridgeAbsent>([&]{select_bridge({bridge_candidate()},{});}));               // an empty list opens nothing
    // Both models listed and both present: still refused, not guessed.
    check(throws<std::runtime_error>([&]{select_bridge({bridge_candidate(0x1234,0x5678),bridge_candidate(0x25bb,0x0081)},
        {{0x1234,0x5678},{0x25bb,0x0081}});},"found several"));
}
// An unknown model: a Brunner bridge whose product is not listed. Only when no known bridge is present,
// only for a listed vendor, and only with the bridge's descriptor.
void unknown_models_of_a_listed_vendor() {
    const std::vector<std::uint16_t> brunner{brunner_vendor};
    const auto unknown=bridge_candidate(0x25bb,0x00ff);
    check(select_bridge({HidCandidate{},unknown},default_usb_ids(),brunner)==1);                       // opened as an unknown model
    check(throws<BridgeAbsent>([&]{select_bridge({unknown},default_usb_ids());}));                     // no vendor list: not opened
    check(throws<BridgeAbsent>([&]{select_bridge({unknown},default_usb_ids(),{0x1234});}));           // another vendor's list
    check(throws<BridgeAbsent>([&]{select_bridge({bridge_candidate(0x1234,0x00ff)},default_usb_ids(),brunner);})); // another vendor's device
    auto wrong=unknown;wrong.usage_page=0x0001;
    check(throws<BridgeAbsent>([&]{select_bridge({wrong},default_usb_ids(),brunner);}));              // wrong descriptor
    wrong=unknown;wrong.input_length=32;
    check(throws<BridgeAbsent>([&]{select_bridge({wrong},default_usb_ids(),brunner);}));
    // A known bridge wins over other Brunner devices: no "found several", the known one is chosen.
    check(select_bridge({unknown,bridge_candidate(0x25bb,0x008b)},default_usb_ids(),brunner)==1);
    check(select_bridge({bridge_candidate(0x25bb,0x0081),unknown},default_usb_ids(),brunner)==0);
    // Two known ones, or (with none known) two unknown ones, are refused.
    check(throws<std::runtime_error>([&]{select_bridge({bridge_candidate(0x25bb,0x0081),bridge_candidate(0x25bb,0x008b),unknown},default_usb_ids(),brunner);},"found several"));
    check(throws<std::runtime_error>([&]{select_bridge({unknown,bridge_candidate(0x25bb,0x00fe)},default_usb_ids(),brunner);},"found several"));
}
void a_bridge_reports_the_id_it_opened() {
    Rig rig;
    check(rig.bridge->id().vendor==0x25bb && rig.bridge->id().product==0x0081);
}
void a_read_returns_the_matching_reply() {
    Rig rig;
    rig.will_reply(reply(1,Object::status,0x1234));
    check(rig.bridge->read(1,Object::status,rig.stop)==0x1234);
    check(rig.sent.size()==1 && rig.sent[0][0]==0x3f && rig.sent[0].size()==64);
}
void unrelated_replies_are_skipped() {
    Rig rig;
    rig.will_reply(reply(2,Object::status,7));          // another controller
    rig.will_reply(reply(1,Object::encoder,9));         // another object
    rig.will_reply(reply(1,Object::status,0x4321));
    check(rig.bridge->read(1,Object::status,rig.stop)==0x4321);
    check(rig.device->script.empty());
}
void transport_failures_surface() {
    Rig failing_read;failing_read.will_fail();
    check(throws<TransportError>([&]{failing_read.bridge->read(1,Object::status,failing_read.stop);},"timed out"));
    Rig failing_write;failing_write.device->fail_write=true;
    check(throws<TransportError>([&]{failing_write.bridge->read(1,Object::status,failing_write.stop);},"failed"));
    check(failing_write.sent.empty()); // nothing was sent
}
void a_stall_is_explained() {
    LinkDiagnostics diag;
    const auto has=[](const std::string& text,const char* part) {return text.find(part)!=std::string::npos;};
    // Silence after a good exchange: the read times out while waiting.
    Rig silent;silent.bridge->set_diagnostics(&diag);
    silent.will_reply(reply(1,Object::status,5159));
    check(silent.bridge->read(1,Object::status,silent.stop)==5159);
    silent.will_fail();
    check(throws<TransportError>([&]{silent.bridge->read(2,Object::encoder,silent.stop);},"timed out"));
    auto text=diag.describe(Clock::now(),"HID I/O timed out");
    check(has(text,"Yoke link stall: HID I/O timed out while waiting for the reply") && has(text,"2 requests and 1 replies this session"));
    check(has(text,"fake OS: 7 reports") && has(text,"last good reply") && has(text,"[")&& !has(text,"never"));
    // The facts are frozen when the error is thrown: later exchanges (the emergency stop) do not change them.
    const auto frozen=diag.at_error;
    check(has(frozen,"while waiting for the reply") && has(frozen,"HID I/O timed out"));
    silent.will_reply(reply(1,Object::status,5159));
    check(silent.bridge->read(1,Object::status,silent.stop)==5159);
    check(diag.at_error==frozen && has(diag.describe(Clock::now(),"x"),"between requests"));
    // A failed write is told apart from a missing reply.
    LinkDiagnostics diag2;
    Rig broken;broken.bridge->set_diagnostics(&diag2);broken.device->fail_write=true;
    check(throws<TransportError>([&]{broken.bridge->read(1,Object::status,broken.stop);},"failed"));
    check(has(diag2.at_error,"while writing a request") && has(diag2.at_error,"0 replies"));
    // Replies that are not the one asked for are counted.
    LinkDiagnostics diag3;
    Rig noisy;noisy.bridge->set_diagnostics(&diag3);
    noisy.will_reply(reply(2,Object::encoder,1));noisy.will_reply(reply(2,Object::encoder,2));noisy.will_fail();
    check(throws<TransportError>([&]{noisy.bridge->read(1,Object::status,noisy.stop);}));
    check(has(diag3.at_error,"2 other reports arrived since it was sent") && has(diag3.at_error,"(2 unmatched, 0 retries; reply times"));
    // Without diagnostics the bridge behaves as before.
    Rig plain;plain.will_fail();
    check(throws<TransportError>([&]{plain.bridge->read(1,Object::status,plain.stop);},"timed out"));
}
// What a controller answers to the last write: the request, acknowledged (SDO "write done", node 0x580 + n).
Report write_ack(const Report& request) {
    Report r=request;
    r[1]=0x11;r[4]=static_cast<std::uint8_t>(0x80+request[4]);r[5]=0x85;r[6]=0x60;
    for (unsigned i=0;i<4;++i) r[10+i]=0;
    return r;
}
void a_loading_write_is_sent_again_until_its_reply_comes() {
    const RegisterWrite op{trim_offset,0,0};
    LinkDiagnostics diag;
    // Two attempts get no reply, the third is answered: the one write, sent three times, succeeds.
    Rig rig;rig.bridge->set_diagnostics(&diag);
    rig.will_time_out();rig.will_time_out();
    rig.device->script.push_back([&rig](Report& out){out=write_ack(rig.sent.back());});
    rig.bridge->load_write(1,op,rig.stop);
    check(rig.sent.size()==3 && diag.retries==2 && (rig.sent[0][6]==0x23 || rig.sent[0][6]==0x2b));
    check(rig.sent[0][10]==rig.sent[2][10] && rig.sent[0][7]==rig.sent[2][7]);       // the same register and value each time
    check(diag.at_error.empty());                                                    // a retried miss leaves no failure behind
    // Nothing ever answers: it ends after the tolerance (250 ms), as a plain ReplyMissed.
    Rig mute;mute.bridge->set_diagnostics(&diag);
    const auto begin=Clock::now();
    check(throws<ReplyMissed>([&]{mute.bridge->load_write(1,op,mute.stop);}));
    const auto took=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
    check(took>=load_write_tolerance_ms && took<load_write_tolerance_ms+150 && mute.sent.size()>=5);
    check(diag.describe(Clock::now(),"x").find(" retries; reply times")!=std::string::npos);
    // A failed link is not retried.
    Rig dead;dead.device->script.push_back([](Report&){throw TransportError("HID I/O failed");});
    check(throws<TransportError>([&]{dead.bridge->load_write(1,op,dead.stop);},"failed") && dead.sent.size()==1);
    static_assert(load_write_reply_timeout_ms==20 && load_write_tolerance_ms==250,"the figures the docs state");
}
void the_reply_timeout_is_the_callers() {
    // A sampling read gives up after the sample period; the default stays 250 ms.
    Rig quick;
    auto begin=Clock::now();
    check(throws<ReplyMissed>([&]{quick.bridge->read(1,Object::status,quick.stop,sample_reply_timeout_ms);},"No matching controller reply"));
    auto took=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
    check(took>=sample_reply_timeout_ms-2 && took<150);
    Rig patient;
    begin=Clock::now();
    check(throws<ReplyMissed>([&]{patient.bridge->read(1,Object::status,patient.stop);}));
    took=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
    check(took>=default_reply_timeout_ms-5);
    static_assert(sample_reply_timeout_ms==sample_period_ms && sample_period_ms==20 && default_reply_timeout_ms==250,"the figures the docs state");
}
void a_request_is_named_in_the_log() {
    const auto text=[](const Report& r) {std::array<std::uint8_t,14> head{};std::copy_n(r.begin(),head.size(),head.begin());return request_text(head);};
    const auto has=[](const std::string& s,const char* part) {return s.find(part)!=std::string::npos;};
    // The sampling reads, and a firmware read.
    check(text(read_request(2,Object::status,1))=="node 2 read of status word (sampling) (0x6041:0)");
    check(text(read_request(1,Object::encoder,1))=="node 1 read of encoder position (sampling) (0x6064:0)");
    check(has(text(read_request(1,Object::digital_inputs,1)),"digital inputs (buttons)"));
    // Register reads: the ones the force-feel loop's confirmation and read-backs make.
    check(text(reference_read_request(2,trim_position,1))=="node 2 read of trim position (0x2116:0)");
    check(has(text(reference_read_request(1,trim_offset,1)),"trim offset (0x2115:0)") && has(text(reference_read_request(2,manual_force_bias,1)),"constant force (bias) (0x211A:0)"));
    check(has(text(reference_read_request(2,center_mode,1)),"centre mode (0x2101:4)") && has(text(reference_read_request(1,force_mode,1)),"force mode (0x2101:1)"));
    const auto at=[&](unsigned index,unsigned sub) {auto r=reference_read_request(1,trim_offset,1);r[7]=static_cast<std::uint8_t>(index);r[8]=static_cast<std::uint8_t>(index>>8);r[9]=static_cast<std::uint8_t>(sub);return r;};
    check(has(text(at(0x2113,3)),"spring curve point 3 (0x2113:3)"));
    // Writes say so, with the value: a two-byte one, a four-byte one that is negative, and a register it has no name for.
    check(text(loading_write_request(1,{force_scale,40,0},1))=="node 1 write of spring strength (0x2114:0) = 40");
    check(text(loading_write_request(2,{trim_position,static_cast<std::uint32_t>(-500),0},1))=="node 2 write of trim position (0x2116:0) = -500");
    check(has(text(at(0x1234,2)),"unnamed register (0x1234:2)"));
    // And the stall line carries it.
    LinkDiagnostics diag;
    Rig rig;rig.bridge->set_diagnostics(&diag);rig.will_fail();
    check(throws<TransportError>([&]{rig.bridge->read(2,Object::encoder,rig.stop);}));
    check(has(diag.at_error,"request node 2 read of encoder position (sampling) (0x6064:0) ["));
}
void a_read_that_is_not_a_sample_is_retried() {
    LinkDiagnostics diag;
    // The first attempt gets no reply, the second is answered: one read, sent twice, returns the value.
    Rig rig;rig.bridge->set_diagnostics(&diag);
    rig.will_time_out();rig.will_reply(reply(1,Object::status,5159));
    check(rig.bridge->read(1,Object::status,rig.stop)==5159 && rig.sent.size()==2 && diag.retries==1 && diag.at_error.empty());
    check(rig.sent[0][7]==rig.sent[1][7] && rig.sent[0][8]==rig.sent[1][8]);          // the same object both times
    // A reference register read is retried the same way (the read-back after a write).
    Rig reg;reg.bridge->set_diagnostics(&diag);
    reg.will_time_out();
    reg.device->script.push_back([&reg](Report& out){out=write_ack(reg.sent.back());out[6]=0x43;out[10]=7;});
    check(reg.bridge->ref_read(1,trim_offset,reg.stop)==7 && reg.sent.size()==2);
    // A single-attempt read (a sample) is not: it ends at its own timeout.
    Rig once;once.will_time_out();
    check(throws<ReplyMissed>([&]{once.bridge->read(1,Object::status,once.stop,sample_reply_timeout_ms);}) && once.sent.size()==1);
    // Never answered: it gives up after the tolerance, not before.
    Rig mute;
    const auto begin=Clock::now();
    check(throws<ReplyMissed>([&]{mute.bridge->read(1,Object::status,mute.stop);}));
    const auto took=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
    check(took>=load_write_tolerance_ms && took<load_write_tolerance_ms+150 && mute.sent.size()>=5);
}
void a_missed_reply_is_told_apart_from_a_failed_link() {
    // A read that times out is a missed reply (ReplyMissed, still a TransportError) and drops what was queued.
    Rig quiet;quiet.will_time_out();
    check(throws<ReplyMissed>([&]{quiet.bridge->read(1,Object::status,quiet.stop,sample_reply_timeout_ms);},"timed out") && quiet.device->flushes==1);   // one attempt
    // Silence until the deadline, and traffic that is not the reply, are missed replies too.
    Rig noisy;
    check(throws<ReplyMissed>([&]{noisy.bridge->read(1,Object::status,noisy.stop);},"No matching controller reply"));
    // A failed or removed device, and a failed write, are not: they stay plain transport errors.
    Rig removed;removed.device->script.push_back([](Report&){throw TransportError("HID I/O failed");});
    check(throws<TransportError>([&]{removed.bridge->read(1,Object::status,removed.stop);},"failed"));
    Rig removed_again;removed_again.device->script.push_back([](Report&){throw TransportError("HID I/O failed");});
    check(!throws<ReplyMissed>([&]{removed_again.bridge->read(1,Object::status,removed_again.stop);}));
    Rig failing_write;failing_write.device->fail_write=true;
    check(!throws<ReplyMissed>([&]{failing_write.bridge->read(1,Object::status,failing_write.stop);}));
    // Reply times are kept for the stall line.
    LinkDiagnostics diag;
    Rig timed;timed.bridge->set_diagnostics(&diag);
    timed.will_reply(reply(1,Object::status,1));
    timed.bridge->read(1,Object::status,timed.stop);
    check(diag.latency[0]+diag.latency[1]+diag.latency[2]+diag.latency[3]+diag.latency[4]+diag.latency[5]+diag.latency[6]+diag.latency[7]+diag.latency[8]==1);
    check(diag.latency_text().find("slowest")!=std::string::npos);
}
void a_stop_cancels_before_anything_is_sent() {
    Rig rig;rig.stop=true;
    check(throws<OperationCancelled>([&]{rig.bridge->read(1,Object::status,rig.stop);}));
    check(rig.sent.empty());
}
void silence_ends_at_the_deadline() {
    Rig rig; // empty script: only unrelated traffic arrives
    const auto begin=Clock::now();
    check(throws<TransportError>([&]{rig.bridge->read(1,Object::status,rig.stop);},"No matching controller reply"));
    const auto took=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
    check(took>=200 && took<2000);
}
}
int main() {
    try {
        the_id_list();
        selection();
        the_id_list_is_the_callers();
        unknown_models_of_a_listed_vendor();
        a_bridge_reports_the_id_it_opened();
        a_read_returns_the_matching_reply();
        unrelated_replies_are_skipped();
        transport_failures_surface();
        a_stall_is_explained();
        a_missed_reply_is_told_apart_from_a_failed_link();
        a_read_that_is_not_a_sample_is_retried();
        a_request_is_named_in_the_log();
        the_reply_timeout_is_the_callers();
        a_loading_write_is_sent_again_until_its_reply_comes();
        a_stop_cancels_before_anything_is_sent();
        silence_ends_at_the_deadline();
        std::cout<<"bridge tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
