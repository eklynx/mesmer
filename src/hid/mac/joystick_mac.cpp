// macOS implementation of hid::open_joystick() over IOKit's IOHIDManager. Contract: hid/joystick_device.hpp.
//
// The joystick interface is opened with kIOHIDOptionsTypeNone: shared, never seized, so X-Plane's own reading
// of the same joystick is untouched (confirmed on hardware with hid/mac/joystick_probe_mac.cpp). Only that one
// interface is ever opened: the manager matches on the joystick usage as well as the vendor, so it never touches the
// bridge Mesmer seizes (hid/mac/hid_mac.cpp), and its own shared open is closed again right after the search, as
// hid_mac.cpp does, leaving just the joystick device open. Input reports
// arrive by callback on a private run-loop thread and are queued; read() waits on the queue with a timeout.
#include "hid/joystick_device.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace hid {
namespace {
bool number_property(IOHIDDeviceRef device, CFStringRef key, int& out) {
    const CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    return value && CFGetTypeID(value) == CFNumberGetTypeID() &&
           CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &out);
}

class MacJoystick final : public JoystickDevice {
    CFTypeRef manager_;                 // owned (retained by us)
    IOHIDDeviceRef device_;             // owned (retained by us)
    std::uint16_t vendor_, product_;
    std::vector<std::uint8_t> descriptor_;
    std::vector<std::uint8_t> buffer_ = std::vector<std::uint8_t>(256);
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::vector<std::uint8_t>> queue_;
    bool dead_ = false;
    bool opened_ = false;
    std::atomic<bool> stop_{false};
    std::thread loop_;

    static void on_input(void* context, IOReturn, void*, IOHIDReportType, uint32_t id, uint8_t* data, CFIndex length) {
        auto& self = *static_cast<MacJoystick*>(context);
        // With report IDs the first byte is the ID; the payload is what follows it.
        const bool prefixed = id != 0 && length > 0 && data[0] == id;
        std::vector<std::uint8_t> payload(data + (prefixed ? 1 : 0), data + length);
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.queue_.push_back(std::move(payload));
        if (self.queue_.size() > 256) self.queue_.pop_front();
        self.changed_.notify_all();
    }
    static void on_removed(void* context, IOReturn, void*) {
        auto& self = *static_cast<MacJoystick*>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.dead_ = true;
        self.changed_.notify_all();
    }

public:
    MacJoystick(CFTypeRef manager, IOHIDDeviceRef device, std::uint16_t vendor, std::uint16_t product)
        : manager_(manager), device_(device), vendor_(vendor), product_(product) {
        CFRetain(manager_);
        CFRetain(device_);
        if (auto ref = IOHIDDeviceGetProperty(device_, CFSTR(kIOHIDReportDescriptorKey)); ref && CFGetTypeID(ref) == CFDataGetTypeID()) {
            const auto data = static_cast<CFDataRef>(ref);
            descriptor_.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
        }
        const IOReturn opened = IOHIDDeviceOpen(device_, kIOHIDOptionsTypeNone);
        if (opened != kIOReturnSuccess) {
            CFRelease(device_);
            CFRelease(manager_);
            throw brunner::TransportError("Cannot open the joystick interface");
        }
        opened_ = true;
        IOHIDDeviceRegisterInputReportCallback(device_, buffer_.data(), static_cast<CFIndex>(buffer_.size()), &MacJoystick::on_input, this);
        IOHIDDeviceRegisterRemovalCallback(device_, &MacJoystick::on_removed, this);
        std::promise<void> ready;
        loop_ = std::thread([this, &ready] {
            const CFRunLoopRef loop = CFRunLoopGetCurrent();
            IOHIDDeviceScheduleWithRunLoop(device_, loop, kCFRunLoopDefaultMode);
            ready.set_value();
            while (!stop_) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
            IOHIDDeviceUnscheduleFromRunLoop(device_, loop, kCFRunLoopDefaultMode);
        });
        ready.get_future().wait();
    }
    ~MacJoystick() override {
        stop_ = true;
        if (loop_.joinable()) loop_.join();
        if (opened_) {
            IOHIDDeviceRegisterInputReportCallback(device_, buffer_.data(), static_cast<CFIndex>(buffer_.size()), nullptr, nullptr);
            IOHIDDeviceRegisterRemovalCallback(device_, nullptr, nullptr);
            IOHIDDeviceClose(device_, kIOHIDOptionsTypeNone);
        }
        CFRelease(device_);
        CFRelease(manager_);
    }
    std::uint16_t vendor() const override { return vendor_; }
    std::uint16_t product() const override { return product_; }
    const std::vector<std::uint8_t>& descriptor() const override { return descriptor_; }
    bool read(std::vector<std::uint8_t>& payload, int timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty() || dead_; });
        if (!queue_.empty()) {
            payload = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }
        if (dead_) throw brunner::TransportError("Joystick interface removed");
        return false;
    }
};
}

std::unique_ptr<JoystickDevice> open_joystick(std::uint16_t vendor, std::string& why) {
    why.clear();
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) { why = "cannot create a HID manager"; return nullptr; }
    struct Release { IOHIDManagerRef m; ~Release() { CFRelease(m); } } release{manager};
    const int vendor_id = vendor;
    CFNumberRef vendor_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor_id);
    const int page_id = 0x01, usage_id = 0x04;   // Generic Desktop / Joystick
    CFNumberRef page_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page_id);
    CFNumberRef usage_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage_id);
    const void* keys[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDPrimaryUsagePageKey), CFSTR(kIOHIDPrimaryUsageKey)};
    const void* values[] = {vendor_number, page_number, usage_number};
    CFDictionaryRef match = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);
    CFRelease(vendor_number);
    CFRelease(page_number);
    CFRelease(usage_number);
    const IOReturn opened = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (opened != kIOReturnSuccess) {
        why = opened == kIOReturnNotPermitted ? "macOS refused access (Input Monitoring permission?)" : "cannot enumerate HID devices";
        return nullptr;
    }
    CFSetRef set = IOHIDManagerCopyDevices(manager);
    std::vector<IOHIDDeviceRef> found;
    if (set) {
        std::vector<const void*> raw(static_cast<std::size_t>(CFSetGetCount(set)));
        CFSetGetValues(set, raw.data());
        for (const void* item : raw) {
            auto* device = static_cast<IOHIDDeviceRef>(const_cast<void*>(item));
            int page = 0, usage = 0;
            number_property(device, CFSTR(kIOHIDPrimaryUsagePageKey), page);
            number_property(device, CFSTR(kIOHIDPrimaryUsageKey), usage);
            if (page == 0x01 && usage == 0x04) found.push_back(device);   // Generic Desktop / Joystick
        }
    }
    struct ReleaseSet { CFSetRef s; ~ReleaseSet() { if (s) CFRelease(s); } } release_set{set};
    if (found.empty()) { why = "no joystick interface"; return nullptr; }
    if (found.size() > 1) throw std::runtime_error("Found several joystick interfaces from the yoke's vendor");
    int product = 0;
    number_property(found.front(), CFSTR(kIOHIDProductIDKey), product);
    // Release the manager's own (shared) open before the device is opened, as hid_mac.cpp does; the manager and the
    // device stay alive (the device is retained below) but only the device's own open remains.
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    return std::make_unique<MacJoystick>(manager, found.front(), vendor, static_cast<std::uint16_t>(product));
}
}
