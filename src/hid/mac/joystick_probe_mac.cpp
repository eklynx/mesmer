// Developer tool, not part of the plugin and not in ctest (like the other hardware tools).
// Lists every Brunner (vendor 0x25BB) HID interface, prints each one's report descriptor,
// then opens the joystick interface (usage page 0x01 / usage 0x04) NON-exclusively and
// prints every input report as hex, with the change from the previous one, until Ctrl-C.
// Observe only: it never writes to a device and never seizes one, so X-Plane keeps working.
// Output feeds docs/dev/button-commands.md.
//
// Build: clang++ -std=c++17 -framework IOKit -framework CoreFoundation joystick_probe_mac.cpp -o "$CLAUDE_WORK/derived/mesmer/joystick_probe"
// Run:   "$CLAUDE_WORK/derived/mesmer/joystick_probe" [seconds]   (default 120)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int number(IOHIDDeviceRef device, CFStringRef key) {
    int value = 0;
    if (auto ref = IOHIDDeviceGetProperty(device, key))
        if (CFGetTypeID(ref) == CFNumberGetTypeID()) CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberIntType, &value);
    return value;
}

static void dump_descriptor(IOHIDDeviceRef device) {
    auto ref = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
    if (!ref || CFGetTypeID(ref) != CFDataGetTypeID()) {
        std::printf("  (no report descriptor property)\n");
        return;
    }
    auto data = static_cast<CFDataRef>(ref);
    const UInt8* bytes = CFDataGetBytePtr(data);
    const CFIndex size = CFDataGetLength(data);
    std::printf("  descriptor (%ld bytes):", static_cast<long>(size));
    for (CFIndex i = 0; i < size; ++i) std::printf("%s%02x", i % 16 == 0 ? "\n    " : " ", bytes[i]);
    std::printf("\n");
}

static std::vector<uint8_t> previous;

static void on_input(void*, IOReturn, void*, IOHIDReportType, uint32_t id, uint8_t* report, CFIndex length) {
    std::printf("id=%02x len=%2ld:", id, static_cast<long>(length));
    for (CFIndex i = 0; i < length; ++i) {
        const bool changed = i < static_cast<CFIndex>(previous.size()) && previous[i] != report[i];
        std::printf(changed ? " [%02x]" : "  %02x ", report[i]);
    }
    std::printf("\n");
    std::fflush(stdout);
    previous.assign(report, report + length);
}

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 120;
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int vendor_id = 0x25bb;
    CFNumberRef vendor = CFNumberCreate(nullptr, kCFNumberIntType, &vendor_id);
    CFStringRef keys[] = {CFSTR(kIOHIDVendorIDKey)};
    CFTypeRef values[] = {vendor};
    CFDictionaryRef match = CFDictionaryCreate(nullptr, reinterpret_cast<const void**>(keys), values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, match);
    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        std::printf("IOHIDManagerOpen failed (Input Monitoring permission?)\n");
        return 1;
    }
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        std::printf("No Brunner HID device found.\n");
        return 1;
    }
    std::vector<const void*> raw(CFSetGetCount(devices));
    CFSetGetValues(devices, raw.data());
    std::vector<IOHIDDeviceRef> list;
    for (auto item : raw) list.push_back(const_cast<IOHIDDeviceRef>(static_cast<IOHIDDeviceRef>(const_cast<void*>(item))));

    IOHIDDeviceRef joystick = nullptr;
    std::vector<uint8_t> buffer(256);
    for (auto device : list) {
        const int page = number(device, CFSTR(kIOHIDPrimaryUsagePageKey)), usage = number(device, CFSTR(kIOHIDPrimaryUsageKey));
        std::printf("device vendor=0x%04X product=0x%04X usage_page=0x%04x usage=0x%04x input=%d output=%d feature=%d\n",
            number(device, CFSTR(kIOHIDVendorIDKey)), number(device, CFSTR(kIOHIDProductIDKey)), page, usage,
            number(device, CFSTR(kIOHIDMaxInputReportSizeKey)), number(device, CFSTR(kIOHIDMaxOutputReportSizeKey)),
            number(device, CFSTR(kIOHIDMaxFeatureReportSizeKey)));
        dump_descriptor(device);
        if (page == 0x01 && usage == 0x04 && !joystick) joystick = device;
    }
    if (!joystick) {
        std::printf("No joystick interface (usage page 0x01 / usage 0x04) among them.\n");
        return 1;
    }
    if (IOHIDDeviceOpen(joystick, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        std::printf("Could not open the joystick interface non-exclusively.\n");
        return 1;
    }
    std::printf("\nListening %d s on the joystick interface (non-exclusive). Changed bytes are in [brackets].\n", seconds);
    IOHIDDeviceRegisterInputReportCallback(joystick, buffer.data(), static_cast<CFIndex>(buffer.size()), on_input, nullptr);
    IOHIDDeviceScheduleWithRunLoop(joystick, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
    IOHIDDeviceClose(joystick, kIOHIDOptionsTypeNone);
    return 0;
}
