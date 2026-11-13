#pragma once
// The workbench's HTTP listener: one thread, a select loop over at most eight
// connections, none of which can hold up another. It never calls the X-Plane
// SDK or the recorder; it does the workbench's file writes.
#include "ui/workbench/workbench.hpp"
#include <atomic>
#include <thread>

namespace brunner::workbench {
// workbench.html, embedded by CMakeLists.txt.
extern const unsigned char page_bytes[];
extern const std::size_t page_size;
inline std::string embedded_page() {return std::string(reinterpret_cast<const char*>(page_bytes),page_size);}
struct ServerOptions {
    int port=8087; // 0 asks the OS for a free port (tests)
    std::string bind_address="0.0.0.0";
    std::function<std::string()> page;
};
// This machine's IPv4 addresses for the page's footer: private ranges first,
// loopback and virtual adapters left out where the OS says which they are.
std::vector<std::string> local_addresses();
std::string machine_name();
class Server {
public:
    Server()=default;
    Server(const Server&)=delete;
    Server& operator=(const Server&)=delete;
    ~Server() {stop();}
    // Binds and starts listening; false with a reason (port in use) otherwise.
    bool start(Workbench& bench,ServerOptions options,std::string& error);
    void stop();
    bool running() const {return thread_.joinable();}
    int port() const {return port_;}
    const std::vector<std::string>& addresses() const {return addresses_;}
private:
    void run();
    Workbench* bench_=nullptr;
    ServerOptions options_;
    std::string machine_;
    std::intptr_t listener_=-1;
    int port_=0;
    std::vector<std::string> addresses_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
}
