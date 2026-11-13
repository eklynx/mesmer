// Windows implementation of open_bridge() / HidDevice. Contract: hid/hid_device.hpp.
// The enumeration, exclusive open and overlapped transfers are moved unchanged
// from reader_win.cpp; the only difference is that matching is decided by
// select_bridge over the ids in hid/usb_ids.hpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
extern "C" {
#include <hidsdi.h>
}
#include "hid/hid_device.hpp"
#include "hid/usb_ids.hpp"
#include <string>
#include <vector>

namespace brunner {
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h) {}
    ~Handle() { if (value!=INVALID_HANDLE_VALUE && value!=nullptr) CloseHandle(value); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
struct DeviceList {
    HDEVINFO value;
    ~DeviceList() { SetupDiDestroyDeviceInfoList(value); }
};
// Fills `out` from an open handle; false when it is neither a listed id nor of a listed unknown-model
// vendor, or cannot be read.
bool describe(HANDLE h,HidCandidate& out,const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    HIDD_ATTRIBUTES attributes{}; attributes.Size=sizeof(attributes);
    if (!HidD_GetAttributes(h,&attributes)
        || !(id_listed(ids,attributes.VendorID,attributes.ProductID) || vendor_listed(unknown_vendors,attributes.VendorID))) return false;
    PHIDP_PREPARSED_DATA data=nullptr;
    if (!HidD_GetPreparsedData(h,&data)) return false;
    HIDP_CAPS caps{};
    const auto result=HidP_GetCaps(data,&caps);
    HidD_FreePreparsedData(data);
    if (result!=HIDP_STATUS_SUCCESS) return false;
    out.vendor=attributes.VendorID;out.product=attributes.ProductID;
    out.usage_page=caps.UsagePage;out.usage=caps.Usage;
    out.input_length=caps.InputReportByteLength;out.output_length=caps.OutputReportByteLength;
    return true;
}
std::wstring find_bridge(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    GUID guid; HidD_GetHidGuid(&guid);
    DeviceList list{SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)};
    if (list.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate HID devices");
    std::vector<HidCandidate> candidates;
    std::vector<std::wstring> paths;
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
        HidCandidate described;
        if (candidate.value!=INVALID_HANDLE_VALUE && describe(candidate.value,described,ids,unknown_vendors)) {
            candidates.push_back(described);
            paths.emplace_back(detail->DevicePath);
        }
    }
    return paths[select_bridge(candidates,ids,unknown_vendors)];
}
class WinHidDevice final : public HidDevice {
    Handle handle_;
    UsbId id_{};
    void transfer(Report& report, bool write, DWORD wait_ms) {
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        if (!event.value) throw TransportError("Cannot create HID completion event");
        OVERLAPPED overlap{}; overlap.hEvent=event.value;
        DWORD bytes=0;
        const auto ok=write ? WriteFile(handle_.value,report.data(),64,&bytes,&overlap)
                            : ReadFile(handle_.value,report.data(),64,&bytes,&overlap);
        if (!ok) {
            if (GetLastError()!=ERROR_IO_PENDING) throw TransportError("HID I/O failed");
            if (WaitForSingleObject(event.value,wait_ms)!=WAIT_OBJECT_0) {
                CancelIoEx(handle_.value,&overlap);
                // Drain cancellation before the OVERLAPPED/buffer leave scope.
                GetOverlappedResult(handle_.value,&overlap,&bytes,TRUE);
                throw TransportError("HID I/O timed out");
            }
            if (!GetOverlappedResult(handle_.value,&overlap,&bytes,FALSE)) throw TransportError("HID completion failed");
        }
        if (bytes!=64) throw TransportError("Short HID report");
    }
 public:
    WinHidDevice(const std::wstring& path,const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors)
        :handle_(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)) {
        if (handle_.value==INVALID_HANDLE_VALUE) throw BridgeBusy{};
        HidCandidate described;
        if (!describe(handle_.value,described,ids,unknown_vendors)) throw TransportError("Unexpected bridge descriptor");
        id_={described.vendor,described.product};
        try {select_bridge({described},ids,unknown_vendors);} // the opened device must still qualify
        catch (const std::exception&) {throw TransportError("Unexpected bridge descriptor");}
        flush();
    }
    UsbId id() const override {return id_;}
    void write(const Report& report,int timeout_ms) override {Report copy=report;transfer(copy,true,static_cast<DWORD>(timeout_ms));}
    void read(Report& report,int timeout_ms) override {transfer(report,false,static_cast<DWORD>(timeout_ms));}
    void flush() override {if (!HidD_FlushQueue(handle_.value)) throw TransportError("Cannot clear old HID input");}
};
}
std::unique_ptr<HidDevice> open_bridge(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    return std::make_unique<WinHidDevice>(find_bridge(ids,unknown_vendors),ids,unknown_vendors);
}
// The bridge's USB device when it is plugged in but switched off in Windows
// (Device Manager's Disable device, or Disable-PnpDevice): still there, with
// problem CM_PROB_DISABLED, and its HID collection gone. Empty when it is not;
// any failure reads as not. Its id carries the bridge's serial number: never logged.
static std::wstring disabled_bridge() {
    wchar_t usb[]=L"USB";
    ULONG size=0;
    if (CM_Get_Device_ID_List_SizeW(&size,usb,CM_GETIDLIST_FILTER_ENUMERATOR|CM_GETIDLIST_FILTER_PRESENT)!=CR_SUCCESS || size==0) return {};
    std::vector<wchar_t> ids(size);
    if (CM_Get_Device_ID_ListW(usb,ids.data(),size,CM_GETIDLIST_FILTER_ENUMERATOR|CM_GETIDLIST_FILTER_PRESENT)!=CR_SUCCESS) return {};
    static constexpr wchar_t prefix[]=L"USB\\VID_25BB&PID_"; // any Brunner device: the product id is gone with the HID collection
    for (wchar_t* id=ids.data();*id;id+=wcslen(id)+1) {
        if (_wcsnicmp(id,prefix,wcslen(prefix))!=0) continue;
        DEVINST node=0;
        ULONG status=0,problem=0;
        if (CM_Locate_DevNodeW(&node,id,CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS
            && CM_Get_DevNode_Status(&status,&problem,node,0)==CR_SUCCESS
            && (status&DN_HAS_PROBLEM) && problem==CM_PROB_DISABLED) return id;
    }
    return {};
}
bool bridge_disabled_in_windows() {return !disabled_bridge().empty();}
// Windows' own pnputil from System32, elevated with the "runas" verb. pnputil is launched, not
// waited for: the reader finds the bridge within a second of it coming back.
std::string turn_bridge_on_in_windows() {
    const auto id=disabled_bridge();
    if (id.empty()) return "The yoke's USB is not switched off in Windows";
    wchar_t system[MAX_PATH];
    const auto length=GetSystemDirectoryW(system,MAX_PATH);
    if (length==0 || length>=MAX_PATH) return "Cannot find Windows' system folder";
    const std::wstring tool=std::wstring(system)+L"\\pnputil.exe";
    const std::wstring parameters=L"/enable-device \""+id+L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    info.lpVerb=L"runas";info.lpFile=tool.c_str();info.lpParameters=parameters.c_str();info.nShow=SW_HIDE;
    if (ShellExecuteExW(&info)) return {};
    return GetLastError()==ERROR_CANCELLED?"Turning the yoke's USB on was declined in Windows' prompt"
        :"Windows could not start pnputil to turn the yoke's USB on";
}
}
