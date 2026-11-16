// Minimal JSON reader/writer (C++11, no dependencies).
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace fc {

class JVal {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };

    JVal() : t_(NUL), b_(false), n_(0) {}
    JVal(bool b) : t_(BOOL), b_(b), n_(0) {}
    JVal(int n) : t_(NUM), b_(false), n_(n) {}
    JVal(unsigned n) : t_(NUM), b_(false), n_(n) {}
    JVal(double n) : t_(NUM), b_(false), n_(n) {}
    JVal(const char* s) : t_(STR), b_(false), n_(0), s_(s) {}
    JVal(const std::string& s) : t_(STR), b_(false), n_(0), s_(s) {}

    static JVal array() { JVal v; v.t_ = ARR; return v; }
    static JVal object() { JVal v; v.t_ = OBJ; return v; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == NUL; }
    bool isArray() const { return t_ == ARR; }
    bool isObject() const { return t_ == OBJ; }

    double num(double def = 0) const { return t_ == NUM ? n_ : (t_ == BOOL ? (b_ ? 1 : 0) : def); }
    int i(int def = 0) const { return t_ == NUM ? (int)n_ : def; }
    bool boolean(bool def = false) const { return t_ == BOOL ? b_ : (t_ == NUM ? n_ != 0 : def); }
    const std::string& str() const { return s_; }
    std::string str(const std::string& def) const { return t_ == STR ? s_ : def; }

    size_t size() const { return t_ == ARR ? a_.size() : (t_ == OBJ ? o_.size() : 0); }
    const JVal& operator[](size_t idx) const;
    const JVal& operator[](int idx) const { return idx < 0 ? (*this)[(size_t)-1] : (*this)[(size_t)idx]; }
    const JVal& operator[](const char* key) const;
    bool has(const char* key) const;

    JVal& push(const JVal& v);
    JVal& set(const std::string& key, const JVal& v);

    // Members of an object, in file order.
    const std::vector<std::pair<std::string, JVal> >& members() const { return o_; }

    std::string dump() const;
    void dump(std::string& out) const;
    static bool parse(const std::string& text, JVal& out, std::string* err = 0);

private:
    Type t_;
    bool b_;
    double n_;
    std::string s_;
    std::vector<JVal> a_;
    std::vector<std::pair<std::string, JVal> > o_;
    friend class JParser;
};

bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);

}  // namespace fc
