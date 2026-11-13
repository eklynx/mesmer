#include "ui/workbench/workbench_server.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstring>

namespace brunner::workbench {
namespace {
// The tiny socket shim: Winsock and BSD sockets differ only here.
#ifdef _WIN32
using Socket=SOCKET;
const Socket no_socket=INVALID_SOCKET;
void close_socket(Socket s) {closesocket(s);}
struct Winsock {
    bool ok=false;
    Winsock() {WSADATA data{};ok=WSAStartup(MAKEWORD(2,2),&data)==0;}
    ~Winsock() {if (ok) WSACleanup();}
};
std::string last_error() {return "socket error "+std::to_string(WSAGetLastError());}
bool prepare_client(Socket s) {u_long on=1;return ioctlsocket(s,FIONBIO,&on)==0;}
bool would_block() {const int e=WSAGetLastError();return e==WSAEWOULDBLOCK || e==WSAEINTR;}
bool selectable(Socket) {return true;} // a Winsock fd_set is a list; 9 sockets fit its 64
int send_some(Socket s,const char* data,std::size_t size) {return send(s,data,static_cast<int>(std::min<std::size_t>(size,1<<20)),0);}
int receive_some(Socket s,char* data,std::size_t size) {return recv(s,data,static_cast<int>(size),0);}
void shutdown_send(Socket s) {shutdown(s,SD_SEND);}
#else
using Socket=int;
const Socket no_socket=-1;
void close_socket(Socket s) {::close(s);}
std::string last_error() {return std::strerror(errno);}
bool prepare_client(Socket s) {
#ifdef SO_NOSIGPIPE
    const int one=1;setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
#endif
    const int flags=fcntl(s,F_GETFL,0);
    return flags!=-1 && fcntl(s,F_SETFL,flags|O_NONBLOCK)==0;
}
bool would_block() {return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR;}
bool selectable(Socket s) {return s>=0 && s<FD_SETSIZE;}
int send_some(Socket s,const char* data,std::size_t size) {
#ifdef MSG_NOSIGNAL
    return static_cast<int>(::send(s,data,size,MSG_NOSIGNAL));
#else
    return static_cast<int>(::send(s,data,size,0));
#endif
}
int receive_some(Socket s,char* data,std::size_t size) {return static_cast<int>(::recv(s,data,size,0));}
void shutdown_send(Socket s) {::shutdown(s,SHUT_WR);}
#endif
// Each connection is served without blocking the others: one select loop
// over the listener and at most eight connections, each with its own buffer
// and deadline.
constexpr std::size_t max_connections=8;
constexpr std::chrono::milliseconds first_byte_time{1000},request_time{2000},write_time{2000},drain_time{250},tick{100};
struct Connection {
    enum class Phase {reading,writing,draining};
    Socket socket=no_socket;std::uint32_t peer=0;
    Phase phase=Phase::reading;
    std::string in,out;std::size_t sent=0;
    Clock::time_point opened,deadline;
    // Reading: a first byte within 1 s and the whole request within 2 s.
    Clock::time_point due() const {return phase!=Phase::reading?deadline:opened+(in.empty()?first_byte_time:request_time);}
};
Socket as_socket(std::intptr_t s) {return static_cast<Socket>(s);}
bool private_range(std::uint32_t a) {return (a>>24)==10 || (a>>20)==((172u<<4)|1) || (a>>16)==((192u<<8)|168);}
std::string dotted(std::uint32_t a) {
    return std::to_string(a>>24)+"."+std::to_string((a>>16)&255)+"."+std::to_string((a>>8)&255)+"."+std::to_string(a&255);
}
std::vector<std::string> ordered(std::vector<std::uint32_t> found) {
    std::stable_sort(found.begin(),found.end(),[](std::uint32_t a,std::uint32_t b) {
        const auto rank=[](std::uint32_t x) {return private_range(x)?0:(x>>16)==((169u<<8)|254)?2:1;};
        return rank(a)<rank(b);
    });
    std::vector<std::string> out;
    for (const auto a:found) {const auto s=dotted(a);if (std::find(out.begin(),out.end(),s)==out.end()) out.push_back(s);}
    return out;
}
bool virtual_name(std::string name) {
    name=lower(std::move(name));
    for (const char* word:{"virtual","hyper-v","vethernet","vmware","virtualbox","wsl","docker","tap-","tunnel","pseudo","loopback"})
        if (name.find(word)!=std::string::npos) return true;
    return false;
}
#ifdef _WIN32
std::string narrow(const wchar_t* text) {
    std::string out;
    for (;text && *text;++text) out+=*text<128?char(*text):'?';
    return out;
}
#endif
}

std::vector<std::string> local_addresses() {
    std::vector<std::uint32_t> found;
#ifdef _WIN32
    ULONG size=16*1024;
    std::vector<unsigned char> buffer;
    ULONG result=ERROR_BUFFER_OVERFLOW;
    for (int attempt=0;attempt<3 && result==ERROR_BUFFER_OVERFLOW;++attempt) {
        buffer.resize(size);
        result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    }
    if (result!=NO_ERROR) return {};
    for (auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next) {
        if (a->OperStatus!=IfOperStatusUp || a->IfType==IF_TYPE_SOFTWARE_LOOPBACK || a->IfType==IF_TYPE_TUNNEL) continue;
        if (virtual_name(narrow(a->Description)) || virtual_name(narrow(a->FriendlyName))) continue;
        for (auto* u=a->FirstUnicastAddress;u;u=u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family!=AF_INET) continue;
            const auto* in=reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            found.push_back(ntohl(in->sin_addr.s_addr));
        }
    }
#else
    ifaddrs* list=nullptr;
    if (getifaddrs(&list)!=0) return {};
    for (auto* a=list;a;a=a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family!=AF_INET || !(a->ifa_flags&IFF_UP) || (a->ifa_flags&IFF_LOOPBACK)) continue;
        const std::string name=a->ifa_name?a->ifa_name:"";
        bool skip=virtual_name(name);
        for (const char* prefix:{"veth","br-","utun","bridge","vmnet","vboxnet","llw","awdl","anpi","ap1"}) skip=skip || name.rfind(prefix,0)==0;
        if (skip) continue;
        found.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr));
    }
    freeifaddrs(list);
#endif
    return ordered(std::move(found));
}
std::string machine_name() {
#ifdef _WIN32
    Winsock ws;
#endif
    char name[256]{};
    if (gethostname(name,sizeof(name)-1)!=0) return {};
    std::string s(name);
    return s.substr(0,s.find('.'));
}

bool Server::start(Workbench& bench,ServerOptions options,std::string& error) {
    stop();
#ifdef _WIN32
    {WSADATA data{};if (WSAStartup(MAKEWORD(2,2),&data)!=0) {error="Winsock unavailable";return false;}}
#endif
    const auto fail=[&](std::string why) {
        if (listener_!=-1) {close_socket(as_socket(listener_));listener_=-1;}
#ifdef _WIN32
        WSACleanup();
#endif
        error=std::move(why);return false;
    };
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<std::uint16_t>(options.port));
    if (inet_pton(AF_INET,options.bind_address.c_str(),&address.sin_addr)!=1) return fail("invalid bind address");
    const Socket s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s==no_socket) return fail(last_error());
    listener_=static_cast<std::intptr_t>(s);
#ifdef _WIN32
    // Exclusive: a second listener on the port fails here instead of sharing it.
    const BOOL one=TRUE;setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one));
#else
    const int one=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
#endif
    if (::bind(s,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0)
        return fail("port "+std::to_string(options.port)+" in use ("+last_error()+")");
    if (::listen(s,16)!=0) return fail(last_error());
    if (!prepare_client(s)) return fail(last_error()); // accept never blocks the loop
    sockaddr_in bound{};
#ifdef _WIN32
    int length=sizeof(bound);
#else
    socklen_t length=sizeof(bound);
#endif
    getsockname(s,reinterpret_cast<sockaddr*>(&bound),&length);
    port_=ntohs(bound.sin_port);
    bench_=&bench;options_=std::move(options);machine_=machine_name();
    addresses_=options_.bind_address=="0.0.0.0"?local_addresses():std::vector<std::string>{options_.bind_address};
    bench.set_listen(addresses_,port_);
    stop_=false;
    try {thread_=std::thread(&Server::run,this);}
    catch (const std::exception& e) {return fail(e.what());}
    return true;
}
void Server::stop() {
    stop_=true;
    if (thread_.joinable()) thread_.join();
    if (listener_!=-1) {
        close_socket(as_socket(listener_));listener_=-1;
#ifdef _WIN32
        WSACleanup();
#endif
    }
}
void Server::run() {
    std::vector<Connection> connections;
    const auto drop=[&](std::size_t i) {close_socket(connections[i].socket);connections.erase(connections.begin()+static_cast<std::ptrdiff_t>(i));};
    // Sends what it can; true while the connection is still wanted.
    const auto write=[&](Connection& c,Clock::time_point now) {
        while (c.sent<c.out.size()) {
            const int n=send_some(c.socket,c.out.data()+c.sent,c.out.size()-c.sent);
            if (n<0 && would_block()) return true;
            if (n<=0) return false;
            c.sent+=static_cast<std::size_t>(n);
        }
        // Read what the client still sends, briefly, so closing does not reset
        // the connection before it has read the answer.
        shutdown_send(c.socket);
        c.phase=Connection::Phase::draining;c.deadline=now+drain_time;
        return true;
    };
    const auto answer=[&](Connection& c,const Response& r,Clock::time_point now) {
        c.out=build_response(r);c.sent=0;c.phase=Connection::Phase::writing;c.deadline=now+write_time;
        return write(c,now);
    };
    const auto read=[&](Connection& c,Clock::time_point now) {
        char chunk[4096];
        for (;;) {
            const int n=receive_some(c.socket,chunk,sizeof(chunk));
            if (n<0 && would_block()) break;
            if (n<=0) return false; // closed, or failed, before a whole request
            c.in.append(chunk,static_cast<std::size_t>(n));
            if (c.in.size()>max_header_bytes+max_body_bytes+4) break;
        }
        Request request;
        switch (parse_request(c.in,request)) {
        case ParseResult::incomplete:
            if (c.in.size()>max_header_bytes+max_body_bytes+4) return answer(c,error_response(400,"Malformed request"),now);
            return true;
        case ParseResult::bad:return answer(c,error_response(400,"Malformed request"),now);
        case ParseResult::too_large: {
            const auto* host=request.header("host");
            if (!host || !allowed_host(*host,machine_)) return answer(c,error_response(403,"Host not allowed"),now);
            return answer(c,error_response(413,"The body is over 4 KiB"),now);
        }
        case ParseResult::complete:break;
        }
        Site site{*bench_,options_.page,machine_};
        Response response;
        try {response=respond(site,request,now);}
        catch (const std::exception&) {response=error_response(500,"The workbench failed to answer");}
        return answer(c,response,now);
    };
    const auto drain=[&](Connection& c) {
        char sink[1024];
        for (;;) {
            const int n=receive_some(c.socket,sink,sizeof(sink));
            if (n<0 && would_block()) return true;
            if (n<=0) return false;
        }
    };
    const Socket listener=as_socket(listener_);
    auto last_persist=Clock::now();
    while (!stop_) {
        auto now=Clock::now();
        for (std::size_t i=connections.size();i-->0;) if (now>=connections[i].due()) drop(i);
        fd_set readable,writable;
        FD_ZERO(&readable);FD_ZERO(&writable);
        FD_SET(listener,&readable);
        Socket top=listener;
        auto wake=now+tick;
        for (const auto& c:connections) {
            if (c.phase==Connection::Phase::writing) FD_SET(c.socket,&writable);
            else FD_SET(c.socket,&readable);
            top=std::max(top,c.socket);
            wake=std::min(wake,c.due());
        }
        const auto wait=std::clamp<long long>(std::chrono::duration_cast<std::chrono::microseconds>(wake-now).count(),1000,100000);
        timeval tv{0,static_cast<decltype(tv.tv_usec)>(wait)};
        const int ready=::select(static_cast<int>(top+1),&readable,&writable,nullptr,&tv);
        now=Clock::now();
        if (ready>0) {
            // Serve the connections that were waited on, then take new ones.
            for (std::size_t i=connections.size();i-->0;) {
                auto& c=connections[i];
                bool keep=true;
                // Nothing a connection does may end the thread (std::terminate would end X-Plane).
                try {
                    if (c.phase==Connection::Phase::reading && FD_ISSET(c.socket,&readable)) keep=read(c,now);
                    else if (c.phase==Connection::Phase::writing && FD_ISSET(c.socket,&writable)) keep=write(c,now);
                    else if (c.phase==Connection::Phase::draining && FD_ISSET(c.socket,&readable)) keep=drain(c);
                } catch (...) {keep=false;}
                if (!keep) drop(i);
            }
            if (FD_ISSET(listener,&readable)) for (int accepted=0;accepted<int(max_connections);++accepted) {
                sockaddr_in peer{};
#ifdef _WIN32
                int length=sizeof(peer);
#else
                socklen_t length=sizeof(peer);
#endif
                const Socket client=::accept(listener,reinterpret_cast<sockaddr*>(&peer),&length);
                if (client==no_socket) break;
                if (!selectable(client) || !prepare_client(client)) {close_socket(client);continue;}
                if (connections.size()>=max_connections) {
                    // Full: the oldest connection still waiting for its request
                    // makes room; failing that, the oldest of all, so a new
                    // request (a Stop) is never refused outright.
                    const auto pick=[&](bool reading_only) {
                        std::size_t oldest=connections.size();
                        for (std::size_t i=0;i<connections.size();++i)
                            if ((!reading_only || connections[i].phase==Connection::Phase::reading)
                                && (oldest==connections.size() || connections[i].opened<connections[oldest].opened)) oldest=i;
                        return oldest;
                    };
                    auto oldest=pick(true);
                    if (oldest==connections.size()) oldest=pick(false);
                    drop(oldest);
                }
                try {
                    Connection c;c.socket=client;c.peer=ntohl(peer.sin_addr.s_addr);c.opened=now;
                    connections.push_back(std::move(c));
                } catch (...) {close_socket(client);continue;}
                auto& added=connections.back();
                bool keep=true;
                try {if (!allowed_peer(added.peer)) keep=answer(added,error_response(403,"Only the local network may connect"),now);}
                catch (...) {keep=false;}
                if (!keep) drop(connections.size()-1);
            }
        }
        if (now-last_persist>=std::chrono::milliseconds(200)) {
            try {bench_->persist();} catch (const std::exception&) {}
            last_persist=now;
        }
    }
    while (!connections.empty()) drop(connections.size()-1);
    try {bench_->persist();} catch (const std::exception&) {}
}
}
