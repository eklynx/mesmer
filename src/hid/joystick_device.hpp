// SOURCE OF TRUTH for the seam between the portable button reader (brunner/button_runner.hpp) and an operating
// system's access to the yoke's joystick HID interface (hid/mac/joystick_mac.cpp). Specification:
// docs/dev/button-commands.md. This is a different interface from the bridge in hid/hid_device.hpp: the
// bridge is the vendor interface Mesmer seizes and talks to; this one is the ordinary joystick the yoke also
// presents (X-Plane reads it too), which Mesmer only observes.
//
// Happy path: open_joystick(vendor) finds the one HID interface of that USB vendor whose top-level usage is
// Generic Desktop / Joystick and opens it NON-exclusively and read-only, so X-Plane keeps receiving the same
// reports. descriptor() is that interface's HID report descriptor (hid/report_decoder.hpp parses it). read()
// returns the next input report's payload (the bytes after the report ID, when the interface uses report IDs).
//
// Error cases:
//   open_joystick returns nullptr (never throws for it) when no such interface is present: a yoke without one,
//   unplugged, or access refused by the OS (for example no Input Monitoring permission); `why` then says which
//   when the OS says. It throws std::runtime_error when more than one candidate matches; Mesmer will not guess.
//   read() returns false on timeout. It throws TransportError when the device is removed or the OS fails.
// Not thread-safe; the one reader thread owns it. Platforms without an implementation return nullptr.
#pragma once
#include "brunner/core.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hid {
class JoystickDevice {
public:
    virtual ~JoystickDevice() = default;
    virtual std::uint16_t vendor() const = 0;
    virtual std::uint16_t product() const = 0;
    virtual const std::vector<std::uint8_t>& descriptor() const = 0;
    virtual bool read(std::vector<std::uint8_t>& payload, int timeout_ms) = 0;
};
std::unique_ptr<JoystickDevice> open_joystick(std::uint16_t vendor, std::string& why);
}
