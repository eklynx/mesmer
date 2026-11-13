// Explicit operator-run hardware integration test, never part of CTest.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX // reader.hpp's headers call std::min and std::max
#include <windows.h>
#include <tlhelp32.h>
#include "reader.hpp"
#include <iostream>
#include <cstring>
int main(int argc,char** argv) {
    const bool recover=argc==2 && std::strcmp(argv[1],"--recover-surveyed-loading")==0;
    if (argc!=1 && !recover) {std::cerr<<"Unknown diagnostic option\n";return 2;}
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (processes==INVALID_HANDLE_VALUE) return 2;
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);
    bool clear=true;
    if (!Process32FirstW(processes,&process)) clear=false;
    else do {
        const std::wstring name=process.szExeFile;
        if (name.rfind(L"X-Plane",0)==0 || name.rfind(L"CLS2Sim",0)==0 || name.rfind(L"Simux",0)==0) clear=false;
    } while(Process32NextW(processes,&process));
    CloseHandle(processes);
    if (!clear) {std::cerr<<"Close X-Plane, CLS2Sim and Simux before the standalone test\n";return 2;}
    brunner::Reader reader;reader.start();
    std::uint64_t last=0;
    for (int i=0;i<15;++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto s=reader.snapshot();
        if (!s.connected || s.cycles<=last) {std::cerr<<s.message<<'\n';return 1;}
        last=s.cycles;
        if (recover && i==0) {std::cout<<"Recovering surveyed loading with motors disabled\n";reader.request_loading_recovery();}
        if (recover && !s.loading_result.empty()) {std::cout<<s.loading_result<<std::endl;reader.stop();return 0;}
        std::cout<<"cycles="<<s.cycles;
        for (const auto& a:s.axes) std::cout<<" firmware="<<a.firmware<<" encoder="<<a.encoder<<" control="<<a.position<<" status="<<a.status;
        std::cout<<std::endl;
    }
    reader.stop();
    if (reader.snapshot().connected || reader.snapshot().message!="Disconnected") return 1;
    if (recover) {std::cerr<<"Recovery did not finish\n";return 1;}
    std::cout<<"Native read-only hardware test passed; handle closed\n";
}
