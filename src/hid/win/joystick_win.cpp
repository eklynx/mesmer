// Windows implementation of hid::open_joystick(). Contract: hid/joystick_device.hpp.
//
// Only what is Windows' is here: finding the interface (hid/win/hid_common_win.hpp's walk, the same one the bridge
// uses), opening it shared and read-only, asking Windows where each usage's bits sit, and the overlapped reads.
// Everything else is shared with macOS: hid/report_decoder.hpp builds the layout (layout_from_fields) and decodes
// the reports, brunner/button_runner.hpp turns them into presses.
//
// The interface is opened GENERIC_READ with FILE_SHARE_READ | FILE_SHARE_WRITE: every program that opens a HID
// joystick gets its own copy of each input report from Windows, so X-Plane keeps reading the same reports. Only
// the joystick interface (Generic Desktop / Joystick) is ever opened; the bridge, which hid_win.cpp seizes, is a
// different interface and is never touched here.
//
// Windows hands no program the raw report descriptor. Instead, for each input usage its parse knows, the usage is
// written all ones into an empty report with HidP_SetUsages / HidP_SetUsageValue(Array), and the bits that changed
// are that usage's (layout_from_fields' UsageBits). Windows puts a report-ID byte first in every report, 0 when the
// device uses none, so the payload is everything after it.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#include "hid/joystick_device.hpp"
#include "hid/win/hid_common_win.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace hid {
namespace {
constexpr USAGE page_generic_desktop=0x01,usage_joystick=0x04;

struct Preparsed {
    PHIDP_PREPARSED_DATA value=nullptr;
    explicit Preparsed(HANDLE h) { if (!HidD_GetPreparsedData(h,&value)) value=nullptr; }
    ~Preparsed() { if (value) HidD_FreePreparsedData(value); }
    Preparsed(const Preparsed&)=delete;
    Preparsed& operator=(const Preparsed&)=delete;
};
bool joystick_of(HANDLE h,std::uint16_t vendor,std::uint16_t& product) {
    HIDD_ATTRIBUTES attributes{}; attributes.Size=sizeof(attributes);
    if (!HidD_GetAttributes(h,&attributes) || attributes.VendorID!=vendor) return false;
    Preparsed data(h);
    HIDP_CAPS caps{};
    if (!data.value || HidP_GetCaps(data.value,&caps)!=HIDP_STATUS_SUCCESS) return false;
    product=attributes.ProductID;
    return caps.UsagePage==page_generic_desktop && caps.Usage==usage_joystick;
}

// The bits each input usage occupies, from Windows' parse (see the top of the file).
std::optional<ReportLayout> layout_from_windows(PHIDP_PREPARSED_DATA data) {
    HIDP_CAPS caps{};
    if (HidP_GetCaps(data,&caps)!=HIDP_STATUS_SUCCESS || caps.InputReportByteLength<2) return std::nullopt;
    const ULONG length=caps.InputReportByteLength;
    const int report_bits=static_cast<int>(length-1)*8;
    std::vector<char> report(length);
    bool has_report_id=false;
    const auto empty=[&](UCHAR id) {std::fill(report.begin(),report.end(),char{0});report[0]=static_cast<char>(id);if (id) has_report_id=true;};
    const auto changed=[&] {
        std::vector<int> bits;
        for (ULONG i=1;i<length;++i)
            for (int b=0;b<8;++b) if (static_cast<unsigned char>(report[i])&(1u<<b)) bits.push_back(static_cast<int>(i-1)*8+b);
        return bits;
    };
    std::vector<UsageBits> usages;

    USHORT button_count=caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> buttons(button_count);
    if (button_count && HidP_GetButtonCaps(HidP_Input,buttons.data(),&button_count,data)!=HIDP_STATUS_SUCCESS) button_count=0;
    for (USHORT c=0;c<button_count;++c) {
        const auto& cap=buttons[c];
        const USAGE first=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage,last=cap.IsRange?cap.Range.UsageMax:first;
        for (unsigned u=first;u<=last && u<=0xffff;++u) {
            empty(cap.ReportID);
            USAGE usage=static_cast<USAGE>(u);
            ULONG count=1;
            if (HidP_SetUsages(HidP_Input,cap.UsagePage,cap.LinkCollection,&usage,&count,data,report.data(),length)!=HIDP_STATUS_SUCCESS) continue;
            usages.push_back({cap.UsagePage,static_cast<int>(u),changed(),0,1});   // a button is one bit, 0 or 1
        }
    }

    USHORT value_count=caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> values(value_count);
    if (value_count && HidP_GetValueCaps(HidP_Input,values.data(),&value_count,data)!=HIDP_STATUS_SUCCESS) value_count=0;
    for (USHORT c=0;c<value_count;++c) {
        const auto& cap=values[c];
        const USAGE first=cap.IsRange?cap.Range.UsageMin:cap.NotRange.Usage,last=cap.IsRange?cap.Range.UsageMax:first;
        for (unsigned u=first;u<=last && u<=0xffff;++u) {
            empty(cap.ReportID);
            NTSTATUS status;
            if (!cap.IsRange && cap.ReportCount>1) {
                std::vector<char> ones((static_cast<std::size_t>(cap.BitSize)*cap.ReportCount+7)/8,static_cast<char>(0xff));
                status=HidP_SetUsageValueArray(HidP_Input,cap.UsagePage,cap.LinkCollection,static_cast<USAGE>(u),ones.data(),
                    static_cast<USHORT>(ones.size()),data,report.data(),length);
            } else {
                const ULONG ones=cap.BitSize>=32?0xffffffffu:(1u<<cap.BitSize)-1u;
                status=HidP_SetUsageValue(HidP_Input,cap.UsagePage,cap.LinkCollection,static_cast<USAGE>(u),ones,data,report.data(),length);
            }
            if (status!=HIDP_STATUS_SUCCESS) continue;
            usages.push_back({cap.UsagePage,static_cast<int>(u),changed(),cap.LogicalMin,cap.LogicalMax});
        }
    }
    return layout_from_fields(usages,report_bits,has_report_id);
}

class WinJoystick final : public JoystickDevice {
    brunner::win::Handle handle_;
    DWORD open_error_;                       // Windows' error from opening handle_, before anything else can change it
    brunner::win::Handle event_;
    std::uint16_t vendor_,product_;
    std::optional<ReportLayout> layout_;
    std::vector<std::uint8_t> descriptor_;   // Windows gives none (see the top of the file)
    std::vector<std::uint8_t> buffer_;
    OVERLAPPED overlap_{};
    bool pending_=false;
    // A read is kept pending across timeouts, so no report is lost between two calls.
    void issue() {
        ResetEvent(event_.value);
        overlap_=OVERLAPPED{};overlap_.hEvent=event_.value;
        if (ReadFile(handle_.value,buffer_.data(),static_cast<DWORD>(buffer_.size()),nullptr,&overlap_) || GetLastError()==ERROR_IO_PENDING) {
            pending_=true;
            return;
        }
        throw brunner::TransportError(("Joystick interface removed or failed (Windows error "+std::to_string(GetLastError())+")").c_str());
    }
public:
    WinJoystick(const std::wstring& path,std::uint16_t vendor,std::uint16_t product)
        :handle_(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)),open_error_(GetLastError()),
         event_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),vendor_(vendor),product_(product) {
        if (!handle_.valid()) throw std::runtime_error("Windows would not open the joystick interface (error "+std::to_string(open_error_)+")");
        if (!event_.valid()) throw brunner::TransportError("Cannot create the joystick's completion event");
        Preparsed data(handle_.value);
        HIDP_CAPS caps{};
        if (!data.value || HidP_GetCaps(data.value,&caps)!=HIDP_STATUS_SUCCESS) throw brunner::TransportError("Cannot read the joystick interface's capabilities");
        layout_=layout_from_windows(data.value);
        buffer_.resize(caps.InputReportByteLength);
        HidD_SetNumInputBuffers(handle_.value,64);   // room for a burst while X-Plane's frame runs; failure keeps Windows' default
    }
    ~WinJoystick() override {
        if (pending_) {
            DWORD bytes=0;
            CancelIoEx(handle_.value,&overlap_);
            GetOverlappedResult(handle_.value,&overlap_,&bytes,TRUE);   // drain before the buffer goes
        }
    }
    std::uint16_t vendor() const override {return vendor_;}
    std::uint16_t product() const override {return product_;}
    const std::vector<std::uint8_t>& descriptor() const override {return descriptor_;}
    std::optional<ReportLayout> layout() const override {return layout_;}
    bool read(std::vector<std::uint8_t>& payload,int timeout_ms) override {
        if (!pending_) issue();
        if (WaitForSingleObject(event_.value,static_cast<DWORD>(timeout_ms))!=WAIT_OBJECT_0) return false;
        DWORD bytes=0;
        pending_=false;
        if (!GetOverlappedResult(handle_.value,&overlap_,&bytes,FALSE))
            throw brunner::TransportError(("Joystick interface removed or failed (Windows error "+std::to_string(GetLastError())+")").c_str());
        // The first byte is the report ID (0 when the device uses none); the payload is what follows it.
        payload.assign(buffer_.begin()+(bytes>0?1:0),buffer_.begin()+bytes);
        return true;
    }
};
}

std::unique_ptr<JoystickDevice> open_joystick(std::uint16_t vendor,std::string& why) {
    why.clear();
    std::vector<std::wstring> paths;
    std::uint16_t product=0;
    brunner::win::for_each_hid_interface([&](const std::wstring& path,HANDLE h) {
        std::uint16_t found=0;
        if (joystick_of(h,vendor,found)) {paths.push_back(path);product=found;}
    });
    if (paths.empty()) {why="no joystick interface";return nullptr;}
    if (paths.size()>1) throw std::runtime_error("Found several joystick interfaces from the yoke's vendor");
    try {
        return std::make_unique<WinJoystick>(paths.front(),vendor,product);
    } catch (const brunner::TransportError&) {
        throw;
    } catch (const std::exception& e) {
        why=e.what();
        return nullptr;
    }
}
}
