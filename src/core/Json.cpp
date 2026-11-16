#include "Json.h"
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace fc {

static const JVal kNull;

const JVal& JVal::operator[](size_t idx) const {
    if (t_ != ARR || idx >= a_.size()) return kNull;
    return a_[idx];
}

const JVal& JVal::operator[](const char* key) const {
    if (t_ != OBJ) return kNull;
    for (size_t k = 0; k < o_.size(); ++k)
        if (o_[k].first == key) return o_[k].second;
    return kNull;
}

bool JVal::has(const char* key) const {
    if (t_ != OBJ) return false;
    for (size_t k = 0; k < o_.size(); ++k)
        if (o_[k].first == key) return true;
    return false;
}

JVal& JVal::push(const JVal& v) {
    if (t_ != ARR) { *this = array(); }
    a_.push_back(v);
    return a_.back();
}

JVal& JVal::set(const std::string& key, const JVal& v) {
    if (t_ != OBJ) { *this = object(); }
    for (size_t k = 0; k < o_.size(); ++k)
        if (o_[k].first == key) { o_[k].second = v; return o_[k].second; }
    o_.push_back(std::make_pair(key, v));
    return o_.back().second;
}

static void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (size_t k = 0; k < s.size(); ++k) {
        unsigned char c = (unsigned char)s[k];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

void JVal::dump(std::string& out) const {
    switch (t_) {
        case NUL: out += "null"; break;
        case BOOL: out += b_ ? "true" : "false"; break;
        case NUM: {
            char buf[40];
            if (std::isfinite(n_) && n_ == std::floor(n_) && std::fabs(n_) < 1e15)
                snprintf(buf, sizeof buf, "%.0f", n_);
            else if (std::isfinite(n_))
                snprintf(buf, sizeof buf, "%.10g", n_);
            else
                snprintf(buf, sizeof buf, "0");
            out += buf;
            break;
        }
        case STR: dumpString(s_, out); break;
        case ARR:
            out += '[';
            for (size_t k = 0; k < a_.size(); ++k) {
                if (k) out += ',';
                a_[k].dump(out);
            }
            out += ']';
            break;
        case OBJ:
            out += '{';
            for (size_t k = 0; k < o_.size(); ++k) {
                if (k) out += ',';
                dumpString(o_[k].first, out);
                out += ':';
                o_[k].second.dump(out);
            }
            out += '}';
            break;
    }
}

std::string JVal::dump() const {
    std::string s;
    dump(s);
    return s;
}

class JParser {
public:
    JParser(const std::string& s) : p_(s.c_str()), end_(s.c_str() + s.size()), err_(0) {}
    const char* p_;
    const char* end_;
    const char* err_;
    int depth_ = 0;

    void ws() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) ++p_;
    }
    bool fail(const char* m) {
        if (!err_) err_ = m;
        return false;
    }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if ((size_t)(end_ - p_) < n || strncmp(p_, w, n) != 0) return fail("bad literal");
        p_ += n;
        return true;
    }
    bool str(std::string& out) {
        if (p_ >= end_ || *p_ != '"') return fail("expected string");
        ++p_;
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c == '\\') {
                if (p_ >= end_) return fail("bad escape");
                char e = *p_++;
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (end_ - p_ < 4) return fail("bad \\u");
                        char hex[5] = {p_[0], p_[1], p_[2], p_[3], 0};
                        p_ += 4;
                        unsigned cp = (unsigned)strtoul(hex, 0, 16);
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += e;
                }
            } else {
                out += c;
            }
        }
        if (p_ >= end_) return fail("unterminated string");
        ++p_;
        return true;
    }
    bool value(JVal& v) {
        if (++depth_ > 64) return fail("too deep");
        ws();
        if (p_ >= end_) return fail("unexpected end");
        bool ok = true;
        char c = *p_;
        if (c == '{') {
            v = JVal::object();
            ++p_;
            ws();
            if (p_ < end_ && *p_ == '}') { ++p_; }
            else {
                for (;;) {
                    ws();
                    std::string k;
                    if (!str(k)) { ok = false; break; }
                    ws();
                    if (p_ >= end_ || *p_ != ':') { ok = fail("expected :"); break; }
                    ++p_;
                    v.o_.push_back(std::make_pair(k, JVal()));
                    if (!value(v.o_.back().second)) { ok = false; break; }
                    ws();
                    if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                    if (p_ < end_ && *p_ == '}') { ++p_; break; }
                    ok = fail("expected , or }");
                    break;
                }
            }
        } else if (c == '[') {
            v = JVal::array();
            ++p_;
            ws();
            if (p_ < end_ && *p_ == ']') { ++p_; }
            else {
                for (;;) {
                    v.a_.push_back(JVal());
                    if (!value(v.a_.back())) { ok = false; break; }
                    ws();
                    if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                    if (p_ < end_ && *p_ == ']') { ++p_; break; }
                    ok = fail("expected , or ]");
                    break;
                }
            }
        } else if (c == '"') {
            v = JVal(std::string());
            ok = str(v.s_);
        } else if (c == 't') {
            ok = lit("true"); v = JVal(true);
        } else if (c == 'f') {
            ok = lit("false"); v = JVal(false);
        } else if (c == 'n') {
            ok = lit("null"); v = JVal();
        } else {
            char* e = 0;
            double d = strtod(p_, &e);
            if (e == p_ || e > end_) ok = fail("bad number");
            else { p_ = e; v = JVal(d); }
        }
        --depth_;
        return ok;
    }
};

bool JVal::parse(const std::string& text, JVal& out, std::string* err) {
    JParser ps(text);
    bool ok = ps.value(out);
    if (ok) {
        ps.ws();
        if (ps.p_ != ps.end_) ok = ps.fail("trailing data");
    }
    if (!ok && err) *err = ps.err_ ? ps.err_ : "parse error";
    return ok;
}

bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n ? fread(&out[0], 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fflush(f) == 0) && ok;
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace fc
