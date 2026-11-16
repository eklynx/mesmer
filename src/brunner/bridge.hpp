// The portable request/reply layer over a HidDevice (hid/hid_device.hpp). It is the
// `Transport` that brunner/reference.hpp, brunner/loading.hpp and force/buffet.hpp are templated over.
// One outstanding request, one dedicated worker. No SDK calls on this thread.
#pragma once
#include "hid/hid_device.hpp"
#include "brunner/link_diagnostics.hpp"
#include "brunner/loading.hpp"
#include "brunner/reference.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace buttons { class CanSource; }
namespace brunner {
class Bridge {
    std::unique_ptr<HidDevice> device_;
    std::uint16_t counter_=0;
    LinkDiagnostics* diag_=nullptr;
    buttons::CanSource* can_source_=nullptr;
 public:
    // The CanSource the reader reads the yoke's buttons through, in each sampling cycle (buttons/input/can_source.hpp); none by default.
    void set_can_source(buttons::CanSource* source) {can_source_=source;}
    buttons::CanSource* can_source() const {return can_source_;}
    // Where the stall log's facts go; the caller keeps it alive for the bridge's life. Optional.
    void set_diagnostics(LinkDiagnostics* diag) {diag_=diag;if (diag_) diag_->begin_session(Clock::now());}

    // Opens the platform's bridge among `ids` (may throw BridgeAbsent, BridgeBusy, TransportError).
    explicit Bridge(const std::vector<UsbId>& ids):device_(open_bridge(ids)) {}
    // For tests: any HidDevice.
    explicit Bridge(std::unique_ptr<HidDevice> device):device_(std::move(device)) {}
    UsbId id() const {return device_->id();}
    // reply_ms: how long to wait for the matching reply before it counts as missed (ReplyMissed).
    template<class Parse> std::uint32_t exchange(Report request,Parse parse,const std::atomic<bool>& stop,int reply_ms=default_reply_timeout_ms) {
        try {return exchange_once(request,parse,stop,reply_ms);}
        catch (const TransportError& e) {
            // Frozen now: the stop commands the reader sends next reuse the same fields.
            if (diag_ && diag_->at_error.empty()) {diag_->os_account=device_->describe();diag_->at_error=diag_->describe(Clock::now(),e.what());}
            throw;
        }
    }
 private:
    template<class Parse> std::uint32_t exchange_once(Report request,Parse parse,const std::atomic<bool>& stop,int reply_ms) {
        if (stop) throw OperationCancelled{};
        if (diag_) {
            diag_->stage=LinkDiagnostics::Stage::writing;diag_->last_write=Clock::now();++diag_->exchanges;diag_->unmatched_in_exchange=0;
            std::copy_n(request.begin(),diag_->request_head.size(),diag_->request_head.begin());
        }
        device_->write(request,100);
        if (diag_) diag_->stage=LinkDiagnostics::Stage::waiting;
        const auto end=Clock::now()+std::chrono::milliseconds(reply_ms);
        while (!stop && Clock::now()<end) {
            Report reply{};
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();
            try {device_->read(reply,static_cast<int>(std::max<std::int64_t>(1,remaining)));}
            catch (const ReadTimeout& e) {missed(e.what());}
            if (const auto result=parse(reply)) {
                if (diag_) {diag_->stage=LinkDiagnostics::Stage::idle;diag_->last_reply=Clock::now();++diag_->replies;diag_->note_latency(diag_->last_write,diag_->last_reply);}
                return *result;
            }
            if (diag_) {++diag_->unmatched_in_exchange;++diag_->unmatched_total;}
        }
        if (stop) throw OperationCancelled{};
        missed("No matching controller reply");
    }
    // A missed reply (ReplyMissed): what came late must not be taken for the next request's reply, so what is queued is dropped.
    [[noreturn]] void missed(const char* what) {
        try {device_->flush();} catch (const TransportError&) {}
        throw ReplyMissed(what);
    }
 public:
    // reply_ms >= 0: one attempt, waiting that long for the reply (the sampling reads, and the confirmation before a live
    // update, whose callers skip the cycle on a ReplyMissed). retried_read (the default): each attempt waits
    // load_write_reply_timeout_ms and a missing reply is retried with the same read, which changes nothing, until
    // load_write_tolerance_ms after the first attempt; a ReplyMissed after that is thrown.
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& stop,int reply_ms=retried_read) {
        return attempts(reply_ms,[&](int ms){return exchange(read_request(node,object,counter_++),[=](const Report& r){return read_reply(r,node,object);},stop,ms);});
    }
    std::uint32_t ref_read(unsigned node,Register reg,const std::atomic<bool>& stop,int reply_ms=retried_read) {
        return attempts(reply_ms,[&](int ms){return exchange(reference_read_request(node,reg,counter_++),[=](const Report& r){return reference_reply(r,node,reg,false);},stop,ms);});
    }
    // The writes of setup, reference and stop: one attempt, 250 ms. An unconfirmed step there must end the session.
    void ref_write(unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
        exchange(reference_write_request(node,op,counter_++),[=](const Report& r){return reference_reply(r,node,op.reg,true);},stop);
        const auto deadline=Clock::now()+std::chrono::milliseconds(op.settle_ms);
        while (Clock::now()<deadline && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // A loading write waits load_write_reply_timeout_ms (one sample period) for its reply; if it does not come the same
    // write is sent again (an absolute register value, so a repeat of one that did arrive changes nothing, and the loading
    // sequences read each value back), until load_write_tolerance_ms after the first attempt. A ReplyMissed after that, or
    // any other failure, is thrown as before.
    void load_write(unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
        attempts(retried_read,[&](int ms){
            exchange(loading_write_request(node,op,counter_++),[=](const Report& r){return reference_reply(r,node,op.reg,true);},stop,ms);
            return std::uint32_t{0};
        });
    }
 private:
    // One attempt at reply_ms when it is 0 or more; otherwise attempts of load_write_reply_timeout_ms, repeated on a
    // ReplyMissed until load_write_tolerance_ms after the first (a retry is counted in the diagnostics and forgets the
    // miss it follows; the miss that finally throws keeps its frozen facts).
    template<class Attempt> std::uint32_t attempts(int reply_ms,Attempt attempt) {
        if (reply_ms>=0) return attempt(reply_ms);
        const auto first=Clock::now();
        for (;;) {
            try {return attempt(load_write_reply_timeout_ms);}
            catch (const ReplyMissed&) {
                if (Clock::now()-first>=std::chrono::milliseconds(load_write_tolerance_ms)) throw;
                if (diag_) {++diag_->retries;diag_->at_error.clear();diag_->os_account.clear();}
            }
        }
    }
};
// A Bridge that can only read: SDO uploads of the live objects (read) and of the
// allowlisted registers (ref_read). Its type has no write members, so code that
// is given one cannot write to the yoke, whatever it is asked to do. The
// read-only session (brunner/reader.cpp) and the survey (brunner/reference.hpp) take this.
class ReadOnlyBridge {
    Bridge& bridge_;
 public:
    explicit ReadOnlyBridge(Bridge& bridge):bridge_(bridge) {}
    std::uint32_t read(unsigned node,Object object,const std::atomic<bool>& stop,int reply_ms=retried_read) {return bridge_.read(node,object,stop,reply_ms);}
    buttons::CanSource* can_source() const {return bridge_.can_source();}
    std::uint32_t ref_read(unsigned node,Register reg,const std::atomic<bool>& stop,int reply_ms=retried_read) {return bridge_.ref_read(node,reg,stop,reply_ms);}
};
}
