// macOS implementation of open_bridge() / HidDevice over IOKit's IOHIDManager.
// Contract: hid/hid_device.hpp (source of truth); notes: docs/hid-bridge.md.
//
// The bridge is opened exclusively (kIOHIDOptionsTypeSeizeDevice), like the
// Windows share-mode-0 open, so CLS2Sim or Simux holding it reads as BridgeBusy.
// Input reports arrive by callback on a private run-loop thread and are queued;
// read() waits on the queue with a timeout. write() uses the callback form of
// SetReport so the 100 ms write timeout is honoured.
//
// Not yet run against hardware: the report-ID handling and the report-size
// normalisation below are the assumptions the hardware smoke test checks.
#include "hid/mac/hid_mac.hpp"
#include "hid/hid_device.hpp"
#include "hid/usb_ids.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace brunner {
namespace {
// Owns one Core Foundation object (the Create/Copy rule); retain first for Get-rule objects.
template<class T> class Ref {
    T ref_=nullptr;
 public:
    Ref()=default;
    explicit Ref(T ref):ref_(ref) {}
    ~Ref() { if (ref_) CFRelease(ref_); }
    Ref(const Ref&)=delete;
    Ref& operator=(const Ref&)=delete;
    Ref(Ref&& other) noexcept:ref_(other.ref_) { other.ref_=nullptr; }
    Ref& operator=(Ref&& other) noexcept { if (this!=&other) { if (ref_) CFRelease(ref_); ref_=other.ref_; other.ref_=nullptr; } return *this; }
    T get() const { return ref_; }
};
bool number_property(IOHIDDeviceRef device,CFStringRef key,int& out) {
    const CFTypeRef value=IOHIDDeviceGetProperty(device,key);
    return value && CFGetTypeID(value)==CFNumberGetTypeID()
        && CFNumberGetValue(static_cast<CFNumberRef>(value),kCFNumberIntType,&out);
}
Ref<CFMutableArrayRef> matching_ids(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    Ref<CFMutableArrayRef> list(CFArrayCreateMutable(kCFAllocatorDefault,0,&kCFTypeArrayCallBacks));
    for (const auto& id:ids) {
        Ref<CFMutableDictionaryRef> match(CFDictionaryCreateMutable(kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks));
        const int vendor=id.vendor,product=id.product;
        Ref<CFNumberRef> vendor_number(CFNumberCreate(kCFAllocatorDefault,kCFNumberIntType,&vendor));
        Ref<CFNumberRef> product_number(CFNumberCreate(kCFAllocatorDefault,kCFNumberIntType,&product));
        CFDictionarySetValue(match.get(),CFSTR(kIOHIDVendorIDKey),vendor_number.get());
        CFDictionarySetValue(match.get(),CFSTR(kIOHIDProductIDKey),product_number.get());
        CFArrayAppendValue(list.get(),match.get());
    }
    // An unknown model of a listed vendor: matched on the vendor alone; select_bridge decides.
    for (const auto vendor_id:unknown_vendors) {
        Ref<CFMutableDictionaryRef> match(CFDictionaryCreateMutable(kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks));
        const int vendor=vendor_id;
        Ref<CFNumberRef> vendor_number(CFNumberCreate(kCFAllocatorDefault,kCFNumberIntType,&vendor));
        CFDictionarySetValue(match.get(),CFSTR(kIOHIDVendorIDKey),vendor_number.get());
        CFArrayAppendValue(list.get(),match.get());
    }
    return list;
}
// macOS reports a device's maximum report size with or without the report-ID
// byte. The bridge's reports are 64 bytes with the ID, so accept 63 as 64.
// ASSUMPTION to confirm on hardware (the smoke test prints the raw numbers).
int with_report_id(int size) { return size==63 ? 64 : size; }

struct Enumeration {
    Ref<IOHIDManagerRef> manager;
    std::vector<HidCandidate> candidates;
    std::vector<Ref<IOHIDDeviceRef>> devices; // same order as candidates
    std::string text;                          // raw values, for describe_mac_bridges
};
Enumeration enumerate(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors={}) {
    Enumeration found;
    found.manager=Ref<IOHIDManagerRef>(IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone));
    if (!found.manager.get()) throw TransportError("Cannot create HID manager");
    const auto list=matching_ids(ids,unknown_vendors);
    IOHIDManagerSetDeviceMatchingMultiple(found.manager.get(),list.get());
    const IOReturn opened=IOHIDManagerOpen(found.manager.get(),kIOHIDOptionsTypeNone);
    if (opened==kIOReturnNotPermitted) throw BridgeBusy{}; // typically: no Input Monitoring permission
    if (opened!=kIOReturnSuccess) throw TransportError("Cannot enumerate HID devices");
    Ref<CFSetRef> set(IOHIDManagerCopyDevices(found.manager.get()));
    if (set.get()) {
        std::vector<const void*> raw(static_cast<std::size_t>(CFSetGetCount(set.get())));
        CFSetGetValues(set.get(),raw.data());
        for (const void* item:raw) {
            auto* device=static_cast<IOHIDDeviceRef>(const_cast<void*>(item));
            int vendor=0,product=0,page=0,usage=0,input=0,output=0;
            if (!number_property(device,CFSTR(kIOHIDVendorIDKey),vendor) || !number_property(device,CFSTR(kIOHIDProductIDKey),product)) continue;
            number_property(device,CFSTR(kIOHIDPrimaryUsagePageKey),page);
            number_property(device,CFSTR(kIOHIDPrimaryUsageKey),usage);
            number_property(device,CFSTR(kIOHIDMaxInputReportSizeKey),input);
            number_property(device,CFSTR(kIOHIDMaxOutputReportSizeKey),output);
            char line[160];
            std::snprintf(line,sizeof(line),"vendor=0x%04x product=0x%04x usage_page=0x%04x usage=0x%04x input=%d output=%d\n",
                static_cast<unsigned>(vendor),static_cast<unsigned>(product),static_cast<unsigned>(page),static_cast<unsigned>(usage),input,output);
            found.text+=line;
            HidCandidate candidate;
            candidate.vendor=static_cast<std::uint16_t>(vendor);candidate.product=static_cast<std::uint16_t>(product);
            candidate.usage_page=static_cast<std::uint16_t>(page);candidate.usage=static_cast<std::uint16_t>(usage);
            candidate.input_length=static_cast<std::uint16_t>(with_report_id(input));
            candidate.output_length=static_cast<std::uint16_t>(with_report_id(output));
            found.candidates.push_back(candidate);
            CFRetain(device);
            found.devices.emplace_back(device);
        }
    }
    // Release the manager's own (shared) open so the seize below is the only holder.
    IOHIDManagerClose(found.manager.get(),kIOHIDOptionsTypeNone);
    return found;
}

class MacHidDevice final : public HidDevice {
    struct Input {Report report{};bool whole=false;};
    Enumeration found_; // keeps the manager and the device alive
    IOHIDDeviceRef device_=nullptr;
    bool opened_=false;
    UsbId id_{};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Input> queue_;
    bool dead_=false; // removed, or an I/O timed out: every later call fails
    bool write_done_=false;
    IOReturn write_result_=kIOReturnSuccess;
    Report out_{};
    std::array<std::uint8_t,256> input_buffer_{};
    std::atomic<bool> stop_{false};
    std::thread loop_;
    static void on_input(void* context,IOReturn,void*,IOHIDReportType,uint32_t id,uint8_t* data,CFIndex length) {
        auto& self=*static_cast<MacHidDevice*>(context);
        Input in;
        if (length==64 && data[0]==id) {std::memcpy(in.report.data(),data,64);in.whole=true;}
        else if (length==63) {in.report[0]=static_cast<std::uint8_t>(id);std::memcpy(in.report.data()+1,data,63);in.whole=true;}
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.queue_.push_back(in);
        if (self.queue_.size()>64) self.queue_.pop_front();
        self.changed_.notify_all();
    }
    static void on_removed(void* context,IOReturn,void*) {
        auto& self=*static_cast<MacHidDevice*>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.dead_=true;
        self.changed_.notify_all();
    }
    static void on_write(void* context,IOReturn result,void*,IOHIDReportType,uint32_t,uint8_t*,CFIndex) {
        auto& self=*static_cast<MacHidDevice*>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.write_result_=result;self.write_done_=true;
        self.changed_.notify_all();
    }
 public:
    MacHidDevice(Enumeration found,std::size_t index):found_(std::move(found)),device_(found_.devices[index].get()) {
        id_={found_.candidates[index].vendor,found_.candidates[index].product};
        const IOReturn opened=IOHIDDeviceOpen(device_,kIOHIDOptionsTypeSeizeDevice);
        if (opened==kIOReturnExclusiveAccess || opened==kIOReturnBusy || opened==kIOReturnNotPermitted) throw BridgeBusy{};
        if (opened!=kIOReturnSuccess) throw TransportError("Cannot open HID device");
        opened_=true;
        IOHIDDeviceRegisterInputReportCallback(device_,input_buffer_.data(),static_cast<CFIndex>(input_buffer_.size()),&MacHidDevice::on_input,this);
        IOHIDDeviceRegisterRemovalCallback(device_,&MacHidDevice::on_removed,this);
        std::promise<void> ready;
        loop_=std::thread([this,&ready] {
            const CFRunLoopRef loop=CFRunLoopGetCurrent();
            IOHIDDeviceScheduleWithRunLoop(device_,loop,kCFRunLoopDefaultMode);
            ready.set_value();
            while (!stop_) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.05,false);
            IOHIDDeviceUnscheduleFromRunLoop(device_,loop,kCFRunLoopDefaultMode);
        });
        ready.get_future().wait();
        flush();
    }
    ~MacHidDevice() override {
        stop_=true;
        if (loop_.joinable()) loop_.join();
        if (opened_) {
            IOHIDDeviceRegisterInputReportCallback(device_,input_buffer_.data(),static_cast<CFIndex>(input_buffer_.size()),nullptr,nullptr);
            IOHIDDeviceRegisterRemovalCallback(device_,nullptr,nullptr);
            IOHIDDeviceClose(device_,kIOHIDOptionsTypeNone);
        }
    }
    UsbId id() const override {return id_;}
    void write(const Report& report,int timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (dead_) throw TransportError("HID I/O failed");
        out_=report;write_done_=false;write_result_=kIOReturnSuccess;
        // Byte 0 is the report ID (0x3f) and is part of the 64 bytes sent.
        const IOReturn started=IOHIDDeviceSetReportWithCallback(device_,kIOHIDReportTypeOutput,out_[0],out_.data(),
            static_cast<CFIndex>(out_.size()),std::max(timeout_ms,1)/1000.0,&MacHidDevice::on_write,this);
        if (started!=kIOReturnSuccess) throw TransportError("HID I/O failed");
        if (!changed_.wait_for(lock,std::chrono::milliseconds(timeout_ms+50),[this] {return write_done_ || dead_;})) {
            dead_=true; // a late completion must not be taken for a later write's
            throw TransportError("HID I/O timed out");
        }
        if (!write_done_ || write_result_!=kIOReturnSuccess) throw TransportError("HID I/O failed");
    }
    void read(Report& report,int timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock,std::chrono::milliseconds(timeout_ms),[this] {return !queue_.empty() || dead_;});
        if (!queue_.empty()) {
            const Input in=queue_.front();queue_.pop_front();
            if (!in.whole) throw TransportError("Short HID report");
            report=in.report;
            return;
        }
        if (dead_) throw TransportError("HID I/O failed");
        throw TransportError("HID I/O timed out");
    }
    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
};
}
std::unique_ptr<HidDevice> open_bridge(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    auto found=enumerate(ids,unknown_vendors);
    const auto index=select_bridge(found.candidates,ids,unknown_vendors);
    return std::make_unique<MacHidDevice>(std::move(found),index);
}
std::string describe_mac_bridges(const std::vector<UsbId>& ids) {
    try {
        const auto found=enumerate(ids);
        return found.text.empty() ? std::string("no HID device with a listed bridge id\n") : found.text;
    } catch (const std::exception& e) {
        return std::string("cannot enumerate: ")+e.what()+"\n";
    }
}
}
