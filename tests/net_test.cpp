// Multiplayer test without graphics: one host and two clients over localhost.
// Checks that orders arrive, that every client ends with exactly the host's
// map, and that a player who disconnects is taken over by the AI.
//   net_test <maps dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include "../src/core/Game.h"
#include "../src/net/Session.h"

using namespace fc;

static int failures = 0;
#define CHECK(c, msg)                                                 \
    do {                                                              \
        if (!(c)) {                                                   \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++failures;                                               \
        }                                                             \
    } while (0)

static void nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

static int freeTileFrom(const Game& g, int start) {
    for (int t = start; t < (int)g.owner.size(); ++t)
        if (g.canSpawnAt(t)) return t;
    return -1;
}

static void sendRandomOrders(ClientSession& c, const Game& g, int k) {
    const Player& me = g.players[g.humanId];
    if (!me.alive) return;
    if (k % 20 == 5) {
        for (int tries = 0; tries < 200; ++tries) {
            int x = (int)me.cx + rand() % 81 - 40, y = (int)me.cy + rand() % 81 - 40;
            if (x < 0 || y < 0 || x >= g.map.w || y >= g.map.h) continue;
            int t = y * g.map.w + x;
            if (g.map.isLand(t) && g.owner[t] != g.humanId) {
                c.sendOrder(ORD_ATTACK, t, 0, 0.5f);
                break;
            }
        }
    }
    if (k % 90 == 40) {
        for (int tries = 0; tries < 200; ++tries) {
            int t = rand() % (int)g.owner.size();
            if (g.owner[t] == g.humanId) {
                c.sendOrder(ORD_BUILD, rand() % B_COUNT, t, 0);
                break;
            }
        }
    }
    if (k % 300 == 150) c.sendOrder(ORD_RECRUIT, 0, 0, 0);
    if (k % 200 == 120)
        for (size_t b = 0; b < g.buildings.size(); ++b)
            if (g.buildings[b].owner == g.humanId) {
                c.sendOrder(ORD_UPGRADE, g.buildings[b].tile, 0, 0);
                break;
            }
}

int main(int argc, char** argv) {
    std::string mapsDir = argc > 1 ? argv[1] : "Resources/maps";
    const int port = 47991;
    std::string err;
    srand(11);

    // wrong address: must fail cleanly, not hang
    {
        ClientSession lost;
        Game dummy;
        CHECK(lost.connect("127.0.0.1", 47990, "Test", &err), "connect call");
        for (int k = 0; k < 3000 && lost.state() != ClientSession::CLOSED; ++k) { lost.pump(dummy, mapsDir); nap(); }
        CHECK(lost.state() == ClientSession::CLOSED, "connecting to a closed port reports an error");
        printf("  closed port -> \"%s\"\n", lost.closeReason().c_str());
    }

    // reading the public IP out of a web answer
    CHECK(PublicIp::parse("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n84.25.130.7\n") == "84.25.130.7", "public ip: plain");
    CHECK(PublicIp::parse("HTTP/1.0 200 OK\r\nX: 1.2.3.4\r\n\r\n  203.0.113.255") == "203.0.113.255", "public ip: body only");
    CHECK(PublicIp::parse("HTTP/1.1 301 Moved\r\n\r\n1.2.3.4") == "", "public ip: only on status 200");
    CHECK(PublicIp::parse("HTTP/1.1 200 OK\r\n\r\n<html>error 999.1.1.1</html>") == "", "public ip: rejects nonsense");
    CHECK(PublicIp::parse("HTTP/1.1 200 OK\r\n\r\n1.2.3") == "" && PublicIp::parse("garbage") == "", "public ip: incomplete");
    CHECK(cleanPlayerName("") == "Speler" && cleanPlayerName("  Yf er  ") == "Yf er", "name cleaning");

    HostSession host;
    CHECK(host.start(port, &err), err.c_str());
    ClientSession c1, c2;
    Game g, g1, g2;
    CHECK(c1.connect("127.0.0.1", port, "Anna", &err), err.c_str());
    CHECK(c2.connect("127.0.0.1", port, "  <Anna!>  ", &err), err.c_str());
    for (int k = 0; k < 2000; ++k) {
        host.pumpLobby("test lobby", "Yfer");
        c1.pump(g1, mapsDir);
        c2.pump(g2, mapsDir);
        if (host.clientCount() == 2 && c1.lobbyCount() == 3 && c2.lobbyCount() == 3) break;
        nap();
    }
    CHECK(host.clientCount() == 2, "two clients in the lobby");
    CHECK(c1.lobbyCount() == 3 && c2.lobbyCount() == 3 && c1.lobbyInfo() == "test lobby", "clients see the lobby");
    CHECK(c1.lobbySlot() != c2.lobbySlot(), "different slots");
    for (int k = 0; k < 600 && c1.lobbyNames().size() != 3; ++k) { host.pumpLobby("test lobby", "Yfer"); c1.pump(g1, mapsDir); nap(); }
    CHECK(c1.lobbyNames().size() == 3 && c1.lobbyNames()[0] == "Yfer", "lobby shows the host's name");
    if (c1.lobbyNames().size() == 3)
        printf("  lobby names: %s | %s | %s\n", c1.lobbyNames()[0].c_str(), c1.lobbyNames()[1].c_str(), c1.lobbyNames()[2].c_str());

    Settings s;
    s.mapId = "europe";
    s.numAI = 30;
    s.difficulty = DIFF_HARD;
    s.numHumans = 3;
    s.seed = 4242;
    CHECK(g.newGame(s, mapsDir, &err), err.c_str());
    host.startGame(g, "Yfer");
    bool s1 = false, s2 = false;
    for (int k = 0; k < 2000 && !(s1 && s2); ++k) {
        host.pumpGame(g);
        if (c1.pump(g1, mapsDir)) s1 = true;
        if (c2.pump(g2, mapsDir)) s2 = true;
        nap();
    }
    CHECK(s1 && s2, "both clients received the game");
    if (!(s1 && s2)) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    CHECK(g1.humanId != g2.humanId && g1.humanId >= 2 && g2.humanId >= 2, "clients got their own player");
    CHECK(g1.owner == g.owner && g2.owner == g.owner, "start state is identical");
    CHECK(g.spawnPhase, "spawn phase waits for the players");
    CHECK(g1.players[1].name == "Yfer" && g1.players[g1.humanId].name.compare(0, 4, "Anna") == 0, "names are used in the game");
    CHECK(g.players[2].name != g.players[3].name, "equal names are made different");
    printf("  in-game names: %s | %s | %s\n", g.players[1].name.c_str(), g.players[2].name.c_str(), g.players[3].name.c_str());
    CHECK(g.players[g2.humanId].name.find('<') == std::string::npos && g.players[g2.humanId].name.size() <= kMaxNameLength,
          "names are cleaned up");

    // everybody picks a start; the game begins only when all three have one
    CHECK(g.spawnHuman(freeTileFrom(g, (int)g.owner.size() / 3)), "host spawns");
    c1.sendOrder(ORD_SPAWN, freeTileFrom(g1, (int)g1.owner.size() / 2), 0, 0);
    for (int k = 0; k < 500 && !g.players[g1.humanId].spawned; ++k) { host.pumpGame(g); nap(); }
    CHECK(g.players[g1.humanId].spawned && g.spawnPhase, "one client spawned, still waiting for the other");
    host.broadcast(g, true);
    for (int k = 0; k < 200 && !g2.players[g1.humanId].spawned; ++k) { c2.pump(g2, mapsDir); nap(); }
    c2.sendOrder(ORD_SPAWN, freeTileFrom(g2, (int)g2.owner.size() * 2 / 3), 0, 0);
    for (int k = 0; k < 500 && g.spawnPhase; ++k) { host.pumpGame(g); nap(); }
    CHECK(!g.spawnPhase, "game starts when everyone has spawned");

    const int ticks = 1500;
    size_t bytesHint = 0;
    for (int k = 0; k < ticks; ++k) {
        host.pumpGame(g);
        if (k % 25 == 0 && g.players[1].alive) g.orderAttack(1, rand() % (int)g.owner.size(), g.players[1].troops * 0.4, 0);
        g.step();
        if (k % 2 == 0) host.broadcast(g, true);
        c1.pump(g1, mapsDir);
        c2.pump(g2, mapsDir);
        sendRandomOrders(c1, g1, k);
        sendRandomOrders(c2, g2, k + 7);
        g1.changed.clear();
        g2.changed.clear();
        if (k % 400 == 399 && !g.checkConsistency(&err)) { printf("  FAIL consistency: %s\n", err.c_str()); ++failures; break; }
        (void)bytesHint;
    }
    // let the last messages arrive
    for (int k = 0; k < 300; ++k) {
        host.pumpGame(g);
        if (k < 3) host.broadcast(g, true);
        c1.pump(g1, mapsDir);
        c2.pump(g2, mapsDir);
        nap();
        if (k > 20 && g1.tick == g.tick && g2.tick == g.tick && g1.owner == g.owner && g2.owner == g.owner) break;
    }
    CHECK(g1.tick == g.tick && g2.tick == g.tick, "clients are at the host's tick");
    size_t d1 = 0, d2 = 0;
    for (size_t t = 0; t < g.owner.size(); ++t) {
        d1 += g1.owner[t] != g.owner[t];
        d2 += g2.owner[t] != g.owner[t];
    }
    CHECK(d1 == 0 && d2 == 0, "every client has exactly the host's map");
    bool same = g1.players.size() == g.players.size();
    for (size_t p = 0; same && p < g.players.size(); ++p) {
        const Player& a = g.players[p];
        const Player& b = g1.players[p];
        if (a.tiles != b.tiles || a.alive != b.alive || std::fabs(a.troops - b.troops) > 1.0 + a.troops * 1e-5 ||
            std::fabs(a.gold - b.gold) > 1.0 + a.gold * 1e-5)
            same = false;
    }
    CHECK(same, "player numbers match");
    CHECK(g1.buildings.size() == g.buildings.size() && g2.buildings.size() == g.buildings.size(), "buildings match");
    int built1 = 0;
    for (size_t b = 0; b < g.buildings.size(); ++b) built1 += g.buildings[b].owner == g1.humanId;
    printf("  after %d ticks: host tiles %d, client1 tiles %d (buildings %d), client2 tiles %d, alive %d, map diff %d/%d\n", ticks,
           g.players[1].tiles, g.players[g1.humanId].tiles, built1, g.players[g2.humanId].tiles, g.aliveCount(), (int)d1, (int)d2);
    CHECK(g.players[g1.humanId].tiles > 60 || !g.players[g1.humanId].alive, "client orders had effect (territory grew)");

    // a client that leaves is taken over by the AI; the game goes on
    int leaver = g2.humanId;
    c2.close();
    for (int k = 0; k < 500 && g.isHuman(leaver); ++k) { host.pumpGame(g); nap(); }
    CHECK(!g.isHuman(leaver), "a player who left becomes an AI");
    CHECK(host.clientCount() == 1, "host has one client left");
    for (int k = 0; k < 100; ++k) { g.step(); host.broadcast(g, true); c1.pump(g1, mapsDir); }
    for (int k = 0; k < 100 && g1.tick != g.tick; ++k) { c1.pump(g1, mapsDir); nap(); }
    CHECK(c1.state() == ClientSession::PLAYING && g1.tick == g.tick, "remaining client keeps playing");
    CHECK(g1.players[leaver].name.find("(AI)") != std::string::npos, "clients see the name change");

    // host stops: client is told
    host.stop();
    for (int k = 0; k < 500 && c1.state() != ClientSession::CLOSED; ++k) { c1.pump(g1, mapsDir); nap(); }
    CHECK(c1.state() == ClientSession::CLOSED, "client notices that the host stopped");
    printf("  host stopped -> \"%s\"\n", c1.closeReason().c_str());

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL NETWORK TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
