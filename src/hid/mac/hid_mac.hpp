// macOS-only diagnostics for the IOKit HID device (hid/mac/hid_mac.cpp). Normal code
// uses open_bridge() from hid/hid_device.hpp, the source of truth; this exists so the
// hardware smoke test can show what macOS reports before anything is opened.
#pragma once
#include "hid/usb_ids.hpp"
#include <string>
#include <vector>

namespace brunner {
// One line per HID device with one of `ids`: ids, usage page/usage and the
// raw report sizes macOS reports, or why nothing could be enumerated (for example
// a missing Input Monitoring permission). Never throws, never opens a device for I/O.
std::string describe_mac_bridges(const std::vector<UsbId>& ids);
}
