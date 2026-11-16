#include "Game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fc {

// ------------------------------------------------------------------ names

const char* modeName(int mode) {
    switch (mode) {
        case MODE_FFA: return "Free For All";
        case MODE_TEAMS: return "Teams";
        case MODE_CONQUEST: return "Conquest";
        case MODE_SANDBOX: return "Sandbox";
        case MODE_CUSTOM: return "Custom Game";
    }
    return "?";
}

const char* difficultyName(int d) {
    switch (d) {
        case DIFF_EASY: return "Makkelijk";
        case DIFF_NORMAL: return "Normaal";
        case DIFF_HARD: return "Moeilijk";
    }
    return "?";
}

const char* buildingName(int type) {
    switch (type) {
        case B_CITY: return "Stad";
        case B_MARKET: return "Markt";
        case B_MINE: return "Mijn";
        case B_FORT: return "Fort";
    }
    return "?";
}

std::string formatNumber(double v) {
    char buf[32];
    double a = std::fabs(v);
    if (a >= 1e6) snprintf(buf, sizeof buf, "%.1fM", v / 1e6);
    else if (a >= 1e4) snprintf(buf, sizeof buf, "%.0fK", v / 1e3);
    else if (a >= 1e3) snprintf(buf, sizeof buf, "%.1fK", v / 1e3);
    else snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

// ------------------------------------------------------------------ map

bool MapData::load(const std::string& dir, const std::string& mapId, std::string* err) {
    std::string text;
    if (!readFile(dir + "/" + mapId + ".json", text)) {
        if (err) *err = "Kaart niet gevonden: " + mapId;
        return false;
    }
    JVal j;
    if (!JVal::parse(text, j, err)) return false;
    id = mapId;
    name = j["name"].str(mapId);
    w = j["width"].i();
    h = j["height"].i();
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        if (err) *err = "Ongeldige kaartgrootte";
        return false;
    }
    std::string bin;
    if (!readFile(dir + "/" + j["file"].str(mapId + ".bin"), bin) || bin.size() != (size_t)w * h) {
        if (err) *err = "Kaartbestand ontbreekt of is beschadigd";
        return false;
    }
    tiles.assign(bin.begin(), bin.end());
    landTiles = 0;
    for (size_t k = 0; k < tiles.size(); ++k)
        if (isLand((int)k)) ++landTiles;
    nations.clear();
    const JVal& ns = j["nations"];
    for (size_t k = 0; k < ns.size(); ++k) {
        Nation n;
        n.name = ns[k]["name"].str("Natie");
        n.x = ns[k]["x"].i();
        n.y = ns[k]["y"].i();
        if (n.x >= 0 && n.y >= 0 && n.x < w && n.y < h) nations.push_back(n);
    }
    return true;
}

bool loadMapIndex(const std::string& dir, std::vector<MapEntry>& out) {
    std::string text;
    if (!readFile(dir + "/index.json", text)) return false;
    JVal j;
    if (!JVal::parse(text, j)) return false;
    out.clear();
    const JVal& ms = j["maps"];
    for (size_t k = 0; k < ms.size(); ++k) {
        MapEntry e;
        e.id = ms[k]["id"].str("");
        e.name = ms[k]["name"].str(e.id);
        if (!e.id.empty()) out.push_back(e);
    }
    return !out.empty();
}

// ------------------------------------------------------------------ settings

JVal Settings::toJson() const {
    JVal j = JVal::object();
    j.set("map", mapId);
    j.set("mode", mode);
    j.set("numAI", numAI);
    j.set("difficulty", difficulty);
    j.set("numTeams", numTeams);
    j.set("customTeams", customTeams);
    j.set("startGold", startGold);
    j.set("winPercent", winPercent);
    j.set("troopSpeed", troopSpeed);
    j.set("aiAttacks", aiAttacks);
    j.set("seed", (double)seed);
    return j;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Settings::fromJson(const JVal& j) {
    mapId = j["map"].str("world");
    mode = clampi(j["mode"].i(MODE_FFA), 0, MODE_COUNT - 1);
    numAI = clampi(j["numAI"].i(20), 1, 50);
    difficulty = clampi(j["difficulty"].i(DIFF_NORMAL), 0, 2);
    numTeams = clampi(j["numTeams"].i(2), 2, 4);
    customTeams = j["customTeams"].boolean(false);
    startGold = clampi(j["startGold"].i(1000), 0, 1000000);
    winPercent = clampi(j["winPercent"].i(80), 10, 100);
    troopSpeed = clampi(j["troopSpeed"].i(100), 25, 400);
    aiAttacks = j["aiAttacks"].boolean(true);
    seed = (unsigned)j["seed"].num(12345);
}

// ------------------------------------------------------------------ random

uint32_t Game::rand32() {
    uint32_t x = rng ? rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}
double Game::rand01() { return (rand32() >> 8) / 16777216.0; }
int Game::randInt(int n) { return n <= 1 ? 0 : (int)(rand32() % (uint32_t)n); }

// ------------------------------------------------------------------ setup

static const uint32_t kPalette[] = {
    0xE74C3C, 0x2ECC71, 0xF1C40F, 0x9B59B6, 0xE67E22, 0x1ABC9C, 0xEC407A, 0x8D6E63,
    0x7CB342, 0xFF7043, 0x26C6DA, 0xAB47BC, 0xD4E157, 0x5C6BC0, 0xFFA726, 0x66BB6A,
    0xEF5350, 0x29B6F6, 0xFFCA28, 0x8E24AA, 0x43A047, 0xF06292, 0x00897B, 0xC0CA33,
    0x6D4C41, 0xFF8A65, 0x4DB6AC, 0xBA68C8, 0x9CCC65, 0xFFB74D, 0x4FC3F7, 0xE57373,
};
static const uint32_t kTeamColors[4] = {0xD64541, 0x3A7BD5, 0x2ECC71, 0xF1C40F};
static const uint32_t kHumanColor = 0x2E86FF;

static uint32_t shade(uint32_t c, double f) {
    int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)((c & 255) * f);
    r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

void Game::setupPlayers() {
    int n = settings.numAI;
    players.assign(n + 2, Player());
    players[0].name = "Niemand";
    players[0].alive = false;
    players[0].color = 0x888888;
    humanId = 1;
    Player& me = players[1];
    me.name = "Jij";
    me.human = true;
    me.color = kHumanColor;

    // Nation names: shuffle the largest nations a bit so games differ.
    std::vector<int> order;
    int pool = std::min((int)map.nations.size(), std::max(n + n / 2, n));
    for (int k = 0; k < pool; ++k) order.push_back(k);
    for (int k = pool - 1; k > 0; --k) std::swap(order[k], order[randInt(k + 1)]);
    for (int p = 2; p < (int)players.size(); ++p) {
        Player& pl = players[p];
        int k = p - 2;
        if (k < (int)order.size()) {
            pl.name = map.nations[order[k]].name;
        } else {
            char buf[32];
            snprintf(buf, sizeof buf, "Vrijstaat %d", k + 1 - (int)order.size());
            pl.name = buf;
        }
        pl.color = kPalette[k % (sizeof kPalette / sizeof kPalette[0])];
        if (k >= 32) pl.color = shade(pl.color, 0.75);
        pl.nextThink = randInt(30);
    }

    if (settings.teamsEnabled()) {
        int t = settings.numTeams;
        for (int p = 1; p < (int)players.size(); ++p) {
            players[p].team = (p - 1) % t;
            double f = 0.75 + 0.5 * ((p * 37) % 11) / 10.0;
            players[p].color = p == humanId ? kTeamColors[players[p].team] : shade(kTeamColors[players[p].team], f);
        }
    } else {
        for (int p = 1; p < (int)players.size(); ++p) players[p].team = p;  // everyone alone
    }

    for (int p = 1; p < (int)players.size(); ++p) {
        players[p].gold = settings.startGold;
        players[p].materials = 50;
    }
    if (settings.mode == MODE_SANDBOX) {
        me.gold = 1000000;
        me.materials = 100000;
    }
}

bool Game::newGame(const Settings& s, const std::string& mapsDir, std::string* err) {
    settings = s;
    settings.numAI = clampi(settings.numAI, 1, 50);
    if (!map.load(mapsDir, settings.mapId, err)) return false;
    rng = settings.seed ? settings.seed : 1;
    for (int k = 0; k < 8; ++k) rand32();
    tick = 0;
    spawnPhase = true;
    over = false;
    winnerPlayer = 0;
    winnerTeam = -1;
    size_t n = (size_t)map.w * map.h;
    owner.assign(n, 0);
    hasBuilding.assign(n, 0);
    stamp.assign(n, 0);
    parent.assign(n, -1);
    stampId = 0;
    adjacency.assign(MAX_PLAYERS * MAX_PLAYERS, 0);
    buildings.clear();
    attacks.clear();
    boats.clear();
    events.clear();
    changed.clear();
    setupPlayers();

    // Spawn the AI nations at their own country (or a random free spot).
    for (int p = 2; p < (int)players.size(); ++p) {
        int sx = -1, sy = -1;
        for (size_t k = 0; k < map.nations.size(); ++k) {
            const Nation& nat = map.nations[k];
            if (nat.name != players[p].name) continue;
            int t = nat.y * map.w + nat.x;
            if (canSpawnAt(t)) { sx = nat.x; sy = nat.y; }
            break;
        }
        for (int tries = 0; sx < 0 && tries < 400; ++tries) {
            int t = randInt(map.w * map.h);
            if (!canSpawnAt(t)) continue;
            // keep a little distance from other nations
            int x = t % map.w, y = t / map.w;
            bool ok = true;
            for (int dy = -6; dy <= 6 && ok; dy += 3)
                for (int dx = -6; dx <= 6 && ok; dx += 3) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < map.w && yy < map.h && owner[yy * map.w + xx]) ok = false;
                }
            if (ok || tries > 300) { sx = x; sy = y; }
        }
        if (sx >= 0) spawnAt(p, sx, sy, 3);
        else players[p].alive = false;
    }
    recomputeTerritory();
    recomputeBuildingLevels();
    addEvent("Tik op de kaart om je startplek te kiezen", 0xFFFFFF);
    return true;
}

bool Game::canSpawnAt(int tile) const {
    if (tile < 0 || tile >= map.w * map.h) return false;
    return map.isLand(tile) && owner[tile] == 0;
}

void Game::spawnAt(int p, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius + 1) continue;
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= map.w || yy >= map.h) continue;
            int t = yy * map.w + xx;
            if (map.isLand(t) && owner[t] == 0) setOwner(t, p);
        }
    Player& pl = players[p];
    pl.spawned = true;
    pl.alive = pl.tiles > 0;
    pl.troops = 2500;
    pl.cx = (float)x;
    pl.cy = (float)y;
}

bool Game::spawnHuman(int tile) {
    if (!spawnPhase || !canSpawnAt(tile)) return false;
    spawnAt(humanId, tile % map.w, tile / map.w, 4);
    if (settings.mode == MODE_SANDBOX) players[humanId].troops = 20000;
    spawnPhase = false;
    recomputeTerritory();
    addEvent("Veel succes! Tik op een gebied naast je om aan te vallen.", 0xFFFFFF);
    return true;
}

// ------------------------------------------------------------------ helpers

void Game::addEvent(const std::string& text, uint32_t color) {
    GameEvent e;
    e.text = text;
    e.tick = tick;
    e.color = color;
    events.push_back(e);
    if (events.size() > 40) events.erase(events.begin(), events.begin() + 10);
}

int Game::neighbors4(int t, int out[4]) const {
    int x = t % map.w, y = t / map.w, n = 0;
    if (x > 0) out[n++] = t - 1;
    if (x < map.w - 1) out[n++] = t + 1;
    if (y > 0) out[n++] = t - map.w;
    if (y < map.h - 1) out[n++] = t + map.w;
    return n;
}

bool Game::sameTeam(int a, int b) const {
    if (a <= 0 || b <= 0) return false;
    if (a == b) return true;
    return settings.teamsEnabled() && players[a].team == players[b].team;
}

int Game::buildingAt(int tile) const {
    if (tile < 0 || tile >= (int)hasBuilding.size() || !hasBuilding[tile]) return -1;
    for (size_t k = 0; k < buildings.size(); ++k)
        if (buildings[k].tile == tile) return (int)k;
    return -1;
}

void Game::setOwner(int t, int p) {
    int old = owner[t];
    if (old == p) return;
    owner[t] = (uint8_t)p;
    changed.push_back(t);
    if (p) {
        Player& np = players[p];
        np.tiles++;
        int x = t % map.w, y = t / map.w;
        if (np.bx1 < np.bx0) { np.bx0 = np.bx1 = x; np.by0 = np.by1 = y; }
        else {
            if (x < np.bx0) np.bx0 = x;
            if (x > np.bx1) np.bx1 = x;
            if (y < np.by0) np.by0 = y;
            if (y > np.by1) np.by1 = y;
        }
    }
    if (hasBuilding[t]) {
        int b = buildingAt(t);
        if (b >= 0) {
            if (p == 0 || buildings[b].type == B_FORT) {
                if (old == humanId) addEvent(std::string(buildingName(buildings[b].type)) + " verwoest!", 0xFF7766);
                buildings.erase(buildings.begin() + b);
                hasBuilding[t] = 0;
            } else {
                if (old == humanId) addEvent(std::string(buildingName(buildings[b].type)) + " veroverd door " + players[p].name, 0xFF7766);
                if (p == humanId) addEvent(std::string(buildingName(buildings[b].type)) + " veroverd!", 0x88FF88);
                buildings[b].owner = p;
            }
        }
    }
    if (old) {
        Player& op = players[old];
        op.tiles--;
        if (op.tiles <= 0 && op.alive) eliminate(old);
    }
}

void Game::eliminate(int p) {
    Player& pl = players[p];
    pl.alive = false;
    pl.troops = 0;
    pl.tiles = 0;
    for (size_t k = 0; k < attacks.size(); ++k)
        if (attacks[k].active && attacks[k].attacker == p) attacks[k].active = false;
    for (size_t k = 0; k < boats.size(); ++k)
        if (boats[k].active && boats[k].owner == p) boats[k].active = false;
    if (p == humanId) addEvent("Je bent verslagen!", 0xFF5555);
    else addEvent(pl.name + " is verslagen", 0xCCCCCC);
}

void Game::recomputeTerritory() {
    int np = (int)players.size();
    std::vector<double> sx(np, 0), sy(np, 0);
    for (int p = 0; p < np; ++p) {
        players[p].tiles = 0;
        players[p].bx0 = map.w; players[p].by0 = map.h;
        players[p].bx1 = -1; players[p].by1 = -1;
    }
    std::fill(adjacency.begin(), adjacency.end(), 0);
    const int W = map.w, H = map.h;
    for (int y = 0; y < H; ++y) {
        const int row = y * W;
        for (int x = 0; x < W; ++x) {
            int t = row + x;
            int o = owner[t];
            if (o) {
                Player& pl = players[o];
                pl.tiles++;
                sx[o] += x; sy[o] += y;
                if (x < pl.bx0) pl.bx0 = x;
                if (x > pl.bx1) pl.bx1 = x;
                if (y < pl.by0) pl.by0 = y;
                if (y > pl.by1) pl.by1 = y;
            }
            if (!map.isLand(t)) continue;
            if (x + 1 < W && map.isLand(t + 1)) {
                int o2 = owner[t + 1];
                if (o2 != o) { adjacency[o * MAX_PLAYERS + o2]++; adjacency[o2 * MAX_PLAYERS + o]++; }
            }
            if (y + 1 < H && map.isLand(t + W)) {
                int o2 = owner[t + W];
                if (o2 != o) { adjacency[o * MAX_PLAYERS + o2]++; adjacency[o2 * MAX_PLAYERS + o]++; }
            }
        }
    }
    for (int p = 1; p < np; ++p) {
        Player& pl = players[p];
        if (pl.tiles > 0) {
            float cx = (float)(sx[p] / pl.tiles), cy = (float)(sy[p] / pl.tiles);
            int t = (int)cy * W + (int)cx;
            // keep the label on own land if the centre falls outside
            if (owner[t] == p || pl.tiles < 4) { pl.cx = cx; pl.cy = cy; }
            else {
                int ot = (int)pl.cy * W + (int)pl.cx;
                if (ot < 0 || ot >= W * H || owner[ot] != p) { pl.cx = cx; pl.cy = cy; }
            }
        }
        if (pl.alive && pl.spawned && pl.tiles == 0) eliminate(p);
    }
}

void Game::recomputeBuildingLevels() {
    for (size_t p = 0; p < players.size(); ++p)
        players[p].cityLevels = players[p].marketLevels = players[p].mineLevels = 0;
    for (size_t k = 0; k < buildings.size(); ++k) {
        const Building& b = buildings[k];
        Player& pl = players[b.owner];
        if (b.type == B_CITY) pl.cityLevels += b.level;
        else if (b.type == B_MARKET) pl.marketLevels += b.level;
        else if (b.type == B_MINE) pl.mineLevels += b.level;
    }
}

// ------------------------------------------------------------------ economy

static double aiMul(const Game& g, int p) {
    if (g.players[p].human) return 1.0;
    static const double m[3] = {0.8, 1.0, 1.2};
    return m[g.settings.difficulty];
}

double Game::maxTroops(int p) const {
    const Player& pl = players[p];
    double m = (2500.0 + pl.tiles * 22.0 + 10000.0 * pl.cityLevels) * aiMul(*this, p);
    if (settings.mode == MODE_SANDBOX && p == humanId) m *= 5;
    return m;
}

double Game::troopGrowth(int p) const {
    const Player& pl = players[p];
    double base = 10.0 + std::pow((double)pl.tiles, 0.62) * 3.5 + pl.cityLevels * 25.0;
    base *= settings.troopSpeed / 100.0 * aiMul(*this, p);
    if (settings.mode == MODE_SANDBOX && p == humanId) base *= 5;
    double mx = maxTroops(p);
    if (pl.troops >= mx) return 0;
    return base * (1.0 - 0.7 * pl.troops / mx);
}

double Game::goldIncome(int p) const {
    const Player& pl = players[p];
    double g = 3.0 + pl.tiles * 0.08 + pl.marketLevels * 12.0;
    if (!pl.human) g *= aiMul(*this, p);
    return g;
}

double Game::materialIncome(int p) const {
    const Player& pl = players[p];
    return pl.tiles * 0.004 + pl.mineLevels * 3.0;
}

void Game::growPlayers() {
    for (size_t p = 1; p < players.size(); ++p) {
        Player& pl = players[p];
        if (!pl.alive || !pl.spawned) continue;
        double mx = maxTroops((int)p);
        if (pl.troops < mx) pl.troops = std::min(mx, pl.troops + troopGrowth((int)p) / TICKS_PER_SECOND);
        else pl.troops -= (pl.troops - mx) * 0.01;
        pl.gold += goldIncome((int)p) / TICKS_PER_SECOND;
        pl.materials += materialIncome((int)p) / TICKS_PER_SECOND;
    }
}

static const double kBuildGold[B_COUNT] = {3000, 2000, 1500, 2500};
static const double kBuildMat[B_COUNT] = {50, 0, 0, 150};

Cost Game::buildCost(int type) const {
    int count = 0;
    for (size_t k = 0; k < buildings.size(); ++k)
        if (buildings[k].owner == humanId && buildings[k].type == type) ++count;
    Cost c = {kBuildGold[type] * (1.0 + 0.25 * count), kBuildMat[type]};
    return c;
}

Cost Game::upgradeCost(const Building& b) const {
    Cost c = {kBuildGold[b.type] * 1.2 * b.level, 60.0 * b.level + (b.type == B_FORT ? 100 : 0)};
    return c;
}

bool Game::nearForeign(int tile, int p, int r) const {
    int x = tile % map.w, y = tile / map.w;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= map.w || yy >= map.h) continue;
            int t = yy * map.w + xx;
            if (map.isLand(t) && owner[t] != p) return true;
        }
    return false;
}

bool Game::canBuildHere(int p, int type, int tile, std::string* why) const {
    if (tile < 0 || tile >= map.w * map.h || owner[tile] != p) {
        if (why) *why = "Je kunt alleen op je eigen land bouwen";
        return false;
    }
    if (type == B_MINE && map.terrain(tile) != T_HILLS && map.terrain(tile) != T_MOUNTAINS) {
        if (why) *why = "Een mijn moet op heuvels of bergen";
        return false;
    }
    int x = tile % map.w, y = tile / map.w;
    for (size_t k = 0; k < buildings.size(); ++k) {
        int bx = buildings[k].tile % map.w, by = buildings[k].tile / map.w;
        if (std::abs(bx - x) <= 3 && std::abs(by - y) <= 3) {
            if (why) *why = "Te dicht bij een ander gebouw";
            return false;
        }
    }
    return true;
}

bool Game::build(int p, int type, int tile, std::string* why) {
    if (type < 0 || type >= B_COUNT || over || spawnPhase) return false;
    if (!canBuildHere(p, type, tile, why)) return false;
    Player& pl = players[p];
    Cost c = buildCost(type);
    if (p != humanId) { c.gold = kBuildGold[type]; c.materials = kBuildMat[type]; }
    if (pl.gold < c.gold || pl.materials < c.materials) {
        if (why) *why = "Niet genoeg goud of grondstoffen";
        return false;
    }
    pl.gold -= c.gold;
    pl.materials -= c.materials;
    Building b;
    b.type = type;
    b.tile = tile;
    b.owner = p;
    b.level = 1;
    buildings.push_back(b);
    hasBuilding[tile] = 1;
    recomputeBuildingLevels();
    if (p == humanId) addEvent(std::string(buildingName(type)) + " gebouwd", 0x88FF88);
    return true;
}

bool Game::upgrade(int p, int bi, std::string* why) {
    if (bi < 0 || bi >= (int)buildings.size() || buildings[bi].owner != p) return false;
    Building& b = buildings[bi];
    if (b.level >= MAX_LEVEL) {
        if (why) *why = "Al op het hoogste niveau";
        return false;
    }
    Cost c = upgradeCost(b);
    Player& pl = players[p];
    if (pl.gold < c.gold || pl.materials < c.materials) {
        if (why) *why = "Niet genoeg goud of grondstoffen";
        return false;
    }
    pl.gold -= c.gold;
    pl.materials -= c.materials;
    b.level++;
    recomputeBuildingLevels();
    if (p == humanId) addEvent(std::string(buildingName(b.type)) + " verbeterd", 0x88FF88);
    return true;
}

bool Game::recruit(int p, std::string* why) {
    Player& pl = players[p];
    const double cost = 1000;
    double mx = maxTroops(p) * 1.25;
    if (pl.gold < cost) {
        if (why) *why = "Rekruteren kost 1000 goud";
        return false;
    }
    if (pl.troops >= mx) {
        if (why) *why = "Je leger zit vol";
        return false;
    }
    pl.gold -= cost;
    pl.troops = std::min(mx, pl.troops + 1500);
    return true;
}

// ------------------------------------------------------------------ combat

static double terrainMul(int t) {
    switch (t) {
        case T_HILLS: return 1.6;
        case T_MOUNTAINS: return 2.4;
        case T_DESERT: return 1.2;
        case T_TUNDRA: return 1.3;
        default: return 1.0;
    }
}

struct FortInfo {
    int x, y, owner, level;
};
static std::vector<FortInfo> g_forts;  // snapshot of all forts, taken each tick

double Game::fortBonus(int tile, int defender) const {
    double m = 1.0;
    int x = tile % map.w, y = tile / map.w;
    for (size_t k = 0; k < g_forts.size(); ++k) {
        const FortInfo& f = g_forts[k];
        if (f.owner != defender) continue;
        int dx = f.x - x, dy = f.y - y;
        if (dx * dx + dy * dy <= 144) m = std::max(m, 1.0 + 0.6 * f.level);
    }
    return m;
}

double Game::tileCost(int tile, int target) const {
    double tm = terrainMul(map.terrain(tile));
    if (target == 0) return 5.0 * tm;
    const Player& d = players[target];
    double density = d.troops / std::max(1, d.tiles);
    return tm * (5.0 + density * 1.1) * fortBonus(tile, target);
}

bool Game::buildFrontier(Attack& a) {
    a.frontier.clear();
    const Player& at = players[a.attacker];
    if (at.bx1 < at.bx0) return false;
    int x0 = std::max(0, at.bx0 - 1), x1 = std::min(map.w - 1, at.bx1 + 1);
    int y0 = std::max(0, at.by0 - 1), y1 = std::min(map.h - 1, at.by1 + 1);
    if (a.target) {
        const Player& tg = players[a.target];
        if (tg.bx1 < tg.bx0) return false;
        x0 = std::max(x0, tg.bx0); x1 = std::min(x1, tg.bx1);
        y0 = std::max(y0, tg.by0); y1 = std::min(y1, tg.by1);
    }
    const int W = map.w;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            int t = y * W + x;
            if (owner[t] != a.target || !map.isLand(t)) continue;
            if ((x > 0 && owner[t - 1] == a.attacker) || (x < W - 1 && owner[t + 1] == a.attacker) ||
                (y > 0 && owner[t - W] == a.attacker) || (y < map.h - 1 && owner[t + W] == a.attacker))
                a.frontier.push_back(t);
        }
    return !a.frontier.empty();
}

int Game::startAttack(int att, int target, double troops, int aimTile, bool fromStock) {
    Player& ap = players[att];
    if (fromStock) troops = std::min(troops, ap.troops);
    if (troops < 1) return -1;

    // Reinforce an attack that is already running.
    for (size_t k = 0; k < attacks.size(); ++k) {
        Attack& a = attacks[k];
        if (a.active && a.attacker == att && a.target == target) {
            a.troops += troops;
            a.aimTile = aimTile;
            if (fromStock) ap.troops -= troops;
            return (int)k;
        }
    }

    Attack tmp;
    tmp.active = true;
    tmp.attacker = att;
    tmp.target = target;
    tmp.aimTile = aimTile;
    if (!buildFrontier(tmp)) {
        if (!fromStock) ap.troops += troops;  // nowhere to go: troops come home
        return -1;
    }
    if (fromStock) ap.troops -= troops;

    // Two armies attacking each other meet in the middle first.
    if (target) {
        for (size_t k = 0; k < attacks.size(); ++k) {
            Attack& b = attacks[k];
            if (b.active && b.attacker == target && b.target == att) {
                double m = std::min(troops, b.troops);
                troops -= m;
                b.troops -= m;
                if (b.troops < 1) b.active = false;
                break;
            }
        }
        if (troops < 1) return -1;
        players[target].lastAttackedBy = att;
        players[target].lastAttackedTick = tick;
        if (target == humanId) addEvent(ap.name + " valt je aan!", 0xFF7766);
    }
    tmp.troops = troops;
    for (size_t k = 0; k < attacks.size(); ++k) {
        if (!attacks[k].active) {
            attacks[k].frontier.swap(tmp.frontier);
            tmp.frontier.clear();
            attacks[k].active = true;
            attacks[k].attacker = att;
            attacks[k].target = target;
            attacks[k].troops = troops;
            attacks[k].aimTile = aimTile;
            return (int)k;
        }
    }
    attacks.push_back(tmp);
    return (int)attacks.size() - 1;
}

void Game::stepAttack(Attack& a) {
    Player& ap = players[a.attacker];
    if (!ap.alive) { a.active = false; return; }
    if (a.target && !players[a.target].alive) {
        ap.troops += a.troops;
        a.active = false;
        return;
    }
    if (a.frontier.empty() || a.frontier.size() > 60000) {
        if (!buildFrontier(a)) {
            ap.troops += a.troops;
            a.active = false;
            return;
        }
    }
    int budget = 2 + (int)a.frontier.size() / 7;
    if (budget > 90) budget = 90;
    int guard = budget * 4 + 8;
    int conquered = 0;
    Player* dp = a.target ? &players[a.target] : 0;
    while (conquered < budget && guard-- > 0 && !a.frontier.empty()) {
        // pick the better of two random frontier tiles (rounder fronts)
        int n = (int)a.frontier.size();
        int i1 = randInt(n), i2 = randInt(n);
        int best = i1, bestScore = -1;
        for (int pass = 0; pass < 2; ++pass) {
            int idx = pass ? i2 : i1;
            int t = a.frontier[idx];
            int nb[4], c = 0, m = neighbors4(t, nb);
            for (int k = 0; k < m; ++k)
                if (owner[nb[k]] == a.attacker) ++c;
            if (c > bestScore) { bestScore = c; best = idx; }
        }
        int t = a.frontier[best];
        a.frontier[best] = a.frontier.back();
        a.frontier.pop_back();
        if (owner[t] != a.target || bestScore <= 0) continue;

        double cost = tileCost(t, a.target);
        if (a.troops < cost) {
            // attack runs out of steam: survivors go home
            ap.troops += a.troops;
            a.active = false;
            if (a.attacker == humanId && conquered == 0) addEvent("Aanval gestopt: te weinig troepen", 0xFFCC66);
            return;
        }
        a.troops -= cost;
        if (dp) {
            double density = dp->troops / std::max(1, dp->tiles);
            dp->troops = std::max(0.0, dp->troops - (density * 0.8 + 0.5));
        }
        setOwner(t, a.attacker);
        ++conquered;
        if (!a.active) return;  // attacker eliminated meanwhile (cannot happen, but safe)
        int nb[4], m = neighbors4(t, nb);
        for (int k = 0; k < m; ++k)
            if (owner[nb[k]] == a.target && map.isLand(nb[k])) a.frontier.push_back(nb[k]);
    }
    if (dp) {
        dp->lastAttackedBy = a.attacker;
        dp->lastAttackedTick = tick;
    }
}

void Game::stepAttacks() {
    g_forts.clear();
    for (size_t k = 0; k < buildings.size(); ++k)
        if (buildings[k].type == B_FORT) {
            FortInfo f = {buildings[k].tile % map.w, buildings[k].tile / map.w, buildings[k].owner, buildings[k].level};
            g_forts.push_back(f);
        }
    for (size_t k = 0; k < attacks.size(); ++k)
        if (attacks[k].active) stepAttack(attacks[k]);
}

// ------------------------------------------------------------------ boats

bool Game::findBoatPath(int p, int targetTile, std::vector<int>& path) {
    path.clear();
    const int W = map.w, H = map.h, N = W * H;
    if (++stampId == 0) { std::fill(stamp.begin(), stamp.end(), 0); stampId = 1; }
    std::vector<int> queue;
    queue.reserve(4096);
    int nb[4];
    int m = neighbors4(targetTile, nb);
    for (int k = 0; k < m; ++k)
        if (map.isWater(nb[k])) {
            stamp[nb[k]] = stampId;
            parent[nb[k]] = -1;
            queue.push_back(nb[k]);
        }
    size_t head = 0;
    int goal = -1;
    while (head < queue.size() && goal < 0) {
        int t = queue[head++];
        int x = t % W, y = t / W;
        int cand[4] = {x > 0 ? t - 1 : -1, x < W - 1 ? t + 1 : -1, y > 0 ? t - W : -1, y < H - 1 ? t + W : -1};
        for (int k = 0; k < 4; ++k) {
            int c = cand[k];
            if (c < 0 || c >= N) continue;
            if (map.isLand(c)) {
                if (owner[c] == p) { goal = t; break; }
                continue;
            }
            if (stamp[c] == stampId) continue;
            stamp[c] = stampId;
            parent[c] = t;
            queue.push_back(c);
        }
    }
    if (goal < 0) return false;
    for (int t = goal; t >= 0; t = parent[t]) {
        path.push_back(t);
        if (path.size() > (size_t)N) return false;
    }
    path.push_back(targetTile);
    return path.size() >= 2;
}

bool Game::launchBoat(int p, int tile, double troops, std::string* why) {
    int active = 0;
    for (size_t k = 0; k < boats.size(); ++k)
        if (boats[k].active && boats[k].owner == p) ++active;
    if (active >= 3) {
        if (why) *why = "Je hebt al 3 schepen onderweg";
        return false;
    }
    // land on the coast: if the tapped tile is inland, use the nearest coastal tile of that owner
    int target = -1;
    int want = owner[tile];
    int tx = tile % map.w, ty = tile / map.w;
    int bestD = 1 << 30;
    for (int dy = -10; dy <= 10; ++dy)
        for (int dx = -10; dx <= 10; ++dx) {
            int x = tx + dx, y = ty + dy;
            if (x < 0 || y < 0 || x >= map.w || y >= map.h) continue;
            int t = y * map.w + x;
            if (!map.isLand(t) || owner[t] != want) continue;
            int nb[4], m = neighbors4(t, nb);
            bool coast = false;
            for (int k = 0; k < m; ++k)
                if (map.isWater(nb[k])) coast = true;
            int d = dx * dx + dy * dy;
            if (coast && d < bestD) { bestD = d; target = t; }
        }
    if (target < 0) {
        if (why) *why = "Niet bereikbaar: geen grens en geen kust in de buurt";
        return false;
    }
    std::vector<int> path;
    if (!findBoatPath(p, target, path)) {
        if (why) *why = "Niet bereikbaar over zee";
        return false;
    }
    Player& pl = players[p];
    troops = std::min(troops, pl.troops);
    if (troops < 1) {
        if (why) *why = "Te weinig troepen";
        return false;
    }
    pl.troops -= troops;
    Boat* b = 0;
    for (size_t k = 0; k < boats.size(); ++k)
        if (!boats[k].active) { b = &boats[k]; break; }
    if (!b) { boats.push_back(Boat()); b = &boats.back(); }
    b->active = true;
    b->owner = p;
    b->troops = troops;
    b->target = target;
    b->pos = 0;
    b->path.swap(path);
    if (p == humanId) addEvent("Schip vertrekt", 0xAADDFF);
    else if (want == humanId) addEvent(pl.name + " stuurt een schip naar je toe!", 0xFF7766);
    return true;
}

void Game::landBoat(Boat& b) {
    b.active = false;
    int p = b.owner;
    Player& pl = players[p];
    if (!pl.alive) return;
    int t = owner[b.target];
    if (t == p || sameTeam(p, t)) {
        pl.troops += b.troops;
        return;
    }
    double cost = tileCost(b.target, t);
    if (b.troops <= cost) {
        if (p == humanId) addEvent("Landing mislukt", 0xFFCC66);
        return;
    }
    b.troops -= cost;
    setOwner(b.target, p);
    if (b.troops >= 1) startAttack(p, t, b.troops, b.target, false);
}

void Game::stepBoats() {
    for (size_t k = 0; k < boats.size(); ++k) {
        Boat& b = boats[k];
        if (!b.active) continue;
        if (!players[b.owner].alive) { b.active = false; continue; }
        b.pos += 1.5f;
        if (b.pos >= (float)(b.path.size() - 1)) landBoat(b);
    }
}

// ------------------------------------------------------------------ orders

bool Game::orderAttack(int p, int tile, double troops, std::string* why) {
    if (over || spawnPhase) return false;
    if (tile < 0 || tile >= map.w * map.h) return false;
    if (!map.isLand(tile)) {
        if (why) *why = "Dat is water";
        return false;
    }
    int t = owner[tile];
    if (t == p) {
        if (why) *why = "Dat is al van jou";
        return false;
    }
    if (t && sameTeam(p, t)) {
        if (why) *why = "Dat is een teamgenoot";
        return false;
    }
    if (troops < 1 || players[p].troops < 1) {
        if (why) *why = "Te weinig troepen";
        return false;
    }
    if (startAttack(p, t, troops, tile, true) >= 0) return true;
    return launchBoat(p, tile, troops, why);
}

// ------------------------------------------------------------------ AI

int Game::randomOwnedTile(int p, int tries) {
    const Player& pl = players[p];
    if (pl.bx1 < pl.bx0) return -1;
    int bw = pl.bx1 - pl.bx0 + 1, bh = pl.by1 - pl.by0 + 1;
    for (int k = 0; k < tries; ++k) {
        int t = (pl.by0 + randInt(bh)) * map.w + pl.bx0 + randInt(bw);
        if (owner[t] == p) return t;
    }
    return -1;
}

void Game::aiBuild(int p) {
    Player& pl = players[p];
    int type = -1;
    double mx = maxTroops(p);
    if (pl.troops > 0.7 * mx && pl.cityLevels < 1 + pl.tiles / 700) type = B_CITY;
    else if (pl.marketLevels < 1 + pl.tiles / 500) type = B_MARKET;
    else if (pl.mineLevels < pl.tiles / 1200) type = B_MINE;
    else if (settings.difficulty >= DIFF_NORMAL && rand01() < 0.3) type = B_FORT;

    if (type >= 0) {
        if (pl.gold < kBuildGold[type] || pl.materials < kBuildMat[type]) return;
        for (int tries = 0; tries < 25; ++tries) {
            int t = randomOwnedTile(p, 20);
            if (t < 0) return;
            bool border = nearForeign(t, p, 3);
            if (type == B_FORT ? !border : border) continue;
            if (build(p, type, t, 0)) return;
        }
        return;
    }
    // otherwise upgrade something
    for (int tries = 0; tries < 4 && !buildings.empty(); ++tries) {
        int bi = randInt((int)buildings.size());
        if (buildings[bi].owner == p && buildings[bi].level < MAX_LEVEL) {
            upgrade(p, bi, 0);
            return;
        }
    }
    if (pl.gold > 15000 && pl.troops < 0.5 * mx) recruit(p, 0);
}

void Game::aiAttack(int p) {
    Player& pl = players[p];
    int d = settings.difficulty;
    static const int maxAtt[3] = {1, 2, 3};
    static const double threshold[3] = {0.55, 0.42, 0.32};
    static const double share[3] = {0.35, 0.45, 0.55};
    static const double need[3] = {1.6, 1.2, 0.9};
    static const double humanBias[3] = {0.7, 1.0, 1.25};

    int running = 0;
    for (size_t k = 0; k < attacks.size(); ++k)
        if (attacks[k].active && attacks[k].attacker == p) ++running;
    if (running >= maxAtt[d]) return;

    double mx = maxTroops(p);
    double ratio = pl.troops / mx;
    bool peaceful = settings.mode == MODE_SANDBOX && !settings.aiAttacks;

    // 1. Strike back at whoever attacked us recently.
    int foe = pl.lastAttackedBy;
    if (!peaceful && foe > 0 && tick - pl.lastAttackedTick < 120 && players[foe].alive && adj(p, foe) > 0 &&
        !sameTeam(p, foe) && ratio > 0.25 && pl.troops > players[foe].troops * 0.5 && d > DIFF_EASY) {
        int aim = randomOwnedTile(foe, 30);
        startAttack(p, foe, pl.troops * 0.45, aim < 0 ? 0 : aim, true);
        return;
    }
    if (ratio < threshold[d]) return;

    // 2. Grab free land next to us.
    if (adj(p, 0) > 0) {
        startAttack(p, 0, pl.troops * (0.3 + 0.05 * d), 0, true);
        return;
    }
    if (peaceful) return;

    // 3. Attack the most attractive neighbour.
    int best = -1;
    double bestScore = 0;
    bool anyNeighbour = false;
    for (int q = 1; q < (int)players.size(); ++q) {
        if (q == p || !players[q].alive || adj(p, q) == 0) continue;
        anyNeighbour = true;
        if (sameTeam(p, q)) continue;
        double score = pl.troops / (players[q].troops + 100.0);
        if (players[q].human) score *= humanBias[d];
        if (pl.lastAttackedBy == q) score *= 1.5;
        score *= 0.9 + 0.2 * rand01();
        if (score > bestScore) { bestScore = score; best = q; }
    }
    if (best > 0 && bestScore > need[d]) {
        int aim = randomOwnedTile(best, 30);
        startAttack(p, best, pl.troops * share[d], aim < 0 ? 0 : aim, true);
        return;
    }

    // 4. Nothing to fight over land: go by sea (rarely, the path search costs time).
    if ((!anyNeighbour && rand01() < 0.3) || rand01() < 0.02 * (d + 1)) {
        for (size_t k = 0; k < boats.size(); ++k)
            if (boats[k].active && boats[k].owner == p) return;
        for (int tries = 0; tries < 60; ++tries) {
            int t = randInt(map.w * map.h);
            if (!map.isLand(t) || owner[t] == p || sameTeam(p, owner[t])) continue;
            int o = owner[t];
            if (o && players[o].troops > pl.troops) continue;
            int nb[4], m = neighbors4(t, nb);
            bool coast = false;
            for (int k = 0; k < m; ++k)
                if (map.isWater(nb[k])) coast = true;
            if (!coast) continue;
            launchBoat(p, t, pl.troops * 0.35, 0);
            return;
        }
    }
}

void Game::aiThink(int p) {
    static const int interval[3] = {30, 18, 10};
    Player& pl = players[p];
    pl.nextThink = tick + interval[settings.difficulty] + randInt(6);
    aiBuild(p);
    aiAttack(p);
}

// ------------------------------------------------------------------ main step

int Game::aliveCount() const {
    int n = 0;
    for (size_t p = 1; p < players.size(); ++p)
        if (players[p].alive) ++n;
    return n;
}

int Game::rankOf(int p) const {
    int r = 1;
    for (size_t q = 1; q < players.size(); ++q)
        if ((int)q != p && players[q].tiles > players[p].tiles) ++r;
    return r;
}

void Game::checkWin() {
    if (over) return;
    int land = std::max(1, map.landTiles);
    const Player& me = players[humanId];
    if (!me.alive && me.spawned) {
        over = true;
        // who is doing best?
        int best = 0;
        for (size_t p = 1; p < players.size(); ++p)
            if (players[p].alive && (best == 0 || players[p].tiles > players[best].tiles)) best = (int)p;
        winnerPlayer = best;
        if (settings.teamsEnabled() && best) winnerTeam = players[best].team;
        return;
    }
    if (settings.mode == MODE_SANDBOX) return;

    if (settings.teamsEnabled()) {
        int teamTiles[4] = {0, 0, 0, 0};
        int teamAlive[4] = {0, 0, 0, 0};
        for (size_t p = 1; p < players.size(); ++p) {
            int t = players[p].team & 3;
            teamTiles[t] += players[p].tiles;
            if (players[p].alive) teamAlive[t]++;
        }
        int teamsLeft = 0, lastTeam = -1;
        for (int t = 0; t < settings.numTeams; ++t)
            if (teamAlive[t]) { ++teamsLeft; lastTeam = t; }
        for (int t = 0; t < settings.numTeams; ++t)
            if (teamTiles[t] * 100 >= settings.winPercent * land) { winnerTeam = t; over = true; }
        if (!over && teamsLeft <= 1) { winnerTeam = lastTeam; over = true; }
        return;
    }
    int alive = 0, last = 0;
    for (size_t p = 1; p < players.size(); ++p)
        if (players[p].alive) { ++alive; last = (int)p; }
    if (alive <= 1) { winnerPlayer = last; over = true; return; }
    if (settings.mode == MODE_CONQUEST) return;  // conquest: last one standing
    for (size_t p = 1; p < players.size(); ++p)
        if (players[p].tiles * 100 >= settings.winPercent * land) { winnerPlayer = (int)p; over = true; return; }
}

void Game::step() {
    if (spawnPhase) return;
    ++tick;
    recomputeBuildingLevels();
    growPlayers();
    stepAttacks();
    stepBoats();
    if (tick % 20 == 0) recomputeTerritory();
    for (size_t p = 2; p < players.size(); ++p) {
        Player& pl = players[p];
        if (pl.alive && pl.spawned && tick >= pl.nextThink) aiThink((int)p);
    }
    if (tick % 10 == 0) {
        bool wasOver = over;
        checkWin();
        if (over && !wasOver) {
            bool won = winnerPlayer == humanId ||
                       (settings.teamsEnabled() && winnerTeam >= 0 && winnerTeam == players[humanId].team);
            addEvent(won ? "Gewonnen!" : "Spel voorbij", won ? 0x88FF88 : 0xFF7766);
        }
    }
}

// ------------------------------------------------------------------ checks

bool Game::checkConsistency(std::string* err) const {
    std::vector<int> count(players.size(), 0);
    for (size_t t = 0; t < owner.size(); ++t) {
        if ((size_t)owner[t] >= players.size()) { if (err) *err = "owner out of range"; return false; }
        if (owner[t] && !map.isLand((int)t)) { if (err) *err = "owned water tile"; return false; }
        count[owner[t]]++;
    }
    for (size_t p = 1; p < players.size(); ++p) {
        if (count[p] != players[p].tiles) {
            if (err) {
                char buf[96];
                snprintf(buf, sizeof buf, "tile count mismatch for %d: %d vs %d", (int)p, count[p], players[p].tiles);
                *err = buf;
            }
            return false;
        }
        if (players[p].troops < -0.001 || players[p].troops != players[p].troops) { if (err) *err = "bad troops"; return false; }
        if (players[p].alive && players[p].spawned && players[p].tiles == 0) { if (err) *err = "alive without land"; return false; }
    }
    for (size_t k = 0; k < buildings.size(); ++k) {
        const Building& b = buildings[k];
        if (owner[b.tile] != b.owner || !hasBuilding[b.tile]) { if (err) *err = "building owner mismatch"; return false; }
    }
    size_t marked = 0;
    for (size_t t = 0; t < hasBuilding.size(); ++t) marked += hasBuilding[t];
    if (marked != buildings.size()) { if (err) *err = "building marks mismatch"; return false; }
    for (size_t k = 0; k < attacks.size(); ++k)
        if (attacks[k].active && (attacks[k].troops < 0 || !players[attacks[k].attacker].alive)) {
            if (err) *err = "bad attack";
            return false;
        }
    return true;
}

}  // namespace fc
