// Linux-only diagnostics for the hidraw device (hid/lin/hid_lin.cpp). Normal code
// uses open_bridge() from hid/hid_device.hpp, the source of truth; this exists so the
// hardware smoke test can show what Linux reports before anything is opened.
#pragma once
#include "hid/usb_ids.hpp"
#include <string>
#include <vector>

namespace brunner {
// One line per /dev/hidraw* device with one of `ids`: ids, usage page/usage and the report
// sizes read from its report descriptor, or why nothing could be enumerated. Never throws,
// never opens a device for I/O beyond reading its descriptor.
std::string describe_linux_bridges(const std::vector<UsbId>& ids);
}
