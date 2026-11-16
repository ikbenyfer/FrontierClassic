#include "Net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define FC_CLOSE closesocket
#define FC_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS || (e) == WSAEALREADY)
static int lastErr() { return WSAGetLastError(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define FC_CLOSE ::close
#define FC_WOULDBLOCK(e) ((e) == EWOULDBLOCK || (e) == EAGAIN || (e) == EINPROGRESS || (e) == EINTR)
static int lastErr() { return errno; }
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace fc {

static const size_t kMaxMessage = 16u * 1024u * 1024u;

bool netInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        done = true;
    }
#endif
    return true;
}

unsigned netNowMs() {
#ifdef _WIN32
    return (unsigned)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
#endif
}

static void setNonBlocking(int fd) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&one, sizeof one);
#endif
}

std::vector<std::string> netLocalAddresses() {
    std::vector<std::string> out;
    netInit();
#ifdef _WIN32
    char name[256];
    if (gethostname(name, sizeof name) == 0) {
        struct addrinfo hints, *res = 0;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        if (getaddrinfo(name, 0, &hints, &res) == 0) {
            for (struct addrinfo* a = res; a; a = a->ai_next) {
                struct sockaddr_in* sin = (struct sockaddr_in*)a->ai_addr;
                const char* s = inet_ntoa(sin->sin_addr);
                if (s && strncmp(s, "127.", 4) != 0) out.push_back(s);
            }
            freeaddrinfo(res);
        }
    }
#else
    struct ifaddrs* list = 0;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
            char buf[64];
            struct sockaddr_in* sin = (struct sockaddr_in*)a->ifa_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) out.push_back(buf);
        }
        freeifaddrs(list);
    }
#endif
    return out;
}

// ------------------------------------------------------------------ Conn

void Conn::fail(const std::string& why) {
    if (error_.empty()) error_ = why;
    if (fd_ >= 0) FC_CLOSE(fd_);
    fd_ = -1;
    connecting_ = false;
}

void Conn::close() {
    if (fd_ >= 0) FC_CLOSE(fd_);
    fd_ = -1;
    connecting_ = false;
    in_.clear();
    out_.clear();
}

void Conn::adopt(int fd) {
    close();
    error_.clear();
    fd_ = fd;
    setNonBlocking(fd_);
}

bool Conn::connectTo(const std::string& host, int port, std::string* err) {
    close();
    error_.clear();
    netInit();
    struct addrinfo hints, *res = 0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof portStr, "%d", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        if (err) *err = "Adres niet gevonden";
        return false;
    }
    int fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        if (err) *err = "Kan geen verbinding maken";
        return false;
    }
    setNonBlocking(fd);
    int r = connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen);
    int e = lastErr();
    freeaddrinfo(res);
    if (r != 0 && !FC_WOULDBLOCK(e)) {
        FC_CLOSE(fd);
        if (err) *err = "Verbinden mislukt";
        return false;
    }
    fd_ = fd;
    connecting_ = r != 0;
    connectStart_ = netNowMs();
    return true;
}

void Conn::send(uint8_t type, const std::vector<uint8_t>& payload) {
    if (fd_ < 0) return;
    uint32_t len = (uint32_t)payload.size() + 1;
    out_.push_back(len & 255);
    out_.push_back((len >> 8) & 255);
    out_.push_back((len >> 16) & 255);
    out_.push_back((len >> 24) & 255);
    out_.push_back(type);
    out_.insert(out_.end(), payload.begin(), payload.end());
    if (out_.size() > 64u * 1024u * 1024u) fail("Verbinding te traag");
}

void Conn::pump() {
    if (fd_ < 0) return;
    if (connecting_) {
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET((unsigned)fd_, &wr);
        FD_SET((unsigned)fd_, &ex);
        struct timeval tv = {0, 0};
        int r = select(fd_ + 1, 0, &wr, &ex, &tv);
        if (r > 0) {
            int soerr = 0;
            socklen_t sl = sizeof soerr;
            getsockopt(fd_, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
            if (soerr != 0 || FD_ISSET((unsigned)fd_, &ex)) {
                fail("Verbinden mislukt (host niet bereikbaar?)");
                return;
            }
            connecting_ = false;
        } else if (netNowMs() - connectStart_ > 10000) {
            fail("Geen antwoord van de host (controleer IP, poort en port forwarding)");
            return;
        } else {
            return;
        }
    }
    // write
    while (!out_.empty()) {
        size_t chunk = out_.size() < 65536 ? out_.size() : 65536;
        int n = (int)::send(fd_, (const char*)&out_[0], (int)chunk, MSG_NOSIGNAL);
        if (n > 0) {
            out_.erase(out_.begin(), out_.begin() + n);
        } else {
            if (n < 0 && FC_WOULDBLOCK(lastErr())) break;
            fail("Verbinding verbroken");
            return;
        }
    }
    // read
    for (;;) {
        char buf[16384];
        int n = (int)recv(fd_, buf, sizeof buf, 0);
        if (n > 0) {
            in_.insert(in_.end(), buf, buf + n);
            if (in_.size() > kMaxMessage * 2) { fail("Te veel data"); return; }
        } else if (n == 0) {
            fail("Verbinding gesloten");
            return;
        } else {
            if (FC_WOULDBLOCK(lastErr())) break;
            fail("Verbinding verbroken");
            return;
        }
    }
}

bool Conn::next(uint8_t& type, std::vector<uint8_t>& payload) {
    if (in_.size() < 5) return false;
    uint32_t len = (uint32_t)in_[0] | ((uint32_t)in_[1] << 8) | ((uint32_t)in_[2] << 16) | ((uint32_t)in_[3] << 24);
    if (len < 1 || len > kMaxMessage) {
        fail("Ongeldig bericht");
        in_.clear();
        return false;
    }
    if (in_.size() < 4 + (size_t)len) return false;
    type = in_[4];
    payload.assign(in_.begin() + 5, in_.begin() + 4 + len);
    in_.erase(in_.begin(), in_.begin() + 4 + len);
    return true;
}

void Conn::sendRaw(const std::string& bytes) {
    if (fd_ < 0) return;
    out_.insert(out_.end(), bytes.begin(), bytes.end());
}

std::string Conn::takeRaw() {
    std::string s(in_.begin(), in_.end());
    in_.clear();
    return s;
}

// ------------------------------------------------------------------ PublicIp

static const char* const kIpServices[] = {"api.ipify.org", "checkip.amazonaws.com", "icanhazip.com"};
static const int kIpServiceCount = 3;

std::string PublicIp::parse(const std::string& response) {
    size_t body = response.find("\r\n\r\n");
    if (body == std::string::npos || response.compare(0, 5, "HTTP/") != 0) return std::string();
    if (response.find(" 200") > 15) return std::string();  // status line must say 200
    const std::string b = response.substr(body + 4);
    // first token that looks like a.b.c.d with each part 0..255
    for (size_t i = 0; i < b.size(); ++i) {
        if (b[i] < '0' || b[i] > '9') continue;
        if (i > 0 && (b[i - 1] == '.' || (b[i - 1] >= '0' && b[i - 1] <= '9'))) continue;
        size_t p = i;
        int parts = 0;
        bool ok = true;
        while (parts < 4 && ok) {
            int v = 0, digits = 0;
            while (p < b.size() && b[p] >= '0' && b[p] <= '9' && digits < 4) { v = v * 10 + (b[p] - '0'); ++p; ++digits; }
            if (digits < 1 || digits > 3 || v > 255) { ok = false; break; }
            ++parts;
            if (parts < 4) {
                if (p < b.size() && b[p] == '.') ++p;
                else ok = false;
            }
        }
        if (ok && parts == 4 && (p >= b.size() || !((b[p] >= '0' && b[p] <= '9') || b[p] == '.'))) return b.substr(i, p - i);
    }
    return std::string();
}

void PublicIp::start() {
    conn_.close();
    ip_.clear();
    attempt_ = -1;
    state_ = 1;
    tryNext();
}

void PublicIp::tryNext() {
    conn_.close();
    buf_.clear();
    sent_ = false;
    for (;;) {
        ++attempt_;
        if (attempt_ >= kIpServiceCount) { state_ = 3; return; }
        std::string err;
        if (conn_.connectTo(kIpServices[attempt_], 80, &err)) break;
    }
    startMs_ = netNowMs();
}

void PublicIp::pump() {
    if (state_ != 1) return;
    conn_.pump();
    if (conn_.ready() && !sent_) {
        sent_ = true;
        conn_.sendRaw(std::string("GET / HTTP/1.0\r\nHost: ") + kIpServices[attempt_] +
                      "\r\nUser-Agent: FrontierClassic\r\nAccept: text/plain\r\nConnection: close\r\n\r\n");
        conn_.pump();
    }
    buf_ += conn_.takeRaw();
    if (buf_.size() > 16384) buf_.resize(16384);
    std::string found = parse(buf_);
    bool closed = !conn_.open();
    if (!found.empty() && (closed || buf_.size() > found.size())) {
        // wait for the connection to close so a split address is never cut short
        if (closed || netNowMs() - startMs_ > 1500) {
            ip_ = found;
            state_ = 2;
            conn_.close();
            return;
        }
    }
    if (closed || netNowMs() - startMs_ > 6000) tryNext();
}

// ------------------------------------------------------------------ Listener

bool Listener::listen(int port, std::string* err) {
    close();
    netInit();
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (err) *err = "Kan geen socket maken";
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof addr) != 0 || ::listen(fd, 8) != 0) {
        FC_CLOSE(fd);
        if (err) *err = "Poort is al in gebruik of niet toegestaan";
        return false;
    }
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    fd_ = fd;
    return true;
}

int Listener::acceptFd() {
    if (fd_ < 0) return -1;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    int c = (int)accept(fd_, (struct sockaddr*)&ss, &sl);
    return c < 0 ? -1 : c;
}

void Listener::close() {
    if (fd_ >= 0) FC_CLOSE(fd_);
    fd_ = -1;
}

// ------------------------------------------------------------------ Writer / Reader

void Writer::f32(float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    u32(u);
}

float Reader::f32() {
    uint32_t u = u32();
    float v;
    memcpy(&v, &u, 4);
    return v;
}

std::string Reader::str() {
    uint32_t n = u32();
    if (!ok || p + n > b.size()) { ok = false; return std::string(); }
    if (n == 0) return std::string();
    std::string s((const char*)&b[0] + p, n);
    p += n;
    return s;
}

}  // namespace fc
