// Headless tests for the simulation: every map, every mode, 50 AIs.
//   sim_test <maps dir> [ticks]
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/core/Game.h"

using namespace fc;

static int failures = 0;
#define CHECK(c, msg)                                                   \
    do {                                                                \
        if (!(c)) {                                                     \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);   \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void testJson() {
    JVal v;
    std::string err;
    CHECK(JVal::parse("{\"a\":[1,2.5,-3e2],\"b\":\"x\\n\\u0041\",\"c\":true,\"d\":null}", v, &err), err.c_str());
    CHECK(v["a"].size() == 3 && v["a"][2].num() == -300, "array parse");
    CHECK(v["b"].str() == "x\nA", "string escapes");
    CHECK(v["c"].boolean() && v["d"].isNull(), "literals");
    JVal back;
    CHECK(JVal::parse(v.dump(), back) && back.dump() == v.dump(), "roundtrip");
    CHECK(!JVal::parse("{\"a\":", back), "reject truncated");
    CHECK(!JVal::parse("[1,2]x", back), "reject trailing");
}

static bool sameState(const Game& a, const Game& b) {
    if (a.owner != b.owner) { printf("  owners differ\n"); return false; }
    if (a.players.size() != b.players.size()) return false;
    for (size_t p = 0; p < a.players.size(); ++p) {
        const Player& x = a.players[p];
        const Player& y = b.players[p];
        if (x.name != y.name || x.alive != y.alive || x.tiles != y.tiles || std::abs(x.gold - y.gold) > 0.01 ||
            std::abs(x.troops - y.troops) > 0.01 || x.team != y.team) {
            printf("  player %d differs\n", (int)p);
            return false;
        }
    }
    if (a.buildings.size() != b.buildings.size()) { printf("  buildings differ\n"); return false; }
    return a.tick == b.tick && a.rng == b.rng;
}

static void runGame(const std::string& mapsDir, const std::string& mapId, int mode, int ais, int ticks) {
    Settings s;
    s.mapId = mapId;
    s.mode = mode;
    s.numAI = ais;
    s.difficulty = DIFF_HARD;
    s.numTeams = 3;
    s.customTeams = (mode == MODE_CUSTOM);
    s.seed = 777 + mode;
    Game g;
    std::string err;
    if (!g.newGame(s, mapsDir, &err)) {
        printf("  FAIL newGame %s: %s\n", mapId.c_str(), err.c_str());
        ++failures;
        return;
    }
    // spawn the human on the first free land tile near a random nation
    int spawn = -1;
    for (int t = (int)g.owner.size() / 3; t < (int)g.owner.size() && spawn < 0; ++t)
        if (g.canSpawnAt(t)) spawn = t;
    CHECK(spawn >= 0 && g.spawnHuman(spawn), "spawn human");
    CHECK(g.checkConsistency(&err), err.c_str());

    auto t0 = std::chrono::steady_clock::now();
    int maxBoats = 0, buildingsSeen = 0;
    for (int k = 0; k < ticks; ++k) {
        // "human" plays a bit too: attack a random tile now and then, build sometimes
        if (k % 25 == 0 && g.players[g.humanId].alive) {
            int t = rand() % (int)g.owner.size();
            g.orderAttack(g.humanId, t, g.players[g.humanId].troops * 0.4, 0);
        }
        if (k % 97 == 0 && g.players[g.humanId].alive) {
            for (int tries = 0; tries < 50; ++tries) {
                int t = rand() % (int)g.owner.size();
                if (g.owner[t] == g.humanId && g.build(g.humanId, rand() % B_COUNT, t, 0)) break;
            }
        }
        g.step();
        g.changed.clear();
        int nb = 0;
        for (size_t b = 0; b < g.boats.size(); ++b) nb += g.boats[b].active;
        if (nb > maxBoats) maxBoats = nb;
        if ((int)g.buildings.size() > buildingsSeen) buildingsSeen = (int)g.buildings.size();
        if (k % 200 == 199 && !g.checkConsistency(&err)) {
            printf("  FAIL consistency at tick %d: %s\n", g.tick, err.c_str());
            ++failures;
            return;
        }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    // save -> load -> compare
    std::string path = "test_save_" + mapId + ".json";
    CHECK(g.saveToFile(path), "save");
    Game g2;
    CHECK(g2.loadFromFile(path, mapsDir, &err), err.c_str());
    CHECK(sameState(g, g2), "save/load roundtrip");
    CHECK(g2.checkConsistency(&err), err.c_str());
    std::string sum;
    CHECK(Game::readSummary(path, sum) && !sum.empty(), "summary");
    remove(path.c_str());
    // the loaded game keeps running
    for (int k = 0; k < 100; ++k) g2.step();
    CHECK(g2.checkConsistency(&err), err.c_str());

    int owned = 0;
    for (size_t p = 1; p < g.players.size(); ++p) owned += g.players[p].tiles;
    printf("  %-13s %-12s ais=%d ticks=%d  %.2f ms/tick  alive=%d owned=%d%% buildings=%d boats(max)=%d over=%d %s\n",
           mapId.c_str(), modeName(mode), ais, ticks, ms / ticks, g.aliveCount(),
           owned * 100 / std::max(1, g.map.landTiles), buildingsSeen, maxBoats, (int)g.over, sum.c_str());
}

int main(int argc, char** argv) {
    std::string mapsDir = argc > 1 ? argv[1] : "Resources/maps";
    int ticks = argc > 2 ? atoi(argv[2]) : 3000;
    testJson();
    std::vector<MapEntry> maps;
    CHECK(loadMapIndex(mapsDir, maps) && maps.size() == 6, "map index has 6 maps");
    for (size_t m = 0; m < maps.size(); ++m)
        for (int mode = 0; mode < MODE_COUNT; ++mode)
            runGame(mapsDir, maps[m].id, mode, mode == MODE_SANDBOX ? 10 : 50, mode == 0 ? ticks : ticks / 3);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
