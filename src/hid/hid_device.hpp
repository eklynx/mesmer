// SOURCE OF TRUTH for the seam between the portable reader (brunner/reader.cpp,
// brunner/bridge.hpp) and an operating system's HID access (hid/win/hid_win.cpp, hid/mac/hid_mac.cpp).
// Specification: docs/hid-bridge.md. Read this header, not the platform files,
// to use or implement a HidDevice.
//
// Happy path: open_bridge(ids) finds exactly one yoke bridge among the given USB
// ids (the ids the yoke presets name), opens it exclusively, discards stale input
// and returns it; the device reports which id it opened, so the model follows from it. write() sends one 64-byte report; read()
// returns the next 64-byte report within the timeout.
//
// Error cases (all thrown as the types below, never returned as codes):
//   BridgeAbsent    no bridge with a listed id and the right descriptor is present
//                   (yoke off or unplugged, or an id this version does not know).
//   BridgeBusy      the bridge exists but cannot be opened exclusively; another
//                   program (CLS2Sim, Simux) holds it, or the OS refused access.
//   std::runtime_error  more than one candidate matches; refuse rather than guess.
//   TransportError  any later I/O failure: timeout, short report, device removed.
//   OperationCancelled  the reader was asked to stop between transfers.
#pragma once
#include "brunner/core.hpp"
#include "hid/usb_ids.hpp"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace brunner {
// OperationCancelled and TransportError (the USB link failing) are in brunner/core.hpp.
struct BridgeAbsent : TransportError {
    BridgeAbsent() : TransportError("Expected one original CLS USB Bridge") {}
};
struct BridgeBusy : TransportError {
    BridgeBusy() : TransportError("Yoke busy or unavailable; close CLS2Sim and Simux") {}
};

// Windows keeps a disabled USB bridge off through unplugging, power cycles and restarts, and the
// HID search cannot tell it from an unplugged one. bridge_disabled_in_windows() is true when a
// Brunner USB device (vendor 0x25BB) is present with problem "disabled"; any failure reads as
// false. turn_bridge_on_in_windows() runs Windows' pnputil with the administrator rights the pilot's
// account holds: empty when started, else why not. On macOS the first is always false and the
// second says it does not apply. Implemented in hid/win/hid_win.cpp; hid/hid_select.cpp has the non-Windows answers.
bool bridge_disabled_in_windows();
std::string turn_bridge_on_in_windows();

// What an OS reports about one HID device it enumerated. The platform keeps its
// own handle for each candidate, in the same order as the vector it passes in.
struct HidCandidate {
    std::uint16_t vendor = 0, product = 0;
    std::uint16_t usage_page = 0, usage = 0;
    std::uint16_t input_length = 0, output_length = 0; // bytes, report ID included
};

// Picks the yoke's bridge among enumerated devices and returns its index.
// A device qualifies when its top-level usage is vendor page 0xff00 / usage 0xff02, its input
// and output reports are 64 bytes, and either its id is in `ids` (a known model) or, only when no
// known one is present, its vendor is in `unknown_vendors` (an unknown model: the caller opens it
// read-only). A known yoke therefore always wins over other devices of the same vendor.
// Throws BridgeAbsent if none qualifies and std::runtime_error ("... found several") if more than
// one qualifies at the tier that decides.
std::size_t select_bridge(const std::vector<HidCandidate>& candidates,const std::vector<UsbId>& ids,
    const std::vector<std::uint16_t>& unknown_vendors={});

// An open, exclusively held bridge. One outstanding request at a time; not
// thread-safe. Timeouts are in milliseconds.
class HidDevice {
public:
    virtual ~HidDevice() = default;
    // The USB id this device was opened as: how the model is told from the device.
    virtual UsbId id() const = 0;
    // Sends a 64-byte report (byte 0 is report ID 0x3f). TransportError on failure or timeout.
    virtual void write(const Report& report, int timeout_ms) = 0;
    // Receives the next 64-byte report. TransportError on timeout, short report or removal.
    virtual void read(Report& report, int timeout_ms) = 0;
    // Discards input queued before now. TransportError if it cannot.
    virtual void flush() = 0;
};

// Implemented once per platform (hid/win/hid_win.cpp, hid/mac/hid_mac.cpp). Only devices with one of
// `ids`, or (when none is present) of a vendor in `unknown_vendors`, are considered; BridgeAbsent if
// there is none. The device's id() says which it was.
std::unique_ptr<HidDevice> open_bridge(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors={});
}
