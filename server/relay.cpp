// Frontier Classic relay server.
//
// Lets people play together without port forwarding at the players' homes and
// without typing IP addresses: the host opens a "room" under his player name,
// the others join that name, and this server passes the game traffic along.
// It does not look inside the game data and stores nothing on disk.
//
//   relay [port]          (or environment variable PORT; default 47800)
//
// One process, one thread, poll(). Linux only (meant for Docker).
//
// Wire format (same framing as the game): [u32 length][u8 type][payload].
//   first message on a new connection:
//     100 HOST   {u16 version, str room}  -> this connection controls a room
//     101 JOIN   {str room}               -> wants to play in that room
//     102 ACCEPT {u32 token}              -> host's data connection for one joiner
//   on a host control connection:
//     104 PING   {}                       -> keep-alive
//     105 REFUSE {u32 token, str reason}  -> host does not want that joiner
//   server -> host control: 110 OPENED {}, 111 JOINREQ {u32 token}
//   server -> anyone:       14 BYE {str reason}   (the game shows the reason)
// After ACCEPT the joiner and the host's data connection are simply piped.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

static const size_t kMaxConnections = 400;
static const size_t kMaxRooms = 100;
static const size_t kMaxHandshake = 512;             // bytes before a connection must have said what it is
static const size_t kMaxBuffered = 8u * 1024 * 1024; // per connection (a full game state is < 1 MB)
static const int kHandshakeSeconds = 15;
static const int kJoinWaitSeconds = 25;
static const int kHostSilenceSeconds = 90;           // the host pings every 15 s

enum Kind { K_NEW, K_HOST, K_WAITING, K_PIPED };

struct Conn {
    int fd = -1;
    Kind kind = K_NEW;
    std::string in, out;
    int peer = -1;         // K_PIPED: the other side's fd
    std::string room;      // K_HOST: its room, K_WAITING: the room it wants
    uint32_t token = 0;    // K_WAITING
    time_t since = 0, lastHeard = 0;
    bool closing = false;  // close once `out` is flushed
};

static std::map<int, Conn> conns;
static std::map<std::string, int> rooms;   // room name -> host control fd
static std::map<uint32_t, int> waiting;    // token -> joiner fd
static volatile sig_atomic_t running = 1;
static unsigned long long totalBytes = 0, totalGames = 0;

static void onSignal(int) { running = 0; }

static void logLine(const char* fmt, const char* a = "", long b = 0) {
    char when[32];
    time_t t = time(0);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
    printf("%s  ", when);
    printf(fmt, a, b);
    printf("  [connections %zu, rooms %zu]\n", conns.size(), rooms.size());
    fflush(stdout);
}

// ---- framing helpers
static void putU32(std::string& s, uint32_t v) {
    for (int k = 0; k < 4; ++k) s += (char)((v >> (8 * k)) & 255);
}
static uint32_t getU32(const std::string& s, size_t p) {
    return (uint32_t)(unsigned char)s[p] | ((uint32_t)(unsigned char)s[p + 1] << 8) |
           ((uint32_t)(unsigned char)s[p + 2] << 16) | ((uint32_t)(unsigned char)s[p + 3] << 24);
}
static void sendFrame(Conn& c, int type, const std::string& payload) {
    putU32(c.out, (uint32_t)payload.size() + 1);
    c.out += (char)type;
    c.out += payload;
}
static std::string strField(const std::string& s) {
    std::string p;
    putU32(p, (uint32_t)s.size());
    return p + s;
}
static void bye(Conn& c, const std::string& reason) {
    sendFrame(c, 14, strField(reason));
    c.closing = true;
}
// Takes one frame from c.in. Returns 1 = got one, 0 = need more data, -1 = invalid.
static int takeFrame(Conn& c, int& type, std::string& payload, size_t maxLen) {
    if (c.in.size() < 5) return 0;
    uint32_t len = getU32(c.in, 0);
    if (len < 1 || len > maxLen) return -1;
    if (c.in.size() < 4 + (size_t)len) return 0;
    type = (unsigned char)c.in[4];
    payload.assign(c.in, 5, len - 1);
    c.in.erase(0, 4 + (size_t)len);
    return 1;
}
static bool readStr(const std::string& p, size_t& pos, std::string& out, size_t maxLen) {
    if (pos + 4 > p.size()) return false;
    uint32_t n = getU32(p, pos);
    pos += 4;
    if (n > maxLen || pos + n > p.size()) return false;
    out.assign(p, pos, n);
    pos += n;
    return true;
}
// Room names are compared without capitals and without odd characters.
static std::string roomKey(const std::string& name) {
    std::string k;
    for (size_t i = 0; i < name.size() && k.size() < 24; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ') k += c;
    }
    while (!k.empty() && k[k.size() - 1] == ' ') k.erase(k.size() - 1);
    while (!k.empty() && k[0] == ' ') k.erase(0, 1);
    return k;
}

static void closeConn(int fd);

static void dropWaiting(Conn& c) {
    if (c.kind == K_WAITING) waiting.erase(c.token);
}

static void closeConn(int fd) {
    std::map<int, Conn>::iterator it = conns.find(fd);
    if (it == conns.end()) return;
    Conn c = it->second;
    conns.erase(it);
    close(fd);
    if (c.kind == K_WAITING) waiting.erase(c.token);
    if (c.kind == K_HOST) {
        std::map<std::string, int>::iterator r = rooms.find(c.room);
        if (r != rooms.end() && r->second == fd) rooms.erase(r);
        // people still waiting for this host are told
        for (std::map<int, Conn>::iterator w = conns.begin(); w != conns.end(); ++w)
            if (w->second.kind == K_WAITING && w->second.room == c.room && !w->second.closing) {
                waiting.erase(w->second.token);
                w->second.kind = K_NEW;
                bye(w->second, "De host heeft de lobby gesloten");
            }
        logLine("room closed: %s", c.room.c_str());
    }
    if (c.kind == K_PIPED && c.peer >= 0) {
        std::map<int, Conn>::iterator p = conns.find(c.peer);
        if (p != conns.end()) {
            p->second.peer = -1;
            p->second.closing = true;  // deliver what is queued, then hang up
        }
    }
}

static void handleNew(Conn& c) {
    int type;
    std::string payload;
    int r = takeFrame(c, type, payload, kMaxHandshake);
    if (r == 0) {
        if (c.in.size() > kMaxHandshake) c.closing = true;
        return;
    }
    if (r < 0) { c.closing = true; c.in.clear(); return; }
    size_t pos = 0;
    if (type == 100) {  // HOST
        std::string name;
        if (payload.size() < 2) { c.closing = true; return; }
        pos = 2;
        if (!readStr(payload, pos, name, 64)) { c.closing = true; return; }
        std::string key = roomKey(name);
        if (key.empty()) { bye(c, "Kies eerst een naam"); return; }
        if (rooms.count(key)) { bye(c, "Die naam is al in gebruik op de server: kies een andere naam"); return; }
        if (rooms.size() >= kMaxRooms) { bye(c, "De server is vol"); return; }
        rooms[key] = c.fd;
        c.kind = K_HOST;
        c.room = key;
        sendFrame(c, 110, "");
        logLine("room opened: %s", key.c_str());
    } else if (type == 101) {  // JOIN
        std::string name;
        if (!readStr(payload, pos, name, 64)) { c.closing = true; return; }
        std::string key = roomKey(name);
        std::map<std::string, int>::iterator room = rooms.find(key);
        if (room == rooms.end() || !conns.count(room->second)) {
            c.in.clear();
            bye(c, "Geen open spel gevonden met de naam \"" + name.substr(0, 24) + "\"");
            return;
        }
        uint32_t token;
        do {
            token = ((uint32_t)rand() << 16) ^ (uint32_t)rand() ^ (uint32_t)time(0);
        } while (token == 0 || waiting.count(token));
        waiting[token] = c.fd;
        c.kind = K_WAITING;
        c.room = key;
        c.token = token;
        c.since = time(0);
        std::string p;
        putU32(p, token);
        sendFrame(conns[room->second], 111, p);
    } else if (type == 102) {  // ACCEPT: the host's data connection for one joiner
        if (payload.size() < 4) { c.closing = true; return; }
        uint32_t token = getU32(payload, 0);
        std::map<uint32_t, int>::iterator w = waiting.find(token);
        if (w == waiting.end() || !conns.count(w->second)) { c.closing = true; return; }
        Conn& j = conns[w->second];
        waiting.erase(w);
        j.kind = K_PIPED;
        j.peer = c.fd;
        c.kind = K_PIPED;
        c.peer = j.fd;
        // whatever each side already sent goes straight through
        c.out += j.in;
        j.in.clear();
        j.out += c.in;
        c.in.clear();
        ++totalGames;
        logLine("player joined room: %s", j.room.c_str());
    } else {
        c.closing = true;
    }
}

static void handleHost(Conn& c) {
    for (;;) {
        int type;
        std::string payload;
        int r = takeFrame(c, type, payload, kMaxHandshake);
        if (r == 0) return;
        if (r < 0) { c.closing = true; c.in.clear(); return; }
        if (type == 105 && payload.size() >= 4) {  // REFUSE
            uint32_t token = getU32(payload, 0);
            size_t pos = 4;
            std::string reason;
            if (!readStr(payload, pos, reason, 200)) reason = "De host laat nu niemand toe";
            std::map<uint32_t, int>::iterator w = waiting.find(token);
            if (w != waiting.end() && conns.count(w->second)) {
                Conn& j = conns[w->second];
                waiting.erase(w);
                j.kind = K_NEW;
                j.in.clear();
                bye(j, reason);
            }
        }
        // 104 PING and anything else: nothing to do
    }
}

static void onData(Conn& c) {
    c.lastHeard = time(0);
    switch (c.kind) {
        case K_NEW:
            if (!c.closing) handleNew(c);
            else c.in.clear();
            if (c.kind == K_HOST) handleHost(c);  // more messages may have arrived together
            break;
        case K_HOST:
            handleHost(c);
            break;
        case K_WAITING:
            if (c.in.size() > kMaxBuffered) c.closing = true;
            break;
        case K_PIPED: {
            std::map<int, Conn>::iterator p = conns.find(c.peer);
            if (p == conns.end()) { c.in.clear(); c.closing = true; break; }
            totalBytes += c.in.size();
            p->second.out += c.in;
            c.in.clear();
            if (p->second.out.size() > kMaxBuffered) p->second.closing = true, p->second.out.clear();
            break;
        }
    }
}

int main(int argc, char** argv) {
    int port = 47800;
    if (getenv("PORT")) port = atoi(getenv("PORT"));
    if (argc > 1) port = atoi(argv[1]);
    if (port < 1 || port > 65535) port = 47800;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, onSignal);
    signal(SIGINT, onSignal);
    srand((unsigned)time(0) ^ (unsigned)getpid());

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (lfd < 0 || bind(lfd, (struct sockaddr*)&addr, sizeof addr) != 0 || listen(lfd, 32) != 0) {
        printf("cannot listen on port %d: %s\n", port, strerror(errno));
        return 1;
    }
    fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL, 0) | O_NONBLOCK);
    printf("Frontier Classic relay listening on TCP port %d\n", port);
    fflush(stdout);

    std::vector<struct pollfd> pfds;
    std::vector<int> toClose;
    char buf[65536];
    while (running) {
        pfds.clear();
        struct pollfd l = {lfd, POLLIN, 0};
        pfds.push_back(l);
        for (std::map<int, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
            struct pollfd p = {it->first, (short)(POLLIN | (it->second.out.empty() ? 0 : POLLOUT)), 0};
            pfds.push_back(p);
        }
        int n = poll(&pfds[0], pfds.size(), 1000);
        if (n < 0 && errno != EINTR) break;
        toClose.clear();

        if (n > 0 && (pfds[0].revents & POLLIN)) {
            for (;;) {
                int fd = accept(lfd, 0, 0);
                if (fd < 0) break;
                if (conns.size() >= kMaxConnections) { close(fd); continue; }
                fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                Conn c;
                c.fd = fd;
                c.since = c.lastHeard = time(0);
                conns[fd] = c;
            }
        }
        for (size_t k = 1; n > 0 && k < pfds.size(); ++k) {
            std::map<int, Conn>::iterator it = conns.find(pfds[k].fd);
            if (it == conns.end()) continue;
            Conn& c = it->second;
            bool dead = false;
            if (pfds[k].revents & POLLIN) {
                for (int rounds = 0; rounds < 8; ++rounds) {
                    ssize_t r = recv(c.fd, buf, sizeof buf, 0);
                    if (r > 0) {
                        c.in.append(buf, (size_t)r);
                        onData(c);
                    } else if (r == 0) {
                        dead = true;
                        break;
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) dead = true;
                        break;
                    }
                }
            }
            if (pfds[k].revents & (POLLERR | POLLNVAL)) dead = true;
            if ((pfds[k].revents & POLLHUP) && !(pfds[k].revents & POLLIN)) dead = true;
            if (dead) toClose.push_back(c.fd);
        }
        // write whatever is queued (also data queued during this round)
        time_t now = time(0);
        for (std::map<int, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
            Conn& c = it->second;
            if (!c.out.empty()) {
                ssize_t w = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
                if (w > 0) c.out.erase(0, (size_t)w);
                else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) toClose.push_back(c.fd);
            }
            if (c.closing && c.out.empty()) toClose.push_back(c.fd);
            if (c.closing && now - c.lastHeard > 20) toClose.push_back(c.fd);  // never wait forever on a stuck peer
            if (c.kind == K_NEW && !c.closing && now - c.since > kHandshakeSeconds) toClose.push_back(c.fd);
            if (c.kind == K_WAITING && now - c.since > kJoinWaitSeconds) {
                dropWaiting(c);
                c.kind = K_NEW;
                c.in.clear();
                bye(c, "De host reageert niet");
            }
            if (c.kind == K_HOST && now - c.lastHeard > kHostSilenceSeconds) toClose.push_back(c.fd);
        }
        for (size_t k = 0; k < toClose.size(); ++k) closeConn(toClose[k]);
    }
    printf("relay stopping (passed on %llu bytes, %llu joins)\n", totalBytes, totalGames);
    return 0;
}
