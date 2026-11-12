// SOURCE OF TRUTH for the seam between the portable reader (reader.cpp,
// bridge.hpp) and an operating system's HID access (hid_win.cpp, hid_mac.cpp).
// Specification: docs/hid-bridge.md. Read this header, not the platform files,
// to use or implement a HidDevice.
//
// Happy path: open_bridge() finds exactly one yoke bridge, opens it exclusively,
// discards stale input and returns it. write() sends one 64-byte report; read()
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
#include "core.hpp"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace brunner {
struct OperationCancelled : std::runtime_error {
    OperationCancelled() : std::runtime_error("Reader stopped") {}
};
// USB-level failures: the link is gone or the controllers are silent. The
// reader closes the bridge and keeps looking; nothing about the controllers'
// state is concluded from them.
struct TransportError : std::runtime_error {
    explicit TransportError(const char* what) : std::runtime_error(what) {}
};
struct BridgeAbsent : TransportError {
    BridgeAbsent() : TransportError("Expected one original CLS USB Bridge") {}
};
struct BridgeBusy : TransportError {
    BridgeBusy() : TransportError("Yoke busy or unavailable; close CLS2Sim and Simux") {}
};

// What an OS reports about one HID device it enumerated. The platform keeps its
// own handle for each candidate, in the same order as the vector it passes in.
struct HidCandidate {
    std::uint16_t vendor = 0, product = 0;
    std::uint16_t usage_page = 0, usage = 0;
    std::uint16_t input_length = 0, output_length = 0; // bytes, report ID included
};

// Picks the yoke's bridge among enumerated devices and returns its index.
// A device qualifies when its id is in usb_ids.hpp, its top-level usage is
// vendor page 0xff00 / usage 0xff02, and its input and output reports are 64 bytes.
// Throws BridgeAbsent if none qualifies and std::runtime_error ("... found
// several") if more than one does, whatever their ids.
std::size_t select_bridge(const std::vector<HidCandidate>& candidates);

// An open, exclusively held bridge. One outstanding request at a time; not
// thread-safe. Timeouts are in milliseconds.
class HidDevice {
public:
    virtual ~HidDevice() = default;
    // Sends a 64-byte report (byte 0 is report ID 0x3f). TransportError on failure or timeout.
    virtual void write(const Report& report, int timeout_ms) = 0;
    // Receives the next 64-byte report. TransportError on timeout, short report or removal.
    virtual void read(Report& report, int timeout_ms) = 0;
    // Discards input queued before now. TransportError if it cannot.
    virtual void flush() = 0;
};

// Implemented once per platform (hid_win.cpp, hid_mac.cpp).
std::unique_ptr<HidDevice> open_bridge();
}
