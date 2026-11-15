// What the Windows bridge (hid/win/hid_win.cpp) and the Windows joystick reader (hid/win/joystick_win.cpp) share:
// an owned handle, and the walk over every present HID interface. Windows only; include after <windows.h>,
// <setupapi.h> and <hidsdi.h>.
//
// for_each_hid_interface(visit) calls visit(path, handle) once per present HID interface, with the handle opened
// for attributes and capabilities only (access 0, shared both ways, so it never disturbs another program's open).
// An interface that cannot be opened even that way is skipped. It throws std::runtime_error when Windows cannot
// enumerate HID devices at all.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace brunner::win {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h) {}
    ~Handle() { if (value!=INVALID_HANDLE_VALUE && value!=nullptr) CloseHandle(value); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    bool valid() const {return value!=INVALID_HANDLE_VALUE && value!=nullptr;}
};
struct DeviceList {
    HDEVINFO value;
    ~DeviceList() { SetupDiDestroyDeviceInfoList(value); }
};
template<class Visit> void for_each_hid_interface(Visit&& visit) {
    GUID guid; HidD_GetHidGuid(&guid);
    DeviceList list{SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)};
    if (list.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate HID devices");
    for (DWORD i=0;;++i) {
        SP_DEVICE_INTERFACE_DATA entry{}; entry.cbSize=sizeof(entry);
        if (!SetupDiEnumDeviceInterfaces(list.value,nullptr,&guid,i,&entry)) {
            if (GetLastError()!=ERROR_NO_MORE_ITEMS) throw std::runtime_error("HID enumeration failed");
            break;
        }
        DWORD bytes=0;
        SetupDiGetDeviceInterfaceDetailW(list.value,&entry,nullptr,0,&bytes,nullptr);
        if (!bytes) continue;
        std::vector<std::uint8_t> buffer(bytes);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(list.value,&entry,detail,bytes,nullptr,nullptr)) continue;
        Handle candidate(CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        if (candidate.valid()) visit(std::wstring(detail->DevicePath),candidate.value);
    }
}
}
