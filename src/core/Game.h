// Frontier Classic - game simulation (no graphics, no SDL).
// Everything here runs locally; there is no network code anywhere.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Json.h"

namespace fc {

// T_BLOCKED: land of a country that is not part of this map (grey, nobody can take it)
enum Terrain { T_DEEP = 0, T_SHALLOW, T_PLAINS, T_HILLS, T_MOUNTAINS, T_DESERT, T_TUNDRA, T_BLOCKED };
enum Mode { MODE_FFA = 0, MODE_TEAMS, MODE_CONQUEST, MODE_SANDBOX, MODE_CUSTOM, MODE_COUNT };
enum Difficulty { DIFF_EASY = 0, DIFF_NORMAL, DIFF_HARD };
enum BuildingType { B_CITY = 0, B_MARKET, B_MINE, B_FORT, B_COUNT };

const int TICKS_PER_SECOND = 10;
const int MAX_PLAYERS = 64;  // including slot 0 (neutral)
const int MAX_LEVEL = 3;

const char* modeName(int mode);
const char* difficultyName(int d);
const char* buildingName(int type);

struct Nation {
    std::string name;
    int x, y;
};

struct MapData {
    std::string id, name;
    int w = 0, h = 0;
    int landTiles = 0;
    std::vector<uint8_t> tiles;
    std::vector<Nation> nations;

    bool load(const std::string& mapsDir, const std::string& mapId, std::string* err);
    int terrain(int i) const { return tiles[i] & 15; }
    bool isLand(int i) const { int t = tiles[i] & 15; return t >= T_PLAINS && t <= T_TUNDRA; }
    bool isWater(int i) const { return (tiles[i] & 15) <= T_SHALLOW; }
    bool countryBorder(int i) const { return (tiles[i] & 16) != 0; }
};

struct MapEntry {
    std::string id, name;
};
bool loadMapIndex(const std::string& mapsDir, std::vector<MapEntry>& out);

struct Settings {
    std::string mapId = "world";
    int mode = MODE_FFA;
    int numAI = 20;          // 10..50
    int difficulty = DIFF_NORMAL;
    int numTeams = 2;        // 2..4 (teams mode, or custom with teams)
    bool customTeams = false;
    int startGold = 1000;
    int winPercent = 80;     // percent of land needed to win (FFA/Teams/Custom)
    int troopSpeed = 100;    // percent
    bool aiAttacks = true;   // sandbox: AI may be peaceful
    int numHumans = 1;       // multiplayer: players 1..numHumans are people
    // single player cheats (ignored in multiplayer)
    bool cheatTroops = false, cheatGold = false, cheatMaterials = false, cheatGod = false, cheatFast = false;
    bool anyCheat() const { return cheatTroops || cheatGold || cheatMaterials || cheatGod || cheatFast; }
    unsigned seed = 12345;

    bool teamsEnabled() const { return mode == MODE_TEAMS || (mode == MODE_CUSTOM && customTeams); }
    JVal toJson() const;
    void fromJson(const JVal& j);
};

struct Building {
    int type = 0, tile = 0, owner = 0, level = 1;
};

struct Player {
    std::string name;
    uint32_t color = 0xffffffff;  // 0xRRGGBB
    bool human = false;
    bool alive = true;
    bool spawned = false;
    int team = 0;
    double troops = 0, gold = 0, materials = 0;
    int tiles = 0;
    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;  // bounding box of owned tiles
    float cx = 0, cy = 0;                     // centre of territory (for labels)
    int lastAttackedBy = -1, lastAttackedTick = -100000;
    int nextThink = 0;
    // derived each tick from buildings
    int cityLevels = 0, marketLevels = 0, mineLevels = 0;
};

struct Attack {
    bool active = false;
    int attacker = 0, target = 0;
    double troops = 0;
    int aimTile = 0;  // where the player tapped (for the arrow animation)
    std::vector<int> frontier;
};

struct Boat {
    bool active = false;
    int owner = 0;
    double troops = 0;
    int target = 0;   // land tile to invade
    float pos = 0;    // index into path (fractional for smooth animation)
    std::vector<int> path;
};

struct GameEvent {
    std::string text;
    int tick;
    uint32_t color;
    int player;    // 0 = for everyone, else only for this player
    unsigned seq;  // running number (multiplayer: which events were already sent)
};

struct Cost {
    double gold, materials;
};

class Game {
public:
    MapData map;
    Settings settings;
    std::vector<uint8_t> owner;       // per tile: 0 = nobody, else player id
    std::vector<uint8_t> hasBuilding; // per tile: 1 if a building stands here
    std::vector<Player> players;      // [0] is "nobody"
    std::vector<Building> buildings;
    std::vector<Attack> attacks;      // pooled: inactive slots are reused
    std::vector<Boat> boats;          // pooled
    std::vector<GameEvent> events;
    std::vector<int> changed;         // tiles whose owner changed (renderer consumes)

    int tick = 0;
    int humanId = 1;
    bool spawnPhase = true;
    bool over = false;
    int winnerPlayer = 0;  // player id, or 0
    int winnerTeam = -1;
    uint32_t rng = 1;

    // ---- setup ----
    bool newGame(const Settings& s, const std::string& mapsDir, std::string* err);
    bool spawnHuman(int tile) { return spawnPlayer(humanId, tile); }
    bool spawnPlayer(int p, int tile);   // the game starts when every human has spawned
    void autoSpawnRemaining();           // random start for humans who did not choose
    void makeAI(int p);                  // a player who left is taken over by the AI
    bool isHuman(int p) const { return p > 0 && p < (int)players.size() && players[p].human; }
    // cheats only ever work for the person in a single player game
    bool cheatsFor(int p) const { return settings.numHumans == 1 && isHuman(p); }
    bool eventVisible(const GameEvent& e) const { return e.player == 0 || e.player == humanId; }
    unsigned eventSeq = 0;
    bool canSpawnAt(int tile) const;

    // ---- simulation ----
    void step();  // one tick (0.1 s)

    // ---- player actions (used by both the human UI and the AI) ----
    // Sends `troops` from player `p` against the owner of `tile` (land or by boat).
    // Returns false and fills `why` when not possible.
    bool orderAttack(int p, int tile, double troops, std::string* why);
    bool build(int p, int type, int tile, std::string* why);
    bool upgrade(int p, int buildingIndex, std::string* why);
    bool recruit(int p, std::string* why);
    Cost buildCost(int p, int type) const;
    Cost upgradeCost(const Building& b) const;
    int buildingAt(int tile) const;  // index or -1
    bool canBuildHere(int p, int type, int tile, std::string* why) const;

    // ---- queries ----
    double maxTroops(int p) const;
    double troopGrowth(int p) const;   // per second
    double goldIncome(int p) const;    // per second
    double materialIncome(int p) const;// per second
    bool sameTeam(int a, int b) const;
    bool isAdjacentByLand(int p, int target) const { return adj(p, target) > 0; }
    int adj(int a, int b) const { return adjacency[a * MAX_PLAYERS + b]; }
    int aliveCount() const;
    int rankOf(int p) const;
    int width() const { return map.w; }
    int height() const { return map.h; }

    // ---- save / load ----
    JVal toJson() const;
    bool fromJson(const JVal& j, const std::string& mapsDir, std::string* err);
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path, const std::string& mapsDir, std::string* err);
    static bool readSummary(const std::string& path, std::string& summary);

    // ---- debug / tests ----
    bool checkConsistency(std::string* err) const;

private:
    std::vector<int> adjacency;       // MAX_PLAYERS^2 border lengths, refreshed every 20 ticks
    std::vector<uint32_t> stamp;      // scratch for flood fills
    std::vector<int> parent;          // scratch for boat path search
    uint32_t stampId = 0;

    uint32_t rand32();
    double rand01();
    int randInt(int n);

    void setupPlayers();
    void spawnAt(int p, int x, int y, int radius);
    void setOwner(int tile, int p);
    void eliminate(int p);
public:
    void addEvent(const std::string& text, uint32_t color);
    void addEventFor(int p, const std::string& text, uint32_t color);  // only if p is a human
    void recomputeBuildingLevels();
private:
    void recomputeTerritory();  // tiles, bbox, centre, adjacency
    void growPlayers();
    void stepAttacks();
    void stepBoats();
    void stepAttack(Attack& a);
    bool buildFrontier(Attack& a);
    int startAttack(int attacker, int target, double troops, int aimTile, bool fromPlayerStock);
    bool launchBoat(int p, int tile, double troops, std::string* why);
    bool findBoatPath(int p, int targetTile, std::vector<int>& path);
    void landBoat(Boat& b);
    double tileCost(int tile, int target) const;
    double fortBonus(int tile, int defender) const;
    void checkWin();
    void aiThink(int p);
    void aiBuild(int p);
    void aiAttack(int p);
    int randomOwnedTile(int p, int tries);
    bool nearForeign(int tile, int p, int radius) const;
    int neighbors4(int tile, int out[4]) const;
};

std::string formatNumber(double v);  // 12.3K, 4.5M

}  // namespace fc
