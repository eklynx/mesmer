// The portable request/reply layer over a HidDevice (hid_device.hpp). It is the
// `Transport` that reference.hpp, loading.hpp and buffet.hpp are templated over.
// One outstanding request, one dedicated worker. No SDK calls on this thread.
#pragma once
#include "hid_device.hpp"
#include "loading.hpp"
#include "reference.hpp"
#include <algorithm>
#include <atomic>
#include <thread>

namespace brunner {
class Bridge {
    std::unique_ptr<HidDevice> device_;
    std::uint16_t counter_=0;
 public:
    // Opens the platform's bridge (may throw BridgeAbsent, BridgeBusy, TransportError).
    Bridge():device_(open_bridge()) {}
    // For tests: any HidDevice.
    explicit Bridge(std::unique_ptr<HidDevice> device):device_(std::move(device)) {}
    template<class Parse> std::uint32_t exchange(Report request,Parse parse,const std::atomic<bool>& stop) {
        if (stop) throw OperationCancelled{};
        device_->write(request,100);
        const auto end=Clock::now()+std::chrono::milliseconds(250);
        while (!stop && Clock::now()<end) {
            Report reply{};
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();
            device_->read(reply,static_cast<int>(std::max<std::int64_t>(1,remaining)));
            if (const auto result=parse(reply)) return *result;
        }
        if (stop) throw OperationCancelled{};
        throw TransportError("No matching controller reply");
    }
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& stop) {
        return exchange(read_request(node,object,counter_++),[=](const Report& r){return read_reply(r,node,object);},stop);
    }
    std::uint32_t ref_read(unsigned node,Register reg,const std::atomic<bool>& stop) {
        return exchange(reference_read_request(node,reg,counter_++),[=](const Report& r){return reference_reply(r,node,reg,false);},stop);
    }
    void ref_write(unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
        exchange(reference_write_request(node,op,counter_++),[=](const Report& r){return reference_reply(r,node,op.reg,true);},stop);
        const auto deadline=Clock::now()+std::chrono::milliseconds(op.settle_ms);
        while (Clock::now()<deadline && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    void load_write(unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
        exchange(loading_write_request(node,op,counter_++),[=](const Report& r){return reference_reply(r,node,op.reg,true);},stop);
    }
};
// A Bridge that can only read: SDO uploads of the live objects (read) and of the
// allowlisted registers (ref_read). Its type has no write members, so code that
// is given one cannot write to the yoke, whatever it is asked to do. The
// read-only session (reader.cpp) and the survey (reference.hpp) take this.
class ReadOnlyBridge {
    Bridge& bridge_;
 public:
    explicit ReadOnlyBridge(Bridge& bridge):bridge_(bridge) {}
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& stop) {return bridge_.read(node,object,stop);}
    std::uint32_t ref_read(unsigned node,Register reg,const std::atomic<bool>& stop) {return bridge_.ref_read(node,reg,stop);}
};
}
