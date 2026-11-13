// X-Plane implementation of PublishedDatarefs. Contract: xplane/published.hpp.
#include "xplane/published.hpp"
#include "XPLMDataAccess.h"
#include <algorithm>
#include <cstring>

namespace brunner::xplane {
struct PublishedCallbacks {
    using Entry=PublishedDatarefs::Entry;
    static int read_int(void* refcon) {return static_cast<Entry*>(refcon)->integer.read();}
    static void write_int(void* refcon,int value) {
        auto& entry=*static_cast<Entry*>(refcon);
        if (entry.integer.write) entry.integer.write(value);
    }
    static int read_text(void* refcon,void* output,int offset,int maximum) {
        const std::string text=static_cast<Entry*>(refcon)->text();
        if (!output) return static_cast<int>(text.size());
        if (offset<0 || maximum<=0 || static_cast<std::size_t>(offset)>=text.size()) return 0;
        const auto length=std::min(maximum,static_cast<int>(text.size())-offset);
        std::memcpy(output,text.data()+offset,static_cast<std::size_t>(length));return length;
    }
};

void PublishedDatarefs::publish(Int ref) {
    entries_.push_back(Entry{std::move(ref),{},nullptr});
    auto& entry=entries_.back();
    const bool writable=entry.integer.writable;
    entry.handle=XPLMRegisterDataAccessor(entry.integer.name.c_str(),xplmType_Int,writable?1:0,
        PublishedCallbacks::read_int,writable?PublishedCallbacks::write_int:nullptr,
        nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&entry,&entry);
}
void PublishedDatarefs::publish_text(std::string name,std::function<std::string()> text) {
    Int ref;ref.name=std::move(name);
    entries_.push_back(Entry{std::move(ref),std::move(text),nullptr});
    auto& entry=entries_.back();
    entry.handle=XPLMRegisterDataAccessor(entry.integer.name.c_str(),xplmType_Data,0,
        nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,PublishedCallbacks::read_text,nullptr,&entry,nullptr);
}
void PublishedDatarefs::unpublish_all() noexcept {
    for (auto& entry:entries_) if (entry.handle) {XPLMUnregisterDataAccessor(static_cast<XPLMDataRef>(entry.handle));entry.handle=nullptr;}
    entries_.clear();
}
}
