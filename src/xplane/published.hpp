// SOURCE OF TRUTH for how the plugin publishes its own datarefs (brunner_direct/...)
// to X-Plane and other plugins: the machinery of registering and unregistering
// accessors. What each dataref means and returns is the plugin's (plugin.cpp
// builds the tables and supplies the callbacks); reading X-Plane's own datarefs is
// xplane/sim.hpp.
//
// Thread: every call, and every callback, runs on X-Plane's thread (a dataref is
// read or written by X-Plane, another plugin or a script, never by our threads).
#pragma once
#include <deque>
#include <functional>
#include <string>

namespace brunner::xplane {
class PublishedDatarefs {
 public:
    struct Int {
        std::string name;
        bool writable=false;
        std::function<int()> read;            // required
        std::function<void(int)> write;       // called only when writable; X-Plane asks, the callback validates
    };
    // Registers an integer dataref. Happy path: readable by anyone, and writable when asked.
    // Errors: none thrown; if X-Plane refuses the name the dataref is simply absent, as before.
    void publish(Int ref);
    // Registers a read-only text dataref (type Data): X-Plane asks for the length (no buffer) or for a slice.
    void publish_text(std::string name,std::function<std::string()> text);
    // Unregisters everything published; safe to call twice. Call before the callbacks' owners go away.
    void unpublish_all() noexcept;

 private:
    friend struct PublishedCallbacks;
    struct Entry {
        Int integer;
        std::function<std::string()> text;
        void* handle=nullptr; // XPLMDataRef
    };
    std::deque<Entry> entries_; // stable addresses: each is a refcon
};
}
