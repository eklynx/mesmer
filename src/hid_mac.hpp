// macOS-only diagnostics for the IOKit HID device (hid_mac.cpp). Normal code
// uses open_bridge() from hid_device.hpp, the source of truth; this exists so the
// hardware smoke test can show what macOS reports before anything is opened.
#pragma once
#include <string>

namespace brunner {
// One line per HID device with a listed bridge id: ids, usage page/usage and the
// raw report sizes macOS reports, or why nothing could be enumerated (for example
// a missing Input Monitoring permission). Never throws, never opens a device for I/O.
std::string describe_mac_bridges();
}
