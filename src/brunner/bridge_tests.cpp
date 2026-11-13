// Tests for the HID seam: the list of bridge ids (hid/usb_ids.hpp), bridge
// selection (hid/hid_device.hpp) and the shared request/reply layer (brunner/bridge.hpp)
// over a fake HidDevice. No hardware, no OS HID API. Spec: docs/hid-bridge.md.
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
    void flush() override {}
};
struct Rig {
    std::vector<Report> sent;
    FakeDevice* device=nullptr;
    std::unique_ptr<Bridge> bridge;
    std::atomic<bool> stop{false};
    Rig() {auto d=std::make_unique<FakeDevice>(&sent);device=d.get();bridge=std::make_unique<Bridge>(std::move(d));}
    void will_reply(Report r) {device->script.push_back([r](Report& out){out=r;});}
    void will_fail() {device->script.push_back([](Report&){throw TransportError("HID I/O timed out");});}
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
        a_stop_cancels_before_anything_is_sent();
        silence_ends_at_the_deadline();
        std::cout<<"bridge tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
