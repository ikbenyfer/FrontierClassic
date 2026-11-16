// Small TCP layer for multiplayer: non-blocking sockets, polled once per frame
// (no threads). Works on iOS, Windows and Linux. Messages are framed as
// [u32 length][u8 type][payload], little-endian.
// This is the ONLY part of the game that uses the network, and only when the
// player opens the multiplayer screen.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fc {

bool netInit();  // once at start-up (WSAStartup on Windows)
unsigned netNowMs();  // monotonic milliseconds

// The device's own IPv4 addresses (for the host lobby screen).
std::vector<std::string> netLocalAddresses();

class Conn {
public:
    Conn() {}
    ~Conn() { close(); }
    // Starts a non-blocking connect. `host` is an IP address or a host name.
    bool connectTo(const std::string& host, int port, std::string* err);
    void adopt(int fd);  // an accepted socket
    void close();
    bool open() const { return fd_ >= 0; }
    bool connecting() const { return fd_ >= 0 && connecting_; }
    bool ready() const { return fd_ >= 0 && !connecting_; }
    const std::string& error() const { return error_; }

    void send(uint8_t type, const std::vector<uint8_t>& payload);
    // Flushes outgoing data and reads what has arrived. Closes on errors.
    void pump();
    // Takes the next complete message, if any.
    bool next(uint8_t& type, std::vector<uint8_t>& payload);
    size_t pendingOut() const { return out_.size(); }

private:
    Conn(const Conn&);
    Conn& operator=(const Conn&);
    int fd_ = -1;
    bool connecting_ = false;
    unsigned connectStart_ = 0;
    std::vector<uint8_t> in_, out_;
    std::string error_;
    void fail(const std::string& why);
};

class Listener {
public:
    ~Listener() { close(); }
    bool listen(int port, std::string* err);
    int acceptFd();  // -1 when nobody is waiting
    void close();
    bool open() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

// ---- message building / parsing ----
struct Writer {
    std::vector<uint8_t> b;
    void u8(unsigned v) { b.push_back((uint8_t)v); }
    void u16(unsigned v) { u8(v & 255); u8((v >> 8) & 255); }
    void u32(uint32_t v) { u16(v & 0xffff); u16(v >> 16); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void f32(float v);
    void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
};

struct Reader {
    const std::vector<uint8_t>& b;
    size_t p;
    bool ok;
    explicit Reader(const std::vector<uint8_t>& buf) : b(buf), p(0), ok(true) {}
    unsigned u8() { if (p + 1 > b.size()) { ok = false; return 0; } return b[p++]; }
    unsigned u16() { unsigned a = u8(); return a | (u8() << 8); }
    uint32_t u32() { uint32_t a = u16(); return a | ((uint32_t)u16() << 16); }
    int32_t i32() { return (int32_t)u32(); }
    float f32();
    std::string str();
};

}  // namespace fc
