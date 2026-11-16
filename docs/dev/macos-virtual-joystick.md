# A virtual joystick on macOS: findings

Windows presses a vJoy device so X-Plane can bind the yoke's buttons ([button commands](button-commands.md)). macOS has no virtual
joystick yet, so a model that asks for the `VJoy` action gets `Direct` there. This page records what was found when looking for one
(2026-11-16, macOS 27.2.1, SIP on, AMFI not relaxed). The seam for one exists: `VirtualJoystick` in
`src/buttons/actions/vjoy_presser.hpp` and `make_virtual_joystick()` in `src/buttons/actions/vjoy_factory.cpp`.

## Apple's GameController framework (`GCVirtualController`) does not work for this

It is available on iOS, iPadOS and Mac Catalyst only, not macOS; it is an on-screen touch overlay inside the app that creates it, read
back through that app's own `GCController`; and its elements are a fixed standard set. Another process, X-Plane, cannot see it.
Apple's own engineers say the system's game-controller support ignores virtual HID devices on purpose.

## What does work: a virtual HID device, behind a restricted entitlement

An `IOHIDUserDevice` (the IOKit C API, which CoreHID's `HIDVirtualDevice`, macOS 15 and later, wraps) with our own 32-button report
descriptor is a device any process sees as a joystick. Creating one needs the entitlement `com.apple.developer.hid.virtual.device`,
which Apple grants on request (Developer account, the DriverKit entitlement request form; no timeline).

Measured with `src/hid/mac/virtual_joystick_probe_mac.cpp` (the three IOKit calls it uses are exported but not declared in the public
SDK, so the tool declares them itself):

| Signature of the process | Result |
|---|---|
| ad-hoc, no entitlement | `IOHIDUserDeviceCreateWithProperties` returns `NULL`: no device |
| ad-hoc, claiming the entitlement | the process is killed (exit 137): the system refuses a restricted entitlement with no Apple profile behind it |

The check is on the **process**. X-Plane (hardened runtime, no such entitlement, and not ours to change) is the process a plugin runs
in, so a virtual device created inside the plugin gets `NULL` the same way. An entitlement is also something a main executable carries,
not a bare `.xpl`. It is not an App Store question: distribution outside the App Store does not lift it.

## Ways forward

- **A helper app** (signed and notarized, carrying the entitlement) creates the device; the plugin sends it the button state over a
  local socket. The plugin's `VirtualJoystick` becomes that client. Needs Apple's approval for the entitlement first, and an installer
  that places the helper.
- **Relaxed security on a developer's own Mac** (SIP and AMFI off): works for testing, not something to ask users to do.
- **No virtual joystick:** `Direct` (the X-Plane commands in `button_commands`) stays the macOS action, as it is today.

Not measured: whether X-Plane 12 lists a virtual HID device in Settings > Joystick (it needs a device to look at, so it waits on one of
the above), and how long Apple takes to grant the entitlement.
