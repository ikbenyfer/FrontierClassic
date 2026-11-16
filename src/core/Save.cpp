// Saving and loading a game as a local JSON file.
#include "Game.h"
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace fc {

static const int kSaveVersion = 1;

// Tile owners as run-length text: "owner*count,owner*count,..."
static std::string encodeOwners(const std::vector<uint8_t>& o) {
    std::string out;
    out.reserve(o.size() / 8);
    char buf[24];
    size_t k = 0;
    while (k < o.size()) {
        size_t r = k;
        while (r < o.size() && o[r] == o[k]) ++r;
        snprintf(buf, sizeof buf, "%d*%d,", (int)o[k], (int)(r - k));
        out += buf;
        k = r;
    }
    return out;
}

static bool decodeOwners(const std::string& s, std::vector<uint8_t>& o, int maxOwner) {
    size_t pos = 0, filled = 0;
    const char* p = s.c_str();
    const char* end = p + s.size();
    while (p < end) {
        char* e;
        long v = strtol(p, &e, 10);
        if (e == p || *e != '*') return false;
        p = e + 1;
        long n = strtol(p, &e, 10);
        if (e == p || *e != ',') return false;
        p = e + 1;
        if (v < 0 || v > maxOwner || n <= 0 || filled + (size_t)n > o.size()) return false;
        for (long k = 0; k < n; ++k) o[pos++] = (uint8_t)v;
        filled += (size_t)n;
    }
    return filled == o.size();
}

JVal Game::toJson() const {
    JVal j = JVal::object();
    j.set("game", "FrontierClassic");
    j.set("version", kSaveVersion);
    char sum[160];
    int secs = tick / TICKS_PER_SECOND;
    snprintf(sum, sizeof sum, "%s - %s - %d:%02d - %d over", map.name.c_str(), modeName(settings.mode), secs / 60,
             secs % 60, aliveCount());
    j.set("summary", std::string(sum));
    j.set("settings", settings.toJson());
    j.set("tick", tick);
    j.set("rng", (double)rng);
    j.set("humanId", humanId);
    j.set("spawnPhase", spawnPhase);
    j.set("over", over);
    j.set("winnerPlayer", winnerPlayer);
    j.set("winnerTeam", winnerTeam);

    JVal ps = JVal::array();
    for (size_t p = 0; p < players.size(); ++p) {
        const Player& pl = players[p];
        JVal o = JVal::object();
        o.set("name", pl.name);
        o.set("color", (double)pl.color);
        o.set("human", pl.human);
        o.set("alive", pl.alive);
        o.set("spawned", pl.spawned);
        o.set("team", pl.team);
        o.set("troops", pl.troops);
        o.set("gold", pl.gold);
        o.set("materials", pl.materials);
        o.set("lastAttackedBy", pl.lastAttackedBy);
        o.set("lastAttackedTick", pl.lastAttackedTick);
        o.set("nextThink", pl.nextThink);
        o.set("cx", (double)pl.cx);
        o.set("cy", (double)pl.cy);
        ps.push(o);
    }
    j.set("players", ps);

    JVal bs = JVal::array();
    for (size_t k = 0; k < buildings.size(); ++k) {
        JVal b = JVal::array();
        b.push(buildings[k].type);
        b.push(buildings[k].tile);
        b.push(buildings[k].owner);
        b.push(buildings[k].level);
        bs.push(b);
    }
    j.set("buildings", bs);

    JVal as = JVal::array();
    for (size_t k = 0; k < attacks.size(); ++k) {
        const Attack& a = attacks[k];
        if (!a.active) continue;
        JVal o = JVal::array();
        o.push(a.attacker);
        o.push(a.target);
        o.push(a.troops);
        o.push(a.aimTile);
        as.push(o);
    }
    j.set("attacks", as);

    JVal bt = JVal::array();
    for (size_t k = 0; k < boats.size(); ++k) {
        const Boat& b = boats[k];
        if (!b.active) continue;
        JVal o = JVal::object();
        o.set("owner", b.owner);
        o.set("troops", b.troops);
        o.set("target", b.target);
        o.set("pos", (double)b.pos);
        JVal path = JVal::array();
        for (size_t i = 0; i < b.path.size(); ++i) path.push(b.path[i]);
        o.set("path", path);
        bt.push(o);
    }
    j.set("boats", bt);

    JVal ev = JVal::array();
    size_t first = events.size() > 8 ? events.size() - 8 : 0;
    for (size_t k = first; k < events.size(); ++k) {
        JVal o = JVal::array();
        o.push(events[k].text);
        o.push(events[k].tick);
        o.push((double)events[k].color);
        ev.push(o);
    }
    j.set("events", ev);
    j.set("owners", encodeOwners(owner));
    return j;
}

bool Game::fromJson(const JVal& j, const std::string& mapsDir, std::string* err) {
    if (j["game"].str("") != "FrontierClassic" || j["version"].i() != kSaveVersion) {
        if (err) *err = "Onbekend of te nieuw opslagbestand";
        return false;
    }
    Settings s;
    s.fromJson(j["settings"]);
    if (!map.load(mapsDir, s.mapId, err)) return false;
    settings = s;
    const int N = map.w * map.h;
    const JVal& ps = j["players"];
    if (ps.size() < 2 || ps.size() > (size_t)MAX_PLAYERS) {
        if (err) *err = "Ongeldig aantal spelers";
        return false;
    }
    players.assign(ps.size(), Player());
    for (size_t p = 0; p < ps.size(); ++p) {
        const JVal& o = ps[p];
        Player& pl = players[p];
        pl.name = o["name"].str("?");
        pl.color = (uint32_t)o["color"].num(0xffffff);
        pl.human = o["human"].boolean();
        pl.alive = o["alive"].boolean();
        pl.spawned = o["spawned"].boolean();
        pl.team = o["team"].i();
        pl.troops = o["troops"].num();
        pl.gold = o["gold"].num();
        pl.materials = o["materials"].num();
        pl.lastAttackedBy = o["lastAttackedBy"].i(-1);
        pl.lastAttackedTick = o["lastAttackedTick"].i(-100000);
        pl.nextThink = o["nextThink"].i();
        pl.cx = (float)o["cx"].num();
        pl.cy = (float)o["cy"].num();
    }
    owner.assign(N, 0);
    if (!decodeOwners(j["owners"].str(""), owner, (int)players.size() - 1)) {
        if (err) *err = "Gebiedsgegevens zijn beschadigd";
        return false;
    }
    for (int t = 0; t < N; ++t)
        if (owner[t] && !map.isLand(t)) owner[t] = 0;
    hasBuilding.assign(N, 0);
    stamp.assign(N, 0);
    parent.assign(N, -1);
    stampId = 0;
    adjacency.assign(MAX_PLAYERS * MAX_PLAYERS, 0);
    buildings.clear();
    const JVal& bs = j["buildings"];
    for (size_t k = 0; k < bs.size(); ++k) {
        Building b;
        b.type = bs[k][0].i();
        b.tile = bs[k][1].i();
        b.owner = bs[k][2].i();
        b.level = bs[k][3].i(1);
        if (b.type < 0 || b.type >= B_COUNT || b.tile < 0 || b.tile >= N || owner[b.tile] != b.owner ||
            b.owner <= 0 || hasBuilding[b.tile] || b.level < 1 || b.level > MAX_LEVEL)
            continue;
        hasBuilding[b.tile] = 1;
        buildings.push_back(b);
    }
    tick = j["tick"].i();
    rng = (uint32_t)j["rng"].num(1);
    humanId = j["humanId"].i(1);
    if (humanId <= 0 || humanId >= (int)players.size()) humanId = 1;
    spawnPhase = j["spawnPhase"].boolean();
    over = j["over"].boolean();
    winnerPlayer = j["winnerPlayer"].i();
    winnerTeam = j["winnerTeam"].i(-1);
    events.clear();
    changed.clear();
    recomputeTerritory();
    recomputeBuildingLevels();

    attacks.clear();
    const JVal& as = j["attacks"];
    for (size_t k = 0; k < as.size(); ++k) {
        int a = as[k][0].i(), t = as[k][1].i();
        if (a <= 0 || a >= (int)players.size() || t < 0 || t >= (int)players.size() || !players[a].alive) continue;
        // restored exactly; the frontier is rebuilt from the map
        Attack at;
        at.active = true;
        at.attacker = a;
        at.target = t;
        at.troops = as[k][2].num();
        at.aimTile = std::max(0, std::min(N - 1, as[k][3].i()));
        buildFrontier(at);  // may be empty: stepAttack then retries or sends the troops home
        attacks.push_back(at);
    }
    boats.clear();
    const JVal& bt = j["boats"];
    for (size_t k = 0; k < bt.size(); ++k) {
        Boat b;
        b.active = true;
        b.owner = bt[k]["owner"].i();
        b.troops = bt[k]["troops"].num();
        b.target = bt[k]["target"].i();
        b.pos = (float)bt[k]["pos"].num();
        const JVal& path = bt[k]["path"];
        bool ok = b.owner > 0 && b.owner < (int)players.size() && b.target >= 0 && b.target < N;
        for (size_t i = 0; i < path.size() && ok; ++i) {
            int t = path[i].i();
            if (t < 0 || t >= N) ok = false;
            b.path.push_back(t);
        }
        if (ok && b.path.size() >= 2) boats.push_back(b);
    }
    const JVal& ev = j["events"];
    for (size_t k = 0; k < ev.size(); ++k) {
        GameEvent e;
        e.text = ev[k][0].str("");
        e.tick = ev[k][1].i();
        e.color = (uint32_t)ev[k][2].num(0xffffff);
        events.push_back(e);
    }
    return true;
}

bool Game::saveToFile(const std::string& path) const {
    return writeFileAtomic(path, toJson().dump());
}

bool Game::loadFromFile(const std::string& path, const std::string& mapsDir, std::string* err) {
    std::string text;
    if (!readFile(path, text)) {
        if (err) *err = "Geen opgeslagen spel gevonden";
        return false;
    }
    JVal j;
    if (!JVal::parse(text, j, err)) return false;
    return fromJson(j, mapsDir, err);
}

bool Game::readSummary(const std::string& path, std::string& summary) {
    // The summary is near the start of the file; avoid parsing the whole save.
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[512];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char* k = strstr(buf, "\"summary\":\"");
    if (!k) return false;
    k += 11;
    const char* e = strchr(k, '"');
    if (!e) return false;
    summary.assign(k, e);
    return true;
}

}  // namespace fc
