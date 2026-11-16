// Linux implementation of open_bridge() / HidDevice over the kernel's hidraw interface
// (/dev/hidraw*). No library is needed: the ioctls in <linux/hidraw.h> give the ids and the
// report descriptor, from which the usage and the report sizes are read here.
// Contract: hid/hid_device.hpp (source of truth); notes: docs/dev/hid-bridge.md, docs/dev/linux-build.md.
//
// hidraw has no seize, so the open is made exclusive as far as it can be: flock(LOCK_EX|LOCK_NB)
// keeps a second Mesmer (another X-Plane, a test tool) out and reads as BridgeBusy, as does an OS
// refusal (EACCES: the udev rule is missing). A program that ignores advisory locks can still read
// the same reports; nothing on Linux plays CLS2Sim's role today.
// read() polls the descriptor with the timeout; write() is a plain write of the 64 bytes with the
// report ID (0x3f) as byte 0, which is how hidraw frames a numbered report.
#include "hid/lin/hid_lin.hpp"
#include "hid/hid_device.hpp"
#include "hid/usb_ids.hpp"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace brunner {
namespace {
// Owns one file descriptor.
class Fd {
    int fd_=-1;
 public:
    Fd()=default;
    explicit Fd(int fd):fd_(fd) {}
    ~Fd() { if (fd_>=0) ::close(fd_); }
    Fd(const Fd&)=delete;
    Fd& operator=(const Fd&)=delete;
    Fd(Fd&& other) noexcept:fd_(other.fd_) { other.fd_=-1; }
    Fd& operator=(Fd&& other) noexcept { if (this!=&other) { if (fd_>=0) ::close(fd_); fd_=other.fd_; other.fd_=-1; } return *this; }
    int get() const { return fd_; }
};

struct Descriptor {
    std::uint16_t usage_page=0,usage=0;
    std::uint16_t input_length=0,output_length=0; // bytes, report ID included
};
// Reads the top-level usage and the total input and output report sizes from a HID report
// descriptor (HID 1.11 section 6.2.2). Only what the bridge needs: short items, one top-level
// collection, report sizes summed over every Input / Output main item.
Descriptor parse_descriptor(const std::uint8_t* data,std::size_t size) {
    Descriptor out;
    std::uint32_t page=0,report_size=0,report_count=0,usage=0;
    bool have_usage=false,top_seen=false,numbered=false;
    std::uint64_t input_bits=0,output_bits=0;
    for (std::size_t i=0;i<size;) {
        const std::uint8_t prefix=data[i];
        if (prefix==0xFE) { // long item: skip it
            if (i+1>=size) break;
            i+=3u+data[i+1];
            continue;
        }
        const std::size_t length=prefix&3u?(prefix&3u)==3u?4u:prefix&3u:0u;
        if (i+1+length>size) break;
        std::uint32_t value=0;
        for (std::size_t b=0;b<length;++b) value|=static_cast<std::uint32_t>(data[i+1+b])<<(8*b);
        const unsigned type=(prefix>>2)&3u,tag=prefix>>4;
        if (type==1) { // global
            if (tag==0x0) page=value;
            else if (tag==0x7) report_size=value;
            else if (tag==0x8) numbered=true;
            else if (tag==0x9) report_count=value;
        } else if (type==2) { // local
            if (tag==0x0) {usage=value;have_usage=true;}
        } else if (type==0) { // main
            if (tag==0xA && !top_seen && have_usage) { // first Collection: the top-level usage
                out.usage_page=static_cast<std::uint16_t>(page);out.usage=static_cast<std::uint16_t>(usage);top_seen=true;
            } else if (tag==0x8) input_bits+=std::uint64_t(report_size)*report_count;
            else if (tag==0x9) output_bits+=std::uint64_t(report_size)*report_count;
            if (tag!=0xA) have_usage=false; // a main item other than a Collection consumes the local usage
        }
        i+=1+length;
    }
    // hidraw delivers a numbered report with its ID as byte 0, so the sizes count it.
    out.input_length=static_cast<std::uint16_t>((input_bits+7)/8+(input_bits && numbered?1:0));
    out.output_length=static_cast<std::uint16_t>((output_bits+7)/8+(output_bits && numbered?1:0));
    return out;
}

// Whether /sys/class/hidraw/<name>/device/uevent ("HID_ID=0003:000025BB:00000081") names a wanted id:
// the answer for a node that cannot be opened.
bool sysfs_id_wanted(const std::string& name,const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    std::FILE* file=std::fopen(("/sys/class/hidraw/"+name+"/device/uevent").c_str(),"r");
    if (!file) return false;
    char line[256];
    bool wanted=false;
    while (std::fgets(line,sizeof(line),file)) {
        unsigned bus=0,vendor=0,product=0;
        if (std::sscanf(line,"HID_ID=%x:%x:%x",&bus,&vendor,&product)==3) {
            wanted=id_listed(ids,static_cast<std::uint16_t>(vendor),static_cast<std::uint16_t>(product))
                || vendor_listed(unknown_vendors,static_cast<std::uint16_t>(vendor));
            break;
        }
    }
    std::fclose(file);
    return wanted;
}

struct Node {
    std::string path;
    HidCandidate candidate;
    Fd fd; // opened read/write; the descriptor and ids were read through it
};
// Opens every /dev/hidraw* of a wanted id (or, for an unknown model, of a wanted vendor) and
// describes it. `denied` counts the ones the OS refused.
std::vector<Node> enumerate(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors,unsigned& denied,std::string* text=nullptr) {
    std::vector<Node> nodes;
    denied=0;
    DIR* dir=::opendir("/dev");
    if (!dir) throw TransportError("Cannot enumerate HID devices");
    std::vector<std::string> names;
    while (const dirent* entry=::readdir(dir)) {
        if (std::strncmp(entry->d_name,"hidraw",6)==0) names.emplace_back(entry->d_name);
    }
    ::closedir(dir);
    for (const auto& name:names) {
        const std::string path="/dev/"+name;
        Fd fd(::open(path.c_str(),O_RDWR|O_CLOEXEC|O_NONBLOCK));
        if (fd.get()<0) {
            // Unreadable nodes are most often other devices (a keyboard needs root). Only one
            // that sysfs says has a wanted id counts as a bridge the OS refused.
            if ((errno==EACCES || errno==EPERM) && sysfs_id_wanted(name,ids,unknown_vendors)) ++denied;
            continue;
        }
        hidraw_devinfo info{};
        if (::ioctl(fd.get(),HIDIOCGRAWINFO,&info)<0) continue;
        const auto vendor=static_cast<std::uint16_t>(info.vendor),product=static_cast<std::uint16_t>(info.product);
        if (!id_listed(ids,vendor,product) && !vendor_listed(unknown_vendors,vendor)) continue;
        int size=0;
        if (::ioctl(fd.get(),HIDIOCGRDESCSIZE,&size)<0 || size<=0 || size>HID_MAX_DESCRIPTOR_SIZE) continue;
        hidraw_report_descriptor descriptor{};
        descriptor.size=static_cast<__u32>(size);
        if (::ioctl(fd.get(),HIDIOCGRDESC,&descriptor)<0) continue;
        const auto parsed=parse_descriptor(descriptor.value,static_cast<std::size_t>(size));
        Node node;
        node.path=path;
        node.candidate.vendor=vendor;node.candidate.product=product;
        node.candidate.usage_page=parsed.usage_page;node.candidate.usage=parsed.usage;
        node.candidate.input_length=parsed.input_length;node.candidate.output_length=parsed.output_length;
        if (text) {
            char line[200];
            std::snprintf(line,sizeof(line),"%s vendor=0x%04X product=0x%04X usage_page=0x%04x usage=0x%04x input=%u output=%u\n",path.c_str(),
                static_cast<unsigned>(vendor),static_cast<unsigned>(product),static_cast<unsigned>(parsed.usage_page),static_cast<unsigned>(parsed.usage),
                static_cast<unsigned>(parsed.input_length),static_cast<unsigned>(parsed.output_length));
            *text+=line;
        }
        node.fd=std::move(fd);
        nodes.push_back(std::move(node));
    }
    return nodes;
}

class LinuxHidDevice final : public HidDevice {
    Fd fd_;
    UsbId id_{};
    bool dead_=false; // removed, or an I/O failed: every later call fails
    // Waits until the descriptor is readable or `timeout_ms` passes; false on timeout.
    bool wait_readable(int timeout_ms) {
        pollfd p{fd_.get(),POLLIN,0};
        for (;;) {
            const int ready=::poll(&p,1,timeout_ms);
            if (ready<0 && errno==EINTR) continue;
            if (ready<0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL))) {dead_=true;throw TransportError("HID I/O failed");}
            return ready>0;
        }
    }
 public:
    LinuxHidDevice(Fd fd,UsbId id):fd_(std::move(fd)),id_(id) {
        // Exclusive as far as hidraw allows: a held lock is a busy bridge.
        if (::flock(fd_.get(),LOCK_EX|LOCK_NB)<0) {
            if (errno==EWOULDBLOCK) throw BridgeBusy{};
            throw TransportError("Cannot open HID device");
        }
        flush();
    }
    UsbId id() const override {return id_;}
    void write(const Report& report,int timeout_ms) override {
        if (dead_) throw TransportError("HID I/O failed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        for (;;) {
            const ssize_t n=::write(fd_.get(),report.data(),report.size());
            if (n==static_cast<ssize_t>(report.size())) return;
            if (n>=0) {dead_=true;throw TransportError("HID I/O failed");}
            if (errno==EINTR) continue;
            if (errno!=EAGAIN) {dead_=true;throw TransportError("HID I/O failed");}
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if (left<=0) {dead_=true;throw TransportError("HID I/O timed out");}
            pollfd p{fd_.get(),POLLOUT,0};
            ::poll(&p,1,static_cast<int>(left));
        }
    }
    void read(Report& report,int timeout_ms) override {
        if (dead_) throw TransportError("HID I/O failed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        for (;;) {
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if (!wait_readable(left<0?0:static_cast<int>(left))) throw TransportError("HID I/O timed out");
            std::uint8_t buffer[256];
            const ssize_t n=::read(fd_.get(),buffer,sizeof(buffer));
            if (n<0 && (errno==EAGAIN || errno==EINTR)) continue;
            if (n<0) {dead_=true;throw TransportError("HID I/O failed");}
            if (n!=static_cast<ssize_t>(report.size())) throw TransportError("Short HID report");
            std::memcpy(report.data(),buffer,report.size());
            return;
        }
    }
    void flush() override {
        if (dead_) throw TransportError("HID I/O failed");
        std::uint8_t buffer[256];
        for (;;) {
            const ssize_t n=::read(fd_.get(),buffer,sizeof(buffer));
            if (n>0) continue;
            if (n<0 && errno==EINTR) continue;
            if (n<0 && errno!=EAGAIN) {dead_=true;throw TransportError("HID I/O failed");}
            return;
        }
    }
};
}
std::unique_ptr<HidDevice> open_bridge(const std::vector<UsbId>& ids,const std::vector<std::uint16_t>& unknown_vendors) {
    unsigned denied=0;
    auto nodes=enumerate(ids,unknown_vendors,denied);
    std::vector<HidCandidate> candidates;
    for (const auto& n:nodes) candidates.push_back(n.candidate);
    try {
        const auto index=select_bridge(candidates,ids,unknown_vendors);
        return std::make_unique<LinuxHidDevice>(std::move(nodes[index].fd),UsbId{candidates[index].vendor,candidates[index].product});
    } catch (const BridgeAbsent&) {
        // A node the OS would not open may be the yoke itself (no udev rule): busy, not absent.
        if (denied) throw BridgeBusy{};
        throw;
    }
}
std::string describe_linux_bridges(const std::vector<UsbId>& ids) {
    try {
        unsigned denied=0;
        std::string text;
        enumerate(ids,{},denied,&text);
        if (denied) text+=std::to_string(denied)+" /dev/hidraw device(s) could not be opened (permission); see docs/dev/linux-build.md for the udev rule\n";
        return text.empty() ? std::string("no HID device with a listed bridge id\n") : text;
    } catch (const std::exception& e) {
        return std::string("cannot enumerate: ")+e.what()+"\n";
    }
}
}
