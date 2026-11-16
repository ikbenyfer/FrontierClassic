#include "Session.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace fc {

enum MsgType {
    MSG_HELLO = 1,  // client -> host: magic, version
    MSG_ORDER = 2,  // client -> host: kind, a, b, f
    MSG_LOBBY = 10,  // host -> client: your slot, player count, info text
    MSG_START = 11,  // host -> client: your player id, full game as JSON
    MSG_DELTA = 12,  // host -> client: what changed
    MSG_TOAST = 13,  // host -> client: a message for this player
    MSG_BYE = 14,    // host -> client: reason
};

// ====================================================================== host

bool HostSession::start(int port, std::string* err) {
    stop();
    if (!listener_.listen(port, err)) return false;
    port_ = port;
    inGame_ = false;
    lastLobbyCount_ = -1;
    return true;
}

void HostSession::stop() {
    for (size_t k = 0; k < clients_.size(); ++k) {
        Writer w;
        w.str("De host heeft het spel gesloten");
        clients_[k]->conn.send(MSG_BYE, w.b);
        clients_[k]->conn.pump();
    }
    clients_.clear();
    listener_.close();
    inGame_ = false;
    sentOwner_.clear();
}

int HostSession::clientCount() const {
    int n = 0;
    for (size_t k = 0; k < clients_.size(); ++k)
        if (clients_[k]->greeted && clients_[k]->conn.open()) ++n;
    return n;
}

void HostSession::acceptNew(bool reject) {
    for (;;) {
        int fd = listener_.acceptFd();
        if (fd < 0) break;
        std::unique_ptr<Client> c(new Client());
        c->conn.adopt(fd);
        if (reject || (int)clients_.size() >= kMaxClients) {
            Writer w;
            w.str(reject ? "Dit spel is al begonnen" : "Het spel is vol");
            c->conn.send(MSG_BYE, w.b);
            c->conn.pump();
            continue;  // connection closes when c goes out of scope
        }
        clients_.push_back(std::move(c));
    }
}

void HostSession::dropDead(Game* g) {
    for (size_t k = 0; k < clients_.size();) {
        Client& c = *clients_[k];
        if (c.conn.open()) { ++k; continue; }
        if (g && c.playerId > 0 && g->isHuman(c.playerId)) {
            g->addEvent(g->players[c.playerId].name + " heeft het spel verlaten", 0xFFCC66);
            g->makeAI(c.playerId);
            namesDirty_ = true;
        }
        clients_.erase(clients_.begin() + k);
        lastLobbyCount_ = -1;
    }
}

void HostSession::pumpLobby(const std::string& info) {
    if (!running()) return;
    acceptNew(false);
    for (size_t k = 0; k < clients_.size(); ++k) {
        Client& c = *clients_[k];
        c.conn.pump();
        uint8_t type;
        std::vector<uint8_t> payload;
        while (c.conn.next(type, payload)) {
            if (type != MSG_HELLO) continue;
            Reader r(payload);
            unsigned magic = r.u32(), version = r.u16();
            if (!r.ok || magic != kProtocolMagic || version != kProtocolVersion) {
                Writer w;
                w.str("Verschillende spelversies: werk beide apparaten bij");
                c.conn.send(MSG_BYE, w.b);
                c.conn.pump();
                c.conn.close();
                break;
            }
            c.greeted = true;
            lastLobbyCount_ = -1;
        }
    }
    dropDead(0);
    unsigned now = netNowMs();
    int count = clientCount();
    if (count != lastLobbyCount_ || info != lastLobbyInfo_ || now - lastLobbyMs_ > 1000) {
        lastLobbyCount_ = count;
        lastLobbyInfo_ = info;
        lastLobbyMs_ = now;
        int slot = 2;
        for (size_t k = 0; k < clients_.size(); ++k) {
            Client& c = *clients_[k];
            if (!c.greeted) continue;
            Writer w;
            w.u8(slot++);
            w.u8(count + 1);
            w.str(info);
            c.conn.send(MSG_LOBBY, w.b);
            c.conn.pump();
        }
    }
}

void HostSession::startGame(Game& g) {
    // clients that never said hello are dropped
    for (size_t k = 0; k < clients_.size();)
        if (!clients_[k]->greeted || !clients_[k]->conn.open()) clients_.erase(clients_.begin() + k);
        else ++k;
    std::string json = g.toJson().dump();
    int id = 2;
    for (size_t k = 0; k < clients_.size(); ++k) {
        Client& c = *clients_[k];
        c.playerId = id++;
        c.lastEventSeq = g.eventSeq;
        Writer w;
        w.u8(c.playerId);
        w.str(json);
        c.conn.send(MSG_START, w.b);
        c.conn.pump();
    }
    // a human slot without a client (someone left at the last moment) is played by the AI
    for (int p = id; p < (int)g.players.size(); ++p)
        if (g.isHuman(p)) g.makeAI(p);
    sentOwner_ = g.owner;
    sentBuildingsHash_ = 0;
    namesDirty_ = true;
    inGame_ = true;
    lastSendMs_ = 0;
}

void HostSession::handleOrder(Game& g, Client& c, const std::vector<uint8_t>& payload) {
    Reader r(payload);
    int kind = r.u8();
    int a = r.i32(), b = r.i32();
    float f = r.f32();
    if (!r.ok || !g.isHuman(c.playerId)) return;
    const int p = c.playerId;
    std::string why;
    bool ok = true;
    switch (kind) {
        case ORD_SPAWN:
            ok = g.spawnPlayer(p, a);
            if (!ok && !g.players[p].spawned) why = "Kies vrij land (geen water of bezet gebied)";
            break;
        case ORD_ATTACK: {
            if (!(f > 0)) f = 0;
            if (f > 1) f = 1;
            if (g.players[p].alive) ok = g.orderAttack(p, a, g.players[p].troops * f, &why);
            break;
        }
        case ORD_BUILD:
            if (g.players[p].alive) ok = g.build(p, a, b, &why);
            break;
        case ORD_UPGRADE: {
            int bi = g.buildingAt(a);
            if (bi >= 0) ok = g.upgrade(p, bi, &why);
            break;
        }
        case ORD_RECRUIT:
            if (g.players[p].alive) {
                ok = g.recruit(p, &why);
                if (ok) why = "+1500 troepen";
            }
            break;
        default:
            break;
    }
    (void)ok;
    if (!why.empty()) {
        Writer w;
        w.str(why);
        c.conn.send(MSG_TOAST, w.b);
    }
}

void HostSession::pumpGame(Game& g) {
    if (!running()) return;
    acceptNew(true);
    for (size_t k = 0; k < clients_.size(); ++k) {
        Client& c = *clients_[k];
        c.conn.pump();
        uint8_t type;
        std::vector<uint8_t> payload;
        int budget = 64;  // orders per frame per client
        while (budget-- > 0 && c.conn.next(type, payload))
            if (type == MSG_ORDER) handleOrder(g, c, payload);
    }
    dropDead(&g);
}

static uint32_t hashBuildings(const Game& g) {
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < g.buildings.size(); ++k) {
        const Building& b = g.buildings[k];
        uint32_t v[4] = {(uint32_t)b.type, (uint32_t)b.level, (uint32_t)b.owner, (uint32_t)b.tile};
        for (int i = 0; i < 4; ++i) { h ^= v[i]; h *= 16777619u; }
    }
    return h ^ (uint32_t)g.buildings.size();
}

void HostSession::broadcast(Game& g, bool force) {
    if (!inGame_ || clients_.empty()) return;
    unsigned now = netNowMs();
    if (!force && now - lastSendMs_ < 150) return;
    lastSendMs_ = now;

    Writer w;
    w.i32(g.tick);
    w.u8((g.spawnPhase ? 1 : 0) | (g.over ? 2 : 0) | (namesDirty_ ? 4 : 0));
    w.u8(g.winnerPlayer);
    w.u8((unsigned)(g.winnerTeam + 1));

    // tiles that changed since the last message
    if (sentOwner_.size() != g.owner.size()) sentOwner_.assign(g.owner.size(), 255);
    size_t countPos = w.b.size();
    w.u32(0);
    uint32_t n = 0;
    const size_t N = g.owner.size();
    for (size_t t = 0; t < N; ++t) {
        if (sentOwner_[t] == g.owner[t]) continue;
        sentOwner_[t] = g.owner[t];
        w.u32((uint32_t)t);
        w.u8(g.owner[t]);
        ++n;
    }
    w.b[countPos] = n & 255;
    w.b[countPos + 1] = (n >> 8) & 255;
    w.b[countPos + 2] = (n >> 16) & 255;
    w.b[countPos + 3] = (n >> 24) & 255;

    w.u8((unsigned)g.players.size());
    for (size_t p = 0; p < g.players.size(); ++p) {
        const Player& pl = g.players[p];
        w.f32((float)pl.troops);
        w.f32((float)pl.gold);
        w.f32((float)pl.materials);
        w.i32(pl.tiles);
        w.u8((pl.alive ? 1 : 0) | (pl.spawned ? 2 : 0) | (pl.human ? 4 : 0));
        w.f32(pl.cx);
        w.f32(pl.cy);
        w.u16((unsigned)(pl.bx0 + 1));
        w.u16((unsigned)(pl.by0 + 1));
        w.u16((unsigned)(pl.bx1 + 1));
        w.u16((unsigned)(pl.by1 + 1));
        if (namesDirty_) w.str(pl.name);
    }
    namesDirty_ = false;

    uint32_t bh = hashBuildings(g);
    if (bh != sentBuildingsHash_) {
        sentBuildingsHash_ = bh;
        w.u8(1);
        w.u16((unsigned)g.buildings.size());
        for (size_t k = 0; k < g.buildings.size(); ++k) {
            const Building& b = g.buildings[k];
            w.u8(b.type);
            w.u8(b.level);
            w.u8(b.owner);
            w.u32((uint32_t)b.tile);
        }
    } else {
        w.u8(0);
    }

    unsigned nb = 0;
    for (size_t k = 0; k < g.boats.size(); ++k)
        if (g.boats[k].active && g.boats[k].path.size() >= 2) ++nb;
    w.u16(nb);
    for (size_t k = 0; k < g.boats.size(); ++k) {
        const Boat& b = g.boats[k];
        if (!b.active || b.path.size() < 2) continue;
        size_t i = std::min(b.path.size() - 1, (size_t)b.pos);
        size_t j = std::min(b.path.size() - 1, i + 1);
        w.u8(b.owner);
        w.f32((float)b.troops);
        w.u32((uint32_t)b.path[i]);
        w.u32((uint32_t)b.path[j]);
        w.f32(b.pos - (float)i);
    }

    unsigned na = 0;
    for (size_t k = 0; k < g.attacks.size(); ++k)
        if (g.attacks[k].active && (g.isHuman(g.attacks[k].attacker) || g.isHuman(g.attacks[k].target))) ++na;
    w.u16(na);
    for (size_t k = 0; k < g.attacks.size(); ++k) {
        const Attack& a = g.attacks[k];
        if (!a.active || !(g.isHuman(a.attacker) || g.isHuman(a.target))) continue;
        w.u8(a.attacker);
        w.u8(a.target);
        w.f32((float)a.troops);
        w.u32((uint32_t)a.aimTile);
    }

    // the events differ per player
    for (size_t k = 0; k < clients_.size(); ++k) {
        Client& c = *clients_[k];
        if (!c.conn.open() || c.playerId <= 0) continue;
        Writer m;
        m.b = w.b;
        size_t evPos = m.b.size();
        m.u8(0);
        unsigned ne = 0;
        for (size_t e = 0; e < g.events.size() && ne < 20; ++e) {
            const GameEvent& ev = g.events[e];
            if (ev.seq <= c.lastEventSeq) continue;
            if (ev.player != 0 && ev.player != c.playerId) continue;
            m.u32(ev.color);
            m.str(ev.text);
            ++ne;
        }
        m.b[evPos] = (uint8_t)ne;
        c.lastEventSeq = g.eventSeq;
        c.conn.send(MSG_DELTA, m.b);
        c.conn.pump();
    }
}

// ====================================================================== client

bool ClientSession::connect(const std::string& host, int port, std::string* err) {
    close();
    reason_.clear();
    toasts_.clear();
    if (!conn_.connectTo(host, port, err)) return false;
    state_ = CONNECTING;
    helloSent_ = false;
    slot_ = count_ = 0;
    info_.clear();
    return true;
}

void ClientSession::close() {
    conn_.close();
    state_ = IDLE;
}

void ClientSession::sendOrder(int kind, int a, int b, float f) {
    if (state_ != PLAYING) return;
    Writer w;
    w.u8(kind);
    w.i32(a);
    w.i32(b);
    w.f32(f);
    conn_.send(MSG_ORDER, w.b);
    conn_.pump();
}

bool ClientSession::nextToast(std::string& out) {
    if (toasts_.empty()) return false;
    out = toasts_.front();
    toasts_.erase(toasts_.begin());
    return true;
}

bool ClientSession::pump(Game& g, const std::string& mapsDir) {
    if (state_ == IDLE || state_ == CLOSED) return false;
    bool started = false;
    conn_.pump();
    if (conn_.ready() && !helloSent_) {
        Writer w;
        w.u32(kProtocolMagic);
        w.u16(kProtocolVersion);
        conn_.send(MSG_HELLO, w.b);
        conn_.pump();
        helloSent_ = true;
        if (state_ == CONNECTING) state_ = LOBBY;
    }
    uint8_t type;
    std::vector<uint8_t> payload;
    while (conn_.next(type, payload)) {
        Reader r(payload);
        switch (type) {
            case MSG_LOBBY:
                slot_ = r.u8();
                count_ = r.u8();
                info_ = r.str();
                break;
            case MSG_START: {
                int id = r.u8();
                std::string json = r.str();
                JVal j;
                std::string err;
                if (!r.ok || !JVal::parse(json, j, &err) || !g.fromJson(j, mapsDir, &err) || id <= 0 ||
                    id >= (int)g.players.size()) {
                    reason_ = "Kon het spel niet laden: " + err;
                    conn_.close();
                    state_ = CLOSED;
                    return false;
                }
                g.humanId = id;
                g.events.clear();
                g.attacks.clear();
                g.boats.clear();
                state_ = PLAYING;
                started = true;
                break;
            }
            case MSG_DELTA:
                if (state_ == PLAYING) applyDelta(g, payload);
                break;
            case MSG_TOAST:
                toasts_.push_back(r.str());
                break;
            case MSG_BYE:
                reason_ = r.str();
                conn_.close();
                state_ = CLOSED;
                return started;
            default:
                break;
        }
    }
    if (!conn_.open() && state_ != CLOSED) {
        reason_ = conn_.error().empty() ? "Verbinding verbroken" : conn_.error();
        state_ = CLOSED;
    }
    return started;
}

void ClientSession::applyDelta(Game& g, const std::vector<uint8_t>& payload) {
    Reader r(payload);
    const int N = g.map.w * g.map.h;
    g.tick = r.i32();
    unsigned flags = r.u8();
    g.spawnPhase = (flags & 1) != 0;
    g.over = (flags & 2) != 0;
    bool names = (flags & 4) != 0;
    g.winnerPlayer = (int)r.u8();
    g.winnerTeam = (int)r.u8() - 1;

    uint32_t n = r.u32();
    for (uint32_t k = 0; k < n && r.ok; ++k) {
        uint32_t t = r.u32();
        unsigned o = r.u8();
        if (!r.ok || t >= (uint32_t)N || o >= g.players.size()) continue;
        if (g.owner[t] != o) {
            g.owner[t] = (uint8_t)o;
            g.changed.push_back((int)t);
        }
    }

    unsigned np = r.u8();
    for (unsigned p = 0; p < np && r.ok; ++p) {
        Player tmp;
        Player& pl = p < g.players.size() ? g.players[p] : tmp;
        pl.troops = r.f32();
        pl.gold = r.f32();
        pl.materials = r.f32();
        pl.tiles = r.i32();
        unsigned f = r.u8();
        pl.alive = (f & 1) != 0;
        pl.spawned = (f & 2) != 0;
        pl.human = (f & 4) != 0;
        pl.cx = r.f32();
        pl.cy = r.f32();
        pl.bx0 = (int)r.u16() - 1;
        pl.by0 = (int)r.u16() - 1;
        pl.bx1 = (int)r.u16() - 1;
        pl.by1 = (int)r.u16() - 1;
        if (names) pl.name = r.str();
    }

    if (r.u8()) {
        for (size_t k = 0; k < g.buildings.size(); ++k)
            if (g.buildings[k].tile >= 0 && g.buildings[k].tile < N) g.hasBuilding[g.buildings[k].tile] = 0;
        g.buildings.clear();
        unsigned nb = r.u16();
        for (unsigned k = 0; k < nb && r.ok; ++k) {
            Building b;
            b.type = (int)r.u8();
            b.level = (int)r.u8();
            b.owner = (int)r.u8();
            b.tile = (int)r.u32();
            if (!r.ok || b.type >= B_COUNT || b.tile < 0 || b.tile >= N || b.owner >= (int)g.players.size()) continue;
            g.hasBuilding[b.tile] = 1;
            g.buildings.push_back(b);
        }
        g.recomputeBuildingLevels();
    }

    unsigned nboats = r.u16();
    g.boats.clear();
    for (unsigned k = 0; k < nboats && r.ok; ++k) {
        Boat b;
        b.active = true;
        b.owner = (int)r.u8();
        b.troops = r.f32();
        int a = (int)r.u32(), c = (int)r.u32();
        b.pos = r.f32();
        if (!r.ok || a < 0 || a >= N || c < 0 || c >= N || b.owner >= (int)g.players.size()) continue;
        b.path.push_back(a);
        b.path.push_back(c);
        g.boats.push_back(b);
    }

    unsigned na = r.u16();
    g.attacks.clear();
    for (unsigned k = 0; k < na && r.ok; ++k) {
        Attack a;
        a.active = true;
        a.attacker = (int)r.u8();
        a.target = (int)r.u8();
        a.troops = r.f32();
        a.aimTile = (int)r.u32();
        if (!r.ok || a.attacker >= (int)g.players.size() || a.target >= (int)g.players.size() || a.aimTile < 0 ||
            a.aimTile >= N)
            continue;
        g.attacks.push_back(a);
    }

    unsigned ne = r.u8();
    for (unsigned k = 0; k < ne && r.ok; ++k) {
        uint32_t color = r.u32();
        std::string text = r.str();
        if (r.ok) g.addEvent(text, color);
    }
}

}  // namespace fc
