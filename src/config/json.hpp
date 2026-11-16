#pragma once
// A strict JSON reader (request bodies, the profiles file) and a small writer.
#include "config/json_errors.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brunner {
// ---- JSON: a strict reader for request bodies and a small writer ----
struct JsonError:std::runtime_error {using std::runtime_error::runtime_error;};
struct Json {
    enum class Kind {null,boolean,integer,real,string,object,array} kind=Kind::null;
    bool boolean=false;std::int64_t integer=0;double real=0;std::string text;
    std::vector<std::pair<std::string,Json>> members;std::vector<Json> items;
    const Json* get(std::string_view key) const {
        for (const auto& m:members) if (m.first==key) return &m.second;
        return nullptr;
    }
};
class JsonReader {
    std::string_view s_;std::size_t i_=0;
    [[noreturn]] void fail(const char* why) const {throw JsonError(why);}
    void space() {while (i_<s_.size() && (s_[i_]==' ' || s_[i_]=='\t' || s_[i_]=='\n' || s_[i_]=='\r')) ++i_;}
    bool take(char c) {space();if (i_<s_.size() && s_[i_]==c) {++i_;return true;}return false;}
    void literal(std::string_view word) {if (s_.substr(i_,word.size())!=word) fail(json_errors::invalid_literal);i_+=word.size();}
    static void utf8(std::string& out,std::uint32_t c) {
        if (c<0x80) out+=char(c);
        else if (c<0x800) {out+=char(0xc0|(c>>6));out+=char(0x80|(c&0x3f));}
        else if (c<0x10000) {out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}
        else {out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&0x3f));out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}
    }
    std::uint32_t hex4() {
        if (i_+4>s_.size()) fail(json_errors::short_escape);
        std::uint32_t v=0;
        for (int k=0;k<4;++k) {
            const char c=s_[i_++];v<<=4;
            if (c>='0' && c<='9') v|=std::uint32_t(c-'0');
            else if (c>='a' && c<='f') v|=std::uint32_t(c-'a'+10);
            else if (c>='A' && c<='F') v|=std::uint32_t(c-'A'+10);
            else fail(json_errors::bad_escape);
        }
        return v;
    }
    std::string string() {
        if (!take('"')) fail(json_errors::expected_string);
        std::string out;
        for (;;) {
            if (i_>=s_.size()) fail(json_errors::unterminated_string);
            const auto c=static_cast<unsigned char>(s_[i_++]);
            if (c=='"') return out;
            if (c<0x20) fail(json_errors::control_character_in_string);
            if (c!='\\') {
                // Validate UTF-8 as it passes.
                const int extra=c<0x80?0:(c>>5)==0x6?1:(c>>4)==0xe?2:(c>>3)==0x1e?3:-1;
                if (extra<0 || (c&0xfe)==0xc0 || i_+extra>s_.size()) fail(json_errors::invalid_utf8);
                out+=char(c);
                for (int k=0;k<extra;++k) {
                    const auto d=static_cast<unsigned char>(s_[i_++]);
                    if ((d&0xc0)!=0x80) fail(json_errors::invalid_utf8);
                    out+=char(d);
                }
                continue;
            }
            if (i_>=s_.size()) fail(json_errors::unterminated_escape);
            switch (s_[i_++]) {
            case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;
            case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u': {
                auto c1=hex4();
                if (c1>=0xdc00 && c1<=0xdfff) fail(json_errors::lone_surrogate);
                if (c1>=0xd800 && c1<=0xdbff) {
                    if (i_+2>s_.size() || s_[i_]!='\\' || s_[i_+1]!='u') fail(json_errors::lone_surrogate);
                    i_+=2;const auto c2=hex4();
                    if (c2<0xdc00 || c2>0xdfff) fail(json_errors::lone_surrogate);
                    c1=0x10000+((c1-0xd800)<<10)+(c2-0xdc00);
                }
                utf8(out,c1);break;
            }
            default:fail(json_errors::bad_escape);
            }
        }
    }
    Json value(int depth) {
        if (depth>8) fail(json_errors::too_deep);
        space();
        if (i_>=s_.size()) fail(json_errors::unexpected_end);
        Json j;
        const char c=s_[i_];
        if (c=='{') {
            ++i_;j.kind=Json::Kind::object;
            if (take('}')) return j;
            do {
                space();auto key=string();
                if (j.get(key)) fail(json_errors::duplicate_key);
                if (!take(':')) fail(json_errors::expected_colon);
                j.members.emplace_back(std::move(key),value(depth+1));
            } while (take(','));
            if (!take('}')) fail(json_errors::expected_close_brace);
            return j;
        }
        if (c=='[') {
            ++i_;j.kind=Json::Kind::array;
            if (take(']')) return j;
            do j.items.push_back(value(depth+1)); while (take(','));
            if (!take(']')) fail(json_errors::expected_close_bracket);
            return j;
        }
        if (c=='"') {j.kind=Json::Kind::string;j.text=string();return j;}
        if (c=='t') {literal("true");j.kind=Json::Kind::boolean;j.boolean=true;return j;}
        if (c=='f') {literal("false");j.kind=Json::Kind::boolean;return j;}
        if (c=='n') {literal("null");return j;}
        const auto start=i_;
        if (s_[i_]=='-') ++i_;
        if (i_>=s_.size() || s_[i_]<'0' || s_[i_]>'9') fail(json_errors::invalid_value);
        if (s_[i_]=='0' && i_+1<s_.size() && s_[i_+1]>='0' && s_[i_+1]<='9') fail(json_errors::leading_zero);
        while (i_<s_.size() && s_[i_]>='0' && s_[i_]<='9') ++i_;
        bool real=false;
        if (i_<s_.size() && s_[i_]=='.') {
            real=true;++i_;
            if (i_>=s_.size() || s_[i_]<'0' || s_[i_]>'9') fail(json_errors::invalid_number);
            while (i_<s_.size() && s_[i_]>='0' && s_[i_]<='9') ++i_;
        }
        if (i_<s_.size() && (s_[i_]=='e' || s_[i_]=='E')) {
            real=true;++i_;
            if (i_<s_.size() && (s_[i_]=='+' || s_[i_]=='-')) ++i_;
            if (i_>=s_.size() || s_[i_]<'0' || s_[i_]>'9') fail(json_errors::invalid_number);
            while (i_<s_.size() && s_[i_]>='0' && s_[i_]<='9') ++i_;
        }
        const auto token=s_.substr(start,i_-start);
        if (real) {
            // Whatever the process's locale (X-Plane's may use a decimal comma).
            std::istringstream in{std::string(token)};in.imbue(std::locale::classic());
            if (!(in>>j.real) || !std::isfinite(j.real)) fail(json_errors::number_out_of_range);
            j.kind=Json::Kind::real;return j;
        }
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),j.integer);
        if (parsed.ec!=std::errc{}) fail(json_errors::integer_out_of_range);
        j.kind=Json::Kind::integer;return j;
    }
public:
    explicit JsonReader(std::string_view text):s_(text) {}
    Json parse() {auto v=value(0);space();if (i_!=s_.size()) fail(json_errors::trailing_characters);return v;}
};
inline Json parse_json(std::string_view text) {return JsonReader(text).parse();}

// Writer: valid UTF-8 passes through; anything else becomes '?'.
inline void write_string(std::string& out,std::string_view text) {
    out+='"';
    for (std::size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);
        if (c=='"' || c=='\\') {out+='\\';out+=char(c);++i;continue;}
        if (c<0x20) {
            static const char hex[]="0123456789abcdef";
            out+="\\u00";out+=hex[c>>4];out+=hex[c&15];++i;continue;
        }
        if (c<0x80) {out+=char(c);++i;continue;}
        const std::size_t extra=(c>>5)==0x6?1:(c>>4)==0xe?2:(c>>3)==0x1e?3:0;
        bool ok=extra>0 && (c&0xfe)!=0xc0 && i+extra<text.size();
        for (std::size_t k=1;ok && k<=extra;++k) ok=(static_cast<unsigned char>(text[i+k])&0xc0)==0x80;
        if (!ok) {out+='?';++i;continue;}
        out.append(text.substr(i,extra+1));i+=extra+1;
    }
    out+='"';
}
inline std::string json_string(std::string_view text) {std::string out;write_string(out,text);return out;}
inline std::string number_or_null(const std::optional<int>& v) {return v?std::to_string(*v):"null";}
inline std::optional<int> rounded(double v) {
    if (!std::isfinite(v) || std::abs(v)>1e9) return {};
    return static_cast<int>(std::lround(v));
}
inline std::string decimal_or_null(double v,int places=1) {
    if (!std::isfinite(v) || std::abs(v)>1e9) return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out.setf(std::ios::fixed);out.precision(places);out<<v;
    auto s=out.str();
    if (s.front()=='-' && s.find_first_not_of("-0.")==std::string::npos) s.erase(0,1);
    return s;
}
// Six significant figures, for a profile figure such as 0.0127.
inline std::string number_json(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(6);out<<v;
    return out.str();
}
// Back to text, members in the order read: a mark's settings are kept as saved.
inline std::string write_json(const Json& j) {
    switch (j.kind) {
    case Json::Kind::null:return "null";
    case Json::Kind::boolean:return j.boolean?"true":"false";
    case Json::Kind::integer:return std::to_string(j.integer);
    case Json::Kind::real:return number_json(j.real);
    case Json::Kind::string:return json_string(j.text);
    case Json::Kind::array: {
        std::string o="[";
        for (std::size_t i=0;i<j.items.size();++i) o+=(i?",":"")+write_json(j.items[i]);
        return o+"]";
    }
    case Json::Kind::object: {
        std::string o="{";
        for (std::size_t i=0;i<j.members.size();++i) o+=(i?",":"")+json_string(j.members[i].first)+":"+write_json(j.members[i].second);
        return o+"}";
    }
    }
    return "null";
}
}
