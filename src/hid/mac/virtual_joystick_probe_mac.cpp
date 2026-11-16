// Developer tool, not part of the plugin and not in ctest (like the other hardware tools). The SPIKE for a macOS virtual joystick
// (plan: a virtual HID device X-Plane can bind). Creates an IOHIDUserDevice with a 32-button joystick descriptor, then presses
// button 1, 2, 3 ... 32 in turn (one at a time, `hold` ms each), and prints what every step returned. It tells, on this Mac and
// for the way this binary is signed:
//   - whether IOHIDUserDeviceCreateWithProperties gives a device at all (the restricted entitlement
//     com.apple.developer.hid.virtual.device is what Apple documents as required),
//   - whether the device shows up for other processes (look in X-Plane's Settings > Joystick, or System Information > USB/HID, while this runs).
// It writes to nothing but its own virtual device. No yoke is needed.
//
// Build: clang++ -std=c++17 -framework IOKit -framework CoreFoundation virtual_joystick_probe_mac.cpp -o "$CLAUDE_WORK/derived/mesmer/virtual_joystick_probe"
// Run:   "$CLAUDE_WORK/derived/mesmer/virtual_joystick_probe" [seconds] [hold_ms]   (default 30 s, 400 ms)
// To try a signature: codesign --force --sign - [--options runtime] [--entitlements virtual.entitlements] the binary.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/IOReturn.h>
#include <IOKit/IOTypes.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

// IOHIDUserDevice is exported by IOKit but its user-space header is not in the public SDK (only the kernel-side
// hidsystem/IOHIDUserDevice.h is), so the three calls used are declared here, as IOHIDFamily's open-source header has them.
extern "C" {
typedef struct __IOHIDUserDevice* IOHIDUserDeviceRef;
IOHIDUserDeviceRef IOHIDUserDeviceCreateWithProperties(CFAllocatorRef allocator, CFDictionaryRef properties, IOOptionBits options);
IOReturn IOHIDUserDeviceHandleReport(IOHIDUserDeviceRef device, const uint8_t* report, CFIndex reportLength);
}

namespace {
// Generic Desktop / Joystick, one application collection, buttons 1..32 as one 32-bit input report (no report ID, no axes).
const std::uint8_t descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x20,        //   Usage Maximum (32)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0xC0               // End Collection
};
void put(CFMutableDictionaryRef d, CFStringRef key, CFTypeRef value) { CFDictionarySetValue(d, key, value); CFRelease(value); }
CFNumberRef number(int v) { return CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &v); }
}

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 30;
    const int hold_ms = argc > 2 ? std::atoi(argv[2]) : 400;
    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    put(properties, CFSTR(kIOHIDReportDescriptorKey), CFDataCreate(kCFAllocatorDefault, descriptor, sizeof descriptor));
    put(properties, CFSTR(kIOHIDVendorIDKey), number(0x1209));      // pid.codes' test vendor id: not a real product
    put(properties, CFSTR(kIOHIDProductIDKey), number(0x0001));
    put(properties, CFSTR(kIOHIDProductKey), CFSTR("Mesmer Virtual Joystick (spike)"));
    put(properties, CFSTR(kIOHIDSerialNumberKey), CFSTR("mesmer-spike-1"));
    put(properties, CFSTR(kIOHIDTransportKey), CFSTR("Virtual"));
    std::printf("creating the virtual device (pid %d)...\n", getpid());
    IOHIDUserDeviceRef device = IOHIDUserDeviceCreateWithProperties(kCFAllocatorDefault, properties, 0);
    CFRelease(properties);
    if (!device) {
        std::printf("RESULT: IOHIDUserDeviceCreateWithProperties returned NULL (no device). Without the entitlement that is what Apple documents.\n");
        return 2;
    }
    std::printf("RESULT: device created. Look for it in X-Plane's Settings > Joystick; cycling buttons 1..32 for %d s.\n", seconds);
    std::fflush(stdout);
    std::uint32_t word = 0;
    int button = 1;
    for (int elapsed = 0; elapsed < seconds * 1000; elapsed += 2 * hold_ms) {
        word = 1u << (button - 1);
        std::uint8_t report[4] = {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24)};
        const IOReturn down = IOHIDUserDeviceHandleReport(device, report, sizeof report);
        usleep(hold_ms * 1000);
        std::uint8_t up_report[4] = {0, 0, 0, 0};
        const IOReturn up = IOHIDUserDeviceHandleReport(device, up_report, sizeof up_report);
        usleep(hold_ms * 1000);
        std::printf("button %2d: down %s (0x%x), up %s (0x%x)\n", button, down == kIOReturnSuccess ? "ok" : "REFUSED", down, up == kIOReturnSuccess ? "ok" : "REFUSED", up);
        std::fflush(stdout);
        button = button % 32 + 1;
    }
    CFRelease(device);
    std::printf("device released.\n");
    return 0;
}
