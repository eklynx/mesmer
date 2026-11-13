#pragma once
#include "hid/win/buttons.hpp"
#ifndef NOMINMAX
#define NOMINMAX // buttons.hpp and its includes call std::min
#endif
#include <windows.h>
#include <string>
#include <type_traits>

namespace brunner {
// vJoy's feeder on its device 1 (decision 9): Brunner's signed vJoy 2.2.2.0,
// set to 32 buttons and no axes by ansible/files/software/vjoy.ps1. Its DLL
// is loaded when first needed, by full path, from the folder vJoy's
// installer records for 64-bit feeders, so the plugin loads and flies
// without vJoy. Only X-Plane's thread calls it. unload() is the plugin's at
// XPluginStop: nothing is freed from a destructor, which would run under the
// loader's lock.
class VJoy final : public VirtualJoystick {
public:
    std::string load() override {
        if (module_) return {};
        const auto folder=installed_folder();
        if (folder.empty()) return "vJoy is not installed (no 64-bit folder in its uninstall entry)";
        const auto path=folder+L"\\vJoyInterface.dll";
        const HMODULE library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) return "vJoy's vJoyInterface.dll did not load (Windows error "+std::to_string(GetLastError())+")";
        const char* missing=nullptr;
        const auto find=[&](auto& function,const char* name) {
            function=reinterpret_cast<std::remove_reference_t<decltype(function)>>(GetProcAddress(library,name));
            if (!function && !missing) missing=name;
        };
        find(enabled_,"vJoyEnabled");find(status_,"GetVJDStatus");find(acquire_,"AcquireVJD");
        find(relinquish_,"RelinquishVJD");find(count_,"GetVJDButtonNumber");find(set_,"SetBtn");find(reset_,"ResetButtons");
        if (missing) {
            FreeLibrary(library);clear();
            return std::string("vJoy's vJoyInterface.dll has no ")+missing;
        }
        module_=library;return {};
    }
    void unload() {if (module_) FreeLibrary(module_);module_=nullptr;clear();}
    bool enabled() override {return enabled_ && enabled_()!=FALSE;}
    Status status() override {
        if (!status_) return Status::unknown;
        switch (status_(device)) {
        case 0:return Status::owned;
        case 1:return Status::available;
        case 2:return Status::busy;
        case 3:return Status::missing;
        default:return Status::unknown;
        }
    }
    bool acquire() override {return acquire_ && acquire_(device)!=FALSE;}
    void relinquish() override {if (relinquish_) relinquish_(device);}
    int buttons() override {return count_?count_(device):0;}
    bool set(unsigned button,bool down) override {
        return set_ && button>=1 && button<=32 && set_(down?TRUE:FALSE,device,static_cast<UCHAR>(button))!=FALSE;
    }
    bool reset() override {return reset_ && reset_(device)!=FALSE;}
private:
    static constexpr UINT device=1;
    HMODULE module_=nullptr;
    // vjoyinterface.h at v2.2.2.0: all cdecl; VjdStat is an int-sized enum.
    BOOL (__cdecl* enabled_)()=nullptr;
    int (__cdecl* status_)(UINT)=nullptr;
    BOOL (__cdecl* acquire_)(UINT)=nullptr;
    void (__cdecl* relinquish_)(UINT)=nullptr;
    int (__cdecl* count_)(UINT)=nullptr;
    BOOL (__cdecl* set_)(BOOL,UINT,UCHAR)=nullptr;
    BOOL (__cdecl* reset_)(UINT)=nullptr;
    void clear() {enabled_=nullptr;status_=nullptr;acquire_=nullptr;relinquish_=nullptr;count_=nullptr;set_=nullptr;reset_=nullptr;}
    // Where vJoy's installer says its 64-bit feeder DLL is: its uninstall
    // entry's DllX64Location (C:\Program Files\vJoy\x64 on the PC,
    // 2026-11-09), so the folder has one source, the installer.
    static std::wstring installed_folder() {
        wchar_t buffer[MAX_PATH]{};
        DWORD size=sizeof(buffer);
        if (RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{8E31F76F-74C3-47F1-9550-E041EEDC5FBB}_is1",
                L"DllX64Location",RRF_RT_REG_SZ,nullptr,buffer,&size)!=ERROR_SUCCESS) return {};
        std::wstring folder(buffer);
        while (!folder.empty() && (folder.back()==L'\\' || folder.back()==L'/')) folder.pop_back();
        return folder;
    }
};
} // namespace brunner
