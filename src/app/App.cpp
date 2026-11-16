#include "App.h"
#include "Music.h"
#include "Flag.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fc {

// colours are 0xRRGGBBAA
static const uint32_t C_PANEL = 0x101826E8;
static const uint32_t C_BTN = 0x2A3B55FF;
static const uint32_t C_BTN_SEL = 0x3A7BD5FF;
static const uint32_t C_BTN_OFF = 0x1E2838FF;
static const uint32_t C_TEXT = 0xFFFFFFFF;
static const uint32_t C_DIM = 0xA9B4C4FF;
static const uint32_t C_ACCENT = 0xF1C40FFF;
static const uint32_t C_GOOD = 0x7DFF8AFF;
static const uint32_t C_BAD = 0xFF7A6EFF;

static const float kAttackPct[5] = {0.10f, 0.25f, 0.50f, 0.75f, 1.0f};
static const char* kAttackLabel[5] = {"10%", "25%", "50%", "75%", "100%"};
static const char* kSlots[4] = {"autosave", "slot1", "slot2", "slot3"};
static const char* kSlotNames[4] = {"Autosave", "Slot 1", "Slot 2", "Slot 3"};

static uint32_t rgbToRgba(uint32_t rgb, uint8_t a = 255) { return (rgb << 8) | a; }

// ------------------------------------------------------------------ setup

bool App::init(const std::string& resDir, const std::string& saveDir, std::string* err) {
    resDir_ = resDir;
    mapsDir_ = resDir + "maps";
    saveDir_ = saveDir;
    if (!gfx.init(resDir, err)) return false;
    if (!loadMapIndex(mapsDir_, maps_)) {
        if (err) *err = "Kaartenlijst (maps/index.json) ontbreekt";
        return false;
    }
    loadSettings();
    fc_music_load((resDir + "music/holding_the_ridge.mp3").c_str());
    loadFlag();
    // write test: can we save in the save folder?
    std::string probe = saveDir_ + "write_test.tmp";
    saveOk_ = writeFileAtomic(probe, "ok");
    remove(probe.c_str());
    if (!saveOk_) showToast("Let op: opslaan werkt niet in " + saveDir_);
    lastAutosave_ = SDL_GetTicks();
    return true;
}

void App::shutdown() { gfx.shutdown(); }

void App::saveSettings() {
    JVal j = JVal::object();
    j.set("newGame", newSettings.toJson());
    j.set("attackIdx", attackIdx_);
    j.set("music", musicOn_);
    j.set("joinIp", joinIp_);
    j.set("port", portText_);
    writeFileAtomic(saveDir_ + "settings.json", j.dump());
}

void App::loadSettings() {
    std::string text;
    JVal j;
    if (readFile(saveDir_ + "settings.json", text) && JVal::parse(text, j)) {
        newSettings.fromJson(j["newGame"]);
        attackIdx_ = std::max(0, std::min(4, j["attackIdx"].i(1)));
        musicOn_ = j["music"].boolean(true);
        joinIp_ = j["joinIp"].str("");
        portText_ = j["port"].str("47800");
        if (joinIp_.size() > 15) joinIp_.clear();
        if (portText_.size() > 5) portText_ = "47800";
    }
    bool found = false;
    for (size_t k = 0; k < maps_.size(); ++k)
        if (maps_[k].id == newSettings.mapId) found = true;
    if (!found && !maps_.empty()) newSettings.mapId = maps_[0].id;
    if (newSettings.numAI < 10) newSettings.numAI = 10;
}

bool App::startGame(const Settings& s) {
    std::string err;
    Settings st = s;
    st.seed = (unsigned)SDL_GetTicks() * 2654435761u + 17u;
    if (!game.newGame(st, mapsDir_, &err)) {
        showToast(err);
        return false;
    }
    gfx.setFlagOwner(game.humanId);
    gfx.setMap(game);
    game.changed.clear();
    screen = SCR_GAME;
    overlay = OV_NONE;
    buildMode_ = -1;
    selBuildingTile_ = -1;
    speed_ = 1;
    simAcc_ = 0;
    gameOverSeen_ = false;
    fitMap();
    lastAutosave_ = SDL_GetTicks();
    return true;
}

bool App::loadSlot(const std::string& slot) {
    std::string err;
    Game g;
    if (!g.loadFromFile(slotPath(slot), mapsDir_, &err)) {
        loadMessage_ = err;
        return false;
    }
    game = g;
    gfx.setFlagOwner(game.humanId);
    gfx.setMap(game);
    game.changed.clear();
    screen = SCR_GAME;
    overlay = OV_NONE;
    buildMode_ = -1;
    selBuildingTile_ = -1;
    speed_ = 1;
    simAcc_ = 0;
    gameOverSeen_ = game.over;
    if (game.spawnPhase || !game.players[game.humanId].alive) fitMap();
    else focusOnHuman(std::max(2.0f, zoom_));
    lastAutosave_ = SDL_GetTicks();
    showToast("Spel geladen");
    return true;
}

bool App::saveSlot(const std::string& slot) {
    if (game.map.w == 0) return false;
    return game.saveToFile(slotPath(slot));
}

void App::autosave() {
    if (netMode_ == NET_OFF && screen == SCR_GAME && game.map.w && !game.spawnPhase) saveSlot("autosave");
    lastAutosave_ = SDL_GetTicks();
}

// ------------------------------------------------------------------ camera

void App::fitMap() {
    if (!game.map.w) return;
    zoom_ = std::min((float)W_ / game.map.w, (float)(H_ - 140) / game.map.h);
    camX_ = game.map.w * 0.5f;
    camY_ = game.map.h * 0.5f - 10 / zoom_;
}

void App::focusOnHuman(float z) {
    const Player& me = game.players[game.humanId];
    zoom_ = z;
    camX_ = me.cx;
    camY_ = me.cy;
    clampCamera();
}

void App::clampCamera() {
    if (!game.map.w) return;
    float minZoom = std::min((float)W_ / game.map.w, (float)H_ / game.map.h) * 0.8f;
    zoom_ = std::max(minZoom, std::min(28.0f, zoom_));
    camX_ = std::max(0.0f, std::min((float)game.map.w, camX_));
    camY_ = std::max(0.0f, std::min((float)game.map.h, camY_));
}

void App::zoomAt(float x, float y, float factor) {
    float tx = (x - W_ * 0.5f) / zoom_ + camX_, ty = (y - H_ * 0.5f) / zoom_ + camY_;
    zoom_ *= factor;
    clampCamera();
    camX_ = tx - (x - W_ * 0.5f) / zoom_;
    camY_ = ty - (y - H_ * 0.5f) / zoom_;
    clampCamera();
}

int App::screenToTile(float x, float y) const {
    float tx = (x - W_ * 0.5f) / zoom_ + camX_, ty = (y - H_ * 0.5f) / zoom_ + camY_;
    if (tx < 0 || ty < 0 || tx >= game.map.w || ty >= game.map.h) return -1;
    return (int)ty * game.map.w + (int)tx;
}

// ------------------------------------------------------------------ input

bool App::overUI(float x, float y) const {
    for (size_t k = 0; k < blocksPrev_.size(); ++k) {
        const SDL_Rect& r = blocksPrev_[k];
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true;
    }
    return false;
}

void App::pointerDown(float x, float y) {
    downX_ = x;
    downY_ = y;
    downTime_ = SDL_GetTicks();
    moved_ = false;
    downOnUI_ = screen != SCR_GAME || overlay != OV_NONE || overUI(x, y);
}

void App::pointerMove(float x, float y, float dx, float dy) {
    if (std::fabs(x - downX_) + std::fabs(y - downY_) > 12) moved_ = true;
    if (screen == SCR_GAME && overlay == OV_NONE && !downOnUI_ && moved_) {
        camX_ -= dx / zoom_;
        camY_ -= dy / zoom_;
        clampCamera();
    }
}

void App::pointerUp(float x, float y) {
    if (!moved_ && !multi_ && SDL_GetTicks() - downTime_ < 700) {
        tapPending_ = true;
        tapX_ = x;
        tapY_ = y;
    }
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_FINGERDOWN: {
            Touch t = {e.tfinger.x * W_, e.tfinger.y * H_};
            touches_[e.tfinger.fingerId] = t;
            if (touches_.size() == 1) {
                multi_ = false;
                pointerDown(t.x, t.y);
            } else {
                multi_ = true;
                if (touches_.size() == 2) {
                    std::map<SDL_FingerID, Touch>::iterator a = touches_.begin(), b = a;
                    ++b;
                    pinchDist_ = std::sqrt((a->second.x - b->second.x) * (a->second.x - b->second.x) +
                                           (a->second.y - b->second.y) * (a->second.y - b->second.y));
                    pinchMidX_ = (a->second.x + b->second.x) * 0.5f;
                    pinchMidY_ = (a->second.y + b->second.y) * 0.5f;
                }
            }
            break;
        }
        case SDL_FINGERMOTION: {
            std::map<SDL_FingerID, Touch>::iterator it = touches_.find(e.tfinger.fingerId);
            if (it == touches_.end()) break;
            Touch old = it->second;
            it->second.x = e.tfinger.x * W_;
            it->second.y = e.tfinger.y * H_;
            if (touches_.size() == 1 && !multi_) {
                pointerMove(it->second.x, it->second.y, it->second.x - old.x, it->second.y - old.y);
            } else if (touches_.size() == 2 && screen == SCR_GAME && overlay == OV_NONE) {
                std::map<SDL_FingerID, Touch>::iterator a = touches_.begin(), b = a;
                ++b;
                float d = std::sqrt((a->second.x - b->second.x) * (a->second.x - b->second.x) +
                                    (a->second.y - b->second.y) * (a->second.y - b->second.y));
                float mx = (a->second.x + b->second.x) * 0.5f, my = (a->second.y + b->second.y) * 0.5f;
                camX_ -= (mx - pinchMidX_) / zoom_;
                camY_ -= (my - pinchMidY_) / zoom_;
                if (pinchDist_ > 10 && d > 10) zoomAt(mx, my, d / pinchDist_);
                pinchDist_ = d;
                pinchMidX_ = mx;
                pinchMidY_ = my;
                clampCamera();
            }
            break;
        }
        case SDL_FINGERUP: {
            std::map<SDL_FingerID, Touch>::iterator it = touches_.find(e.tfinger.fingerId);
            if (it == touches_.end()) break;
            float x = e.tfinger.x * W_, y = e.tfinger.y * H_;
            touches_.erase(it);
            if (touches_.empty()) pointerUp(x, y);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
            mouseDown_ = true;
            multi_ = false;
            pointerDown((e.button.x - ptrOffX_) * ptrScale_, (e.button.y - ptrOffY_) * ptrScale_);
            break;
        case SDL_MOUSEMOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID || !mouseDown_) break;
            pointerMove((e.motion.x - ptrOffX_) * ptrScale_, (e.motion.y - ptrOffY_) * ptrScale_,
                        e.motion.xrel * ptrScale_, e.motion.yrel * ptrScale_);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
            mouseDown_ = false;
            pointerUp((e.button.x - ptrOffX_) * ptrScale_, (e.button.y - ptrOffY_) * ptrScale_);
            break;
        case SDL_MOUSEWHEEL:
            if (screen == SCR_GAME && overlay == OV_NONE && e.wheel.which != SDL_TOUCH_MOUSEID) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                zoomAt((mx - ptrOffX_) * ptrScale_, (my - ptrOffY_) * ptrScale_, e.wheel.y > 0 ? 1.2f : 1 / 1.2f);
            }
            break;
        case SDL_TEXTINPUT:
            for (const char* c = e.text.text; *c; ++c) typeChar(*c);
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_BACKSPACE) typeChar('\b');
            if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK) {
                if (screen == SCR_GAME) overlay = overlay == OV_PAUSE ? OV_NONE : OV_PAUSE;
                else if (screen != SCR_MENU && screen != SCR_LOBBY) screen = SCR_MENU;
            }
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------ widgets

void App::block(float x, float y, float w, float h) {
    SDL_Rect r = {(int)x, (int)y, (int)w, (int)h};
    blocks_.push_back(r);
}

bool App::hit(float x, float y, float w, float h) {
    if (!tapPending_) return false;
    if (tapX_ >= x && tapY_ >= y && tapX_ < x + w && tapY_ < y + h) {
        tapPending_ = false;
        return true;
    }
    return false;
}

void App::panel(float x, float y, float w, float h) {
    gfx.rect(x, y, w, h, C_PANEL);
    gfx.frame(x, y, w, h, 1, 0x3A4A66FF);
    block(x, y, w, h);
}

bool App::button(float x, float y, float w, float h, const std::string& label, bool selected, bool enabled,
                 const std::string& sub) {
    gfx.rect(x, y, w, h, !enabled ? C_BTN_OFF : (selected ? C_BTN_SEL : C_BTN));
    gfx.frame(x, y, w, h, 1, selected ? 0x9CC3FFFF : 0x44587AFF);
    float size = h >= 60 ? 24 : (h >= 44 ? 20 : 17);
    uint32_t tc = enabled ? C_TEXT : 0x6B7689FF;
    if (sub.empty()) {
        gfx.text(x + w * 0.5f, y + (h - size * 1.05f) * 0.5f, label, size, tc, 1);
    } else {
        gfx.text(x + w * 0.5f, y + h * 0.5f - size * 1.0f, label, size, tc, 1);
        gfx.text(x + w * 0.5f, y + h * 0.5f + 2, sub, size * 0.72f, enabled ? C_DIM : 0x6B7689FF, 1);
    }
    block(x, y, w, h);
    if (uiLocked_) return false;
    return hit(x, y, w, h) && enabled;
}

void App::showToast(const std::string& s) {
    toast_ = s;
    toastUntil_ = SDL_GetTicks() + 2600;
}

// ------------------------------------------------------------------ update

void App::update(double dt) {
    // ---- multiplayer plumbing
    if (netMode_ == NET_HOST) {
        if (screen == SCR_LOBBY) host_.pumpLobby(lobbyInfo());
        else if (screen == SCR_GAME) host_.pumpGame(game);
    } else if (netMode_ == NET_CLIENT) {
        if (client_.pump(game, mapsDir_)) enterGameScreen();
        std::string t;
        while (client_.nextToast(t)) showToast(t);
        if (client_.state() == ClientSession::CLOSED) {
            std::string why = client_.closeReason();
            leaveMultiplayer();
            screen = SCR_MP;
            overlay = OV_NONE;
            showToast(why.empty() ? "Verbinding verbroken" : why);
        }
    }
    if (screen != SCR_GAME || !game.map.w) {
        simAcc_ = 0;
        return;
    }
    const Player& me = game.players[game.humanId];
    bool finished = game.over || (netMode_ != NET_OFF && me.spawned && !me.alive);
    if (netMode_ == NET_CLIENT) {  // the host runs the simulation
        if (finished && !gameOverSeen_) {
            gameOverSeen_ = true;
            overlay = OV_GAMEOVER;
        }
        return;
    }
    // single player pauses under a menu; a hosted game keeps running
    bool paused = netMode_ == NET_OFF && overlay != OV_NONE;
    if (paused || game.spawnPhase) {
        simAcc_ = 0;
    } else {
        simAcc_ += dt * (netMode_ == NET_HOST ? 1 : speed_);
        int steps = 0;
        while (simAcc_ >= 1.0 / TICKS_PER_SECOND && steps < 4) {
            game.step();
            simAcc_ -= 1.0 / TICKS_PER_SECOND;
            ++steps;
        }
        if (steps == 4) simAcc_ = 0;  // device too slow for this speed: drop the backlog
    }
    if (netMode_ == NET_HOST) host_.broadcast(game);
    if (finished && !gameOverSeen_) {
        gameOverSeen_ = true;
        overlay = OV_GAMEOVER;
        autosave();
    }
    if (netMode_ == NET_OFF && !paused && SDL_GetTicks() - lastAutosave_ > 60000) autosave();
}

// ------------------------------------------------------------------ multiplayer

void App::enterGameScreen() {
    gfx.setFlagOwner(game.humanId);
    gfx.setMap(game);
    game.changed.clear();
    screen = SCR_GAME;
    overlay = OV_NONE;
    buildMode_ = -1;
    selBuildingTile_ = -1;
    speed_ = 1;
    simAcc_ = 0;
    gameOverSeen_ = false;
    wasSpawned_ = false;
    fitMap();
    lastAutosave_ = SDL_GetTicks();
}

std::string App::lobbyInfo() const {
    std::string name = newSettings.mapId;
    for (size_t k = 0; k < maps_.size(); ++k)
        if (maps_[k].id == newSettings.mapId) name = maps_[k].name;
    char buf[160];
    snprintf(buf, sizeof buf, "%s  -  %s  -  %d AI (%s)", name.c_str(), modeName(newSettings.mode), newSettings.numAI,
             difficultyName(newSettings.difficulty));
    return buf;
}

int App::portNumber() const {
    int p = atoi(portText_.c_str());
    return p >= 1024 && p <= 65535 ? p : kDefaultPort;
}

bool App::hostLobby(int port, std::string* err) {
    leaveMultiplayer();
    if (!host_.start(port, err)) return false;
    netMode_ = NET_HOST;
    screen = SCR_LOBBY;
    return true;
}

bool App::hostStart() {
    if (netMode_ != NET_HOST) return false;
    Settings st = newSettings;
    st.numHumans = 1 + host_.clientCount();
    st.seed = (unsigned)SDL_GetTicks() * 2654435761u + 17u;
    std::string err;
    if (!game.newGame(st, mapsDir_, &err)) {
        showToast(err);
        return false;
    }
    host_.startGame(game);
    enterGameScreen();
    return true;
}

bool App::joinGame(const std::string& ip, int port, std::string* err) {
    leaveMultiplayer();
    if (!client_.connect(ip, port, err)) return false;
    netMode_ = NET_CLIENT;
    screen = SCR_LOBBY;
    return true;
}

void App::leaveMultiplayer() {
    if (netMode_ == NET_HOST) host_.stop();
    if (netMode_ == NET_CLIENT) client_.close();
    netMode_ = NET_OFF;
}

void App::actSpawn(int t) {
    if (game.players[game.humanId].spawned) return;
    if (netMode_ == NET_CLIENT) {
        if (game.canSpawnAt(t)) client_.sendOrder(ORD_SPAWN, t, 0, 0);
        else showToast("Kies vrij land (geen water of bezet gebied)");
        return;
    }
    if (game.spawnHuman(t)) {
        if (netMode_ == NET_OFF) autosave();
    } else {
        showToast("Kies vrij land (geen water of bezet gebied)");
    }
}

void App::actAttack(int t) {
    if (netMode_ == NET_CLIENT) {
        client_.sendOrder(ORD_ATTACK, t, 0, kAttackPct[attackIdx_]);
        return;
    }
    std::string why;
    double troops = game.players[game.humanId].troops * kAttackPct[attackIdx_];
    if (!game.orderAttack(game.humanId, t, troops, &why)) showToast(why);
}

void App::actBuild(int type, int t) {
    if (netMode_ == NET_CLIENT) {
        client_.sendOrder(ORD_BUILD, type, t, 0);
        buildMode_ = -1;
        return;
    }
    std::string why;
    if (game.build(game.humanId, type, t, &why)) buildMode_ = -1;
    else showToast(why);
}

void App::actUpgrade(int bi) {
    if (bi < 0 || bi >= (int)game.buildings.size()) return;
    if (netMode_ == NET_CLIENT) {
        client_.sendOrder(ORD_UPGRADE, game.buildings[bi].tile, 0, 0);
        return;
    }
    std::string why;
    if (!game.upgrade(game.humanId, bi, &why)) showToast(why);
}

void App::actRecruit() {
    if (netMode_ == NET_CLIENT) {
        client_.sendOrder(ORD_RECRUIT, 0, 0, 0);
        return;
    }
    std::string why;
    if (game.recruit(game.humanId, &why)) showToast("+1500 troepen");
    else showToast(why);
}

void App::typeChar(char c) {
    if (screen != SCR_JOIN && screen != SCR_MP) return;
    std::string& target = (screen == SCR_JOIN && field_ == 0) ? joinIp_ : portText_;
    bool ip = &target == &joinIp_;
    if (c == '\b') {
        if (!target.empty()) target.erase(target.size() - 1);
    } else if (((c >= '0' && c <= '9') || (ip && c == '.')) && target.size() < (ip ? 15u : 5u)) {
        target += c;
    }
}

void App::keypad(float x, float y, std::string& target, size_t maxLen, bool dot) {
    static const char keys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '<'};
    for (int k = 0; k < 12; ++k) {
        char label[2] = {keys[k], 0};
        bool enabled = keys[k] != '.' || dot;
        if (button(x + (k % 3) * 96.0f, y + (k / 3) * 74.0f, 88, 66, keys[k] == '<' ? "Wis" : label, false, enabled)) {
            if (keys[k] == '<') {
                if (!target.empty()) target.erase(target.size() - 1);
            } else if (target.size() < maxLen) {
                target += keys[k];
            }
        }
    }
}

void App::renderMP() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 24, "Multiplayer", 40, C_TEXT, 1);
    gfx.text(W_ * 0.5f, 84, "Speel samen via internet: iPad en Windows door elkaar, tot 8 spelers.", 20, C_DIM, 1);

    float x = 70, y = 150;
    gfx.text(x, y, "Spel hosten", 24, C_ACCENT);
    gfx.text(x, y + 34, "Jij start het spel. Stuur in je router de TCP-poort", 18, C_TEXT);
    gfx.text(x, y + 58, "hiernaast door naar dit apparaat (port forwarding) en", 18, C_TEXT);
    gfx.text(x, y + 82, "geef je publieke IP-adres + poort aan je medespelers.", 18, C_TEXT);
    if (button(x, y + 122, 420, 70, "Spel hosten", true)) {
        mpSetup_ = true;
        screen = SCR_NEW;
    }
    y += 250;
    gfx.text(x, y, "Meedoen", 24, C_ACCENT);
    gfx.text(x, y + 34, "Vul het IP-adres en de poort van de host in.", 18, C_TEXT);
    if (button(x, y + 74, 420, 70, "Meedoen met een spel", true)) {
        field_ = 0;
        screen = SCR_JOIN;
    }

    float kx = 640, ky = 150;
    gfx.text(kx, ky, "Poort om te hosten", 20, C_ACCENT);
    gfx.rect(kx, ky + 32, 280, 56, 0x1B2638FF);
    gfx.frame(kx, ky + 32, 280, 56, 2, C_BTN_SEL);
    gfx.text(kx + 140, ky + 44, portText_.empty() ? "47800" : portText_, 28, portText_.empty() ? C_DIM : C_TEXT, 1);
    keypad(kx, ky + 104, portText_, 5, false);
    gfx.text(kx, ky + 408, "Standaard: 47800 (1024 - 65535)", 16, C_DIM);

    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = SCR_MENU;
}

void App::renderJoin() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 24, "Meedoen met een spel", 40, C_TEXT, 1);
    float x = 90, y = 130;
    gfx.text(x, y, "IP-adres van de host", 20, C_ACCENT);
    if (button(x, y + 30, 440, 64, joinIp_.empty() ? "bijv. 84.25.130.7" : joinIp_, field_ == 0)) field_ = 0;
    gfx.text(x, y + 120, "Poort", 20, C_ACCENT);
    if (button(x, y + 150, 440, 64, portText_.empty() ? "47800" : portText_, field_ == 1)) field_ = 1;
    gfx.text(x, y + 240, "Tik op een veld en gebruik de cijfertoetsen.", 18, C_DIM);
    gfx.text(x, y + 266, "De host moet in de lobby wachten voordat je verbindt.", 18, C_DIM);

    if (field_ == 0) keypad(640, 150, joinIp_, 15, true);
    else keypad(640, 150, portText_, 5, false);

    if (button(x, y + 320, 440, 76, "Verbinden", true, !joinIp_.empty())) {
        std::string err;
        saveSettings();
        if (!joinGame(joinIp_, portNumber(), &err)) showToast(err);
    }
    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = SCR_MP;
}

void App::renderLobby() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    char buf[200];
    if (netMode_ == NET_HOST) {
        gfx.text(W_ * 0.5f, 24, "Lobby - jij bent de host", 40, C_TEXT, 1);
        gfx.text(W_ * 0.5f, 86, lobbyInfo(), 20, C_DIM, 1);
        float x = 110, y = 150;
        snprintf(buf, sizeof buf, "Poort: %d (TCP)", host_.port());
        gfx.text(x, y, buf, 30, C_ACCENT);
        gfx.text(x, y + 50, "Zo kunnen anderen meedoen:", 20, C_TEXT);
        gfx.text(x, y + 80, "1. Stuur deze poort in je router door naar dit apparaat (port forwarding).", 18, C_TEXT);
        gfx.text(x, y + 106, "2. Geef je publieke IP-adres (zoek op: wat is mijn ip) en de poort door.", 18, C_TEXT);
        gfx.text(x, y + 132, "3. Zij kiezen Multiplayer > Meedoen en vullen die twee in.", 18, C_TEXT);
        std::vector<std::string> ips = netLocalAddresses();
        std::string local;
        for (size_t k = 0; k < ips.size() && k < 4; ++k) local += (k ? ",  " : "") + ips[k];
        gfx.text(x, y + 176, "Adres van dit apparaat in je eigen netwerk (voor de router of thuis):", 16, C_DIM);
        gfx.text(x, y + 200, local.empty() ? "(geen netwerk gevonden)" : local, 22, C_TEXT);

        snprintf(buf, sizeof buf, "Spelers in de lobby: %d", 1 + host_.clientCount());
        gfx.text(x, y + 260, buf, 30, C_GOOD);
        gfx.text(x, y + 300, "Jij + iedereen die verbonden is. Lege plekken worden AI.", 18, C_DIM);

        if (button(W_ - 380.0f, H_ - 90.0f, 340, 66, "Start spel", true)) hostStart();
        if (button(40, H_ - 90.0f, 220, 66, "Annuleren")) {
            leaveMultiplayer();
            screen = SCR_MP;
        }
    } else {
        gfx.text(W_ * 0.5f, 24, "Lobby", 40, C_TEXT, 1);
        snprintf(buf, sizeof buf, "%s : %d", joinIp_.c_str(), portNumber());
        gfx.text(W_ * 0.5f, 110, buf, 24, C_DIM, 1);
        if (client_.state() == ClientSession::CONNECTING || client_.lobbyCount() == 0) {
            gfx.text(W_ * 0.5f, 260, "Verbinden met de host...", 30, C_ACCENT, 1);
            gfx.text(W_ * 0.5f, 310, "Lukt het niet? Controleer IP, poort en de port forwarding bij de host.", 18, C_DIM, 1);
        } else {
            gfx.text(W_ * 0.5f, 240, "Verbonden!", 36, C_GOOD, 1);
            snprintf(buf, sizeof buf, "Jij bent speler %d van %d", client_.lobbySlot(), client_.lobbyCount());
            gfx.text(W_ * 0.5f, 300, buf, 26, C_TEXT, 1);
            gfx.text(W_ * 0.5f, 350, client_.lobbyInfo(), 20, C_DIM, 1);
            gfx.text(W_ * 0.5f, 400, "Wachten tot de host het spel start...", 22, C_ACCENT, 1);
        }
        if (button(40, H_ - 90.0f, 220, 66, "Verlaten")) {
            leaveMultiplayer();
            screen = SCR_JOIN;
        }
    }
}

// ------------------------------------------------------------------ render

void App::render(int winW, int winH, int drawW, int drawH, int vpX, int vpY) {
    W_ = winW;
    H_ = winH;
    blocks_.clear();
    if (fc_flag_picked() && loadFlag()) showToast("Je vlag is ingesteld");
    gfx.beginFrame(winW, winH, drawW, drawH, vpX, vpY);
    switch (screen) {
        case SCR_MENU: renderMenu(); break;
        case SCR_NEW: renderNew(); break;
        case SCR_LOAD: renderLoad(); break;
        case SCR_INFO: renderInfo(); break;
        case SCR_GAME: renderGame(); break;
        case SCR_FLAG: renderFlag(); break;
        case SCR_MP: renderMP(); break;
        case SCR_JOIN: renderJoin(); break;
        case SCR_LOBBY: renderLobby(); break;
    }
    if (!toast_.empty() && SDL_GetTicks() < toastUntil_) {
        float w = gfx.textWidth(toast_, 20) + 40;
        float y = screen == SCR_GAME ? 120.0f : (float)H_ - 120;
        gfx.rect((W_ - w) * 0.5f, y, w, 40, 0x000000C8);
        gfx.text(W_ * 0.5f, y + 9, toast_, 20, C_TEXT, 1);
    }
    gfx.endFrame();
    // the song plays while a game is open (also when paused), not in the menus
    fc_music_set_playing(screen == SCR_GAME && musicOn_ ? 1 : 0);
    tapPending_ = false;  // a tap nobody used is dropped
    blocksPrev_.swap(blocks_);
}

void App::renderMenu() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    // decorative coloured territory squares
    static const uint32_t cols[5] = {0xD64541, 0x3A7BD5, 0x2ECC71, 0xF1C40F, 0x9B59B6};
    for (int k = 0; k < 70; ++k) {
        uint32_t h = (uint32_t)(k + 1) * 2654435761u;
        float x = (float)(h % (uint32_t)W_), y = (float)((h >> 10) % (uint32_t)H_);
        gfx.rect(x, y, 34, 34, rgbToRgba(cols[k % 5], 40));
    }
    gfx.text(W_ * 0.5f, 110, "FRONTIER CLASSIC", 64, C_TEXT, 1);
    gfx.text(W_ * 0.5f, 186, "Verover de wereld - volledig offline", 24, C_DIM, 1);

    float bw = 360, bh = 58, x = (W_ - bw) * 0.5f, y = 236;
    const float gap = 12;
    bool hasAuto = false;
    {
        FILE* f = fopen(slotPath("autosave").c_str(), "rb");
        if (f) { hasAuto = true; fclose(f); }
    }
    if (hasAuto) {
        std::string sum;
        Game::readSummary(slotPath("autosave"), sum);
        if (button(x, y, bw, bh, "Doorgaan", true, true, sum)) {
            if (!loadSlot("autosave")) showToast(loadMessage_);
        }
        y += bh + gap;
    }
    if (button(x, y, bw, bh, "Nieuw spel")) { mpSetup_ = false; screen = SCR_NEW; }
    y += bh + gap;
    if (button(x, y, bw, bh, "Multiplayer")) screen = SCR_MP;
    y += bh + gap;
    if (button(x, y, bw, bh, "Spel laden")) { loadReturn_ = SCR_MENU; loadMessage_.clear(); screen = SCR_LOAD; }
    y += bh + gap;
    if (button(x, y, bw, bh, "Jouw vlag")) screen = SCR_FLAG;
    y += bh + gap;
    if (button(x, y, bw, bh, "Info en besturing")) screen = SCR_INFO;
    gfx.text(W_ * 0.5f, H_ - 44.0f, "Speelt volledig offline  -  internet is alleen nodig voor multiplayer", 18, 0x7F8BA0FF, 1);
}

void App::renderNew() {
    Settings& s = newSettings;
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 18, mpSetup_ ? "Multiplayer: spel instellen" : "Nieuw spel", 40, C_TEXT, 1);
    float x0 = 40, y = 82;

    gfx.text(x0, y, "Kaart", 22, C_ACCENT);
    y += 32;
    for (size_t k = 0; k < maps_.size(); ++k) {
        float bx = x0 + (k % 3) * 318, by = y + (k / 3) * 62;
        if (button(bx, by, 306, 54, maps_[k].name, maps_[k].id == s.mapId)) s.mapId = maps_[k].id;
    }
    y += 2 * 62 + 14;

    gfx.text(x0, y, "Spelmodus", 22, C_ACCENT);
    y += 32;
    for (int m = 0; m < MODE_COUNT; ++m)
        if (button(x0 + m * 190, y, 180, 54, modeName(m), s.mode == m)) s.mode = m;
    y += 62;
    static const char* modeHelp[MODE_COUNT] = {
        "Iedereen tegen iedereen. Bezit genoeg land om te winnen.",
        "Spelers in teams. Het team met genoeg land wint.",
        "Verover de hele kaart: versla alle tegenstanders.",
        "Vrij spelen: veel goud en troepen, geen einde.",
        "Kies zelf alle instellingen.",
    };
    gfx.text(x0, y, modeHelp[s.mode], 18, C_DIM);
    y += 34;

    // numbers
    float col2 = x0 + 470;
    gfx.text(x0, y + 14, "AI-tegenstanders", 22, C_TEXT);
    if (button(x0 + 210, y, 54, 50, "-")) s.numAI = std::max(10, s.numAI - 5);
    char buf[64];
    snprintf(buf, sizeof buf, "%d", s.numAI);
    gfx.text(x0 + 310, y + 12, buf, 26, C_ACCENT, 1);
    if (button(x0 + 356, y, 54, 50, "+")) s.numAI = std::min(50, s.numAI + 5);

    gfx.text(col2, y + 14, "AI-niveau", 22, C_TEXT);
    for (int d = 0; d < 3; ++d)
        if (button(col2 + 110 + d * 124, y, 118, 50, difficultyName(d), s.difficulty == d)) s.difficulty = d;
    y += 64;

    bool showTeams = s.mode == MODE_TEAMS || (s.mode == MODE_CUSTOM && s.customTeams);
    if (s.mode == MODE_CUSTOM) {
        gfx.text(x0, y + 14, "Teams", 22, C_TEXT);
        if (button(x0 + 210, y, 200, 50, s.customTeams ? "Aan" : "Uit", s.customTeams)) s.customTeams = !s.customTeams;
    }
    if (showTeams) {
        gfx.text(col2, y + 14, "Aantal teams", 22, C_TEXT);
        for (int t = 2; t <= 4; ++t) {
            snprintf(buf, sizeof buf, "%d", t);
            if (button(col2 + 160 + (t - 2) * 70, y, 62, 50, buf, s.numTeams == t)) s.numTeams = t;
        }
    }
    if (s.mode == MODE_SANDBOX) {
        gfx.text(x0, y + 14, "AI valt aan", 22, C_TEXT);
        if (button(x0 + 210, y, 200, 50, s.aiAttacks ? "Ja" : "Nee (vredig)", s.aiAttacks)) s.aiAttacks = !s.aiAttacks;
    }
    if (showTeams || s.mode == MODE_CUSTOM || s.mode == MODE_SANDBOX) y += 64;

    if (s.mode == MODE_CUSTOM) {
        gfx.text(x0, y + 14, "Startgoud", 22, C_TEXT);
        if (button(x0 + 210, y, 54, 50, "-")) s.startGold = std::max(0, s.startGold - 1000);
        gfx.text(x0 + 310, y + 12, formatNumber(s.startGold), 26, C_ACCENT, 1);
        if (button(x0 + 356, y, 54, 50, "+")) s.startGold = std::min(50000, s.startGold + 1000);

        gfx.text(col2, y + 14, "Winnen bij", 22, C_TEXT);
        if (button(col2 + 160, y, 54, 50, "-")) s.winPercent = std::max(30, s.winPercent - 5);
        snprintf(buf, sizeof buf, "%d%% land", s.winPercent);
        gfx.text(col2 + 280, y + 12, buf, 26, C_ACCENT, 1);
        if (button(col2 + 348, y, 54, 50, "+")) s.winPercent = std::min(100, s.winPercent + 5);
        y += 64;
        gfx.text(x0, y + 14, "Troepengroei", 22, C_TEXT);
        if (button(x0 + 210, y, 54, 50, "-")) s.troopSpeed = std::max(50, s.troopSpeed - 25);
        snprintf(buf, sizeof buf, "%d%%", s.troopSpeed);
        gfx.text(x0 + 310, y + 12, buf, 26, C_ACCENT, 1);
        if (button(x0 + 356, y, 54, 50, "+")) s.troopSpeed = std::min(300, s.troopSpeed + 25);
    } else {
        s.startGold = 1000;  // sandbox: the human gets extra gold in Game::setupPlayers
        s.winPercent = 80;
        s.troopSpeed = 100;
    }

    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = mpSetup_ ? SCR_MP : SCR_MENU;
    if (mpSetup_) {
        if (button(W_ - 340.0f, H_ - 90.0f, 300, 66, "Open lobby", true)) {
            saveSettings();
            std::string err;
            if (!hostLobby(portNumber(), &err)) showToast(err);
        }
    } else if (button(W_ - 340.0f, H_ - 90.0f, 300, 66, "Start", true)) {
        saveSettings();
        startGame(s);
    }
}

void App::renderLoad() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 30, "Spel laden", 40, C_TEXT, 1);
    float x = 150, y = 120, w = W_ - 300.0f;
    for (int k = 0; k < 4; ++k) {
        std::string sum;
        bool has = Game::readSummary(slotPath(kSlots[k]), sum);
        if (button(x, y, w, 80, kSlotNames[k], false, has, has ? sum : "(leeg)")) {
            if (!loadSlot(kSlots[k])) showToast(loadMessage_);
        }
        y += 96;
    }
    if (!loadMessage_.empty()) gfx.text(W_ * 0.5f, y + 10, loadMessage_, 20, C_BAD, 1);
    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = loadReturn_;
}

void App::renderInfo() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 24, "Info en besturing", 40, C_TEXT, 1);
    static const char* lines[] = {
        "Doel: breid je land uit en versla de andere naties.",
        "",
        "Tik op een gebied naast je land: je troepen vallen aan.",
        "Ligt het over zee, dan vertrekt er een schip (max 3 tegelijk).",
        "De knoppen 10% - 100% bepalen hoeveel troepen je stuurt.",
        "Slepen met 1 vinger verschuift de kaart, knijpen zoomt.",
        "",
        "Troepen groeien vanzelf; meer land = snellere groei.",
        "Stad: meer troepen.  Markt: meer goud.  Mijn: grondstoffen (overal te bouwen).",
        "Fort: sterke verdediging in de buurt.  Tik op een gebouw om te verbeteren.",
        "Rekruteren: 1000 goud voor 1500 extra troepen.",
        "",
        "Het spel slaat elke minuut automatisch op. Opslaan staat in het menu.",
        "Geen account en geen advertenties. Internet is alleen nodig voor multiplayer.",
        "",
        "Kaarten: Natural Earth (publiek domein). Lettertype: Aileron (CC0).",
        "Gebouwd met SDL2 (zlib-licentie) en OpenGL ES 2.0. Muziek: Holding The Ridge.",
    };
    float y = 100;
    for (size_t k = 0; k < sizeof lines / sizeof lines[0]; ++k) {
        gfx.text(70, y, lines[k], 21, k == 0 ? C_ACCENT : C_TEXT);
        y += lines[k][0] ? 32 : 14;
    }
    gfx.text(300, H_ - 82.0f, saveOk_ ? "Opslaan werkt. Map:" : "OPSLAAN WERKT NIET. Map:", 16, saveOk_ ? C_GOOD : C_BAD);
    gfx.text(300, H_ - 58.0f, saveDir_, 14, C_DIM);
    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = SCR_MENU;
}

// ------------------------------------------------------------------ flag

bool App::loadFlag() {
    int w = 0, h = 0;
    unsigned char* px = fc_flag_decode((saveDir_ + "flag.png").c_str(), 256, &w, &h);
    if (!px) return false;
    setFlagPixels(px, w, h);
    free(px);
    return true;
}

void App::setFlagPixels(const uint8_t* rgba, int w, int h) {
    gfx.setFlag(rgba, w, h);
    if (game.map.w) gfx.refreshMap(game);
}

void App::removeFlag() {
    remove((saveDir_ + "flag.png").c_str());
    gfx.setFlag(0, 0, 0);
    if (game.map.w) gfx.refreshMap(game);
}

// Where the flag is drawn: your territory's bounding box, filled without
// stretching the flag out of shape (the edges may be cut off).
bool App::flagRect(float out[4]) const {
    if (!gfx.hasFlag() || !game.map.w) return false;
    const Player& me = game.players[game.humanId];
    if (!me.alive || me.bx1 < me.bx0) return false;
    float bw = (float)(me.bx1 - me.bx0 + 1), bh = (float)(me.by1 - me.by0 + 1);
    float aspect = (float)gfx.flagW() / std::max(1, gfx.flagH());
    float w = bw, h = bh;
    if (bw / bh > aspect) h = bw / aspect;
    else w = bh * aspect;
    out[0] = me.bx0 + bw * 0.5f - w * 0.5f;
    out[1] = me.by0 + bh * 0.5f - h * 0.5f;
    out[2] = w;
    out[3] = h;
    return true;
}

void App::renderFlag() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x0F1A2BFF);
    gfx.text(W_ * 0.5f, 24, "Jouw vlag", 40, C_TEXT, 1);
    gfx.text(W_ * 0.5f, 84, "Je eigen land wordt in het spel met deze vlag gevuld.", 20, C_DIM, 1);

    // preview
    float pw = 420, ph = 280, px = (W_ - pw) * 0.5f, py = 130;
    gfx.rect(px - 4, py - 4, pw + 8, ph + 8, 0x3A4A66FF);
    gfx.rect(px, py, pw, ph, 0x1B2638FF);
    if (gfx.hasFlag()) {
        float a = (float)gfx.flagW() / std::max(1, gfx.flagH());
        float w = pw, h = pw / a;
        if (h > ph) { h = ph; w = ph * a; }
        gfx.image(px + (pw - w) * 0.5f, py + (ph - h) * 0.5f, w, h);
    } else {
        gfx.text(W_ * 0.5f, py + ph * 0.5f - 14, "Nog geen vlag gekozen", 24, C_DIM, 1);
    }

    float bw = 300, by = py + ph + 30;
    if (button(W_ * 0.5f - bw - 10, by, bw, 66, "Kies afbeelding", true)) {
        if (!fc_flag_pick((saveDir_ + "flag.png").c_str()))
            showToast("Fotobibliotheek is hier niet beschikbaar");
    }
    if (button(W_ * 0.5f + 10, by, bw, 66, "Vlag verwijderen", false, gfx.hasFlag())) {
        removeFlag();
        showToast("Vlag verwijderd");
    }
    by += 90;
    gfx.text(W_ * 0.5f, by, "Of zet zelf een bestand flag.png in de map Documents van de app", 18, C_TEXT, 1);
    gfx.text(W_ * 0.5f, by + 26, "(iTunes > Bestandsdeling, of Filza) en tik op Opnieuw laden.", 18, C_TEXT, 1);
    gfx.text(W_ * 0.5f, by + 54, saveDir_ + "flag.png", 14, C_DIM, 1);
    if (button(W_ * 0.5f - 150, by + 82, 300, 56, "Opnieuw laden")) {
        showToast(loadFlag() ? "Vlag geladen" : "Geen flag.png gevonden");
    }
    if (button(40, H_ - 90.0f, 220, 66, "Terug")) screen = SCR_MENU;
}

// ------------------------------------------------------------------ game screen

void App::renderGame() {
    {
        const Player& me = game.players[game.humanId];
        if (me.spawned && !wasSpawned_ && me.alive && me.tiles > 0) focusOnHuman(std::max(zoom_, 4.0f));
        wasSpawned_ = me.spawned;
    }
    gfx.updateMap(game);
    gfx.rect(0, 0, (float)W_, (float)H_, 0x121C2BFF);
    float fr[4];
    gfx.drawMap(camX_, camY_, zoom_, flagRect(fr) ? fr : 0);
    renderWorldOverlays();
    uiLocked_ = overlay != OV_NONE;  // HUD buttons below an overlay must not react
    renderHUD();
    uiLocked_ = false;
    if (overlay == OV_PAUSE) renderPause();
    else if (overlay == OV_GAMEOVER) renderGameOver();
    if (tapPending_ && screen == SCR_GAME && overlay == OV_NONE && !overUI(tapX_, tapY_)) {
        tapPending_ = false;
        handleMapTap(tapX_, tapY_);
    }
}

void App::renderWorldOverlays() {
    const float t = SDL_GetTicks() / 1000.0f;
    const int W = game.map.w;
    // buildings
    if (zoom_ >= 1.2f) {
        float s = std::max(13.0f, std::min(28.0f, zoom_ * 3.5f));
        static const char* letter[B_COUNT] = {"S", "$", "M", "F"};
        for (size_t k = 0; k < game.buildings.size(); ++k) {
            const Building& b = game.buildings[k];
            float x = sx(b.tile % W + 0.5f), y = sy(b.tile / W + 0.5f);
            if (x < -s || y < -s || x > W_ + s || y > H_ + s) continue;
            uint32_t pc = game.players[b.owner].color;
            gfx.rect(x - s * 0.5f - 1, y - s * 0.5f - 1, s + 2, s + 2, 0x000000FF);
            gfx.rect(x - s * 0.5f, y - s * 0.5f, s, s, b.type == B_FORT ? 0x505860FF : 0xF4F1E8FF);
            gfx.rect(x - s * 0.5f, y + s * 0.5f - 3, s, 3, rgbToRgba(pc));
            gfx.text(x, y - s * 0.42f, letter[b.type], s * 0.8f, b.type == B_FORT ? 0xFFFFFFFF : 0x202020FF, 1);
            for (int l = 1; l < b.level; ++l) gfx.rect(x + s * 0.5f + 2, y - s * 0.5f + (l - 1) * 5, 4, 4, C_ACCENT);
            if (b.tile == selBuildingTile_) gfx.frame(x - s * 0.5f - 4, y - s * 0.5f - 4, s + 8, s + 8, 2, C_ACCENT);
        }
    }
    // attack arrows: own attacks and attacks against us
    for (size_t k = 0; k < game.attacks.size(); ++k) {
        const Attack& a = game.attacks[k];
        if (!a.active || (a.attacker != game.humanId && a.target != game.humanId)) continue;
        const Player& ap = game.players[a.attacker];
        float x0 = sx(ap.cx), y0 = sy(ap.cy), x1, y1;
        if (a.attacker == game.humanId) {
            x1 = sx(a.aimTile % W + 0.5f);
            y1 = sy(a.aimTile / W + 0.5f);
        } else {
            x1 = sx(game.players[game.humanId].cx);
            y1 = sy(game.players[game.humanId].cy);
        }
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 20) continue;
        uint32_t col = a.attacker == game.humanId ? 0xFFFFFFE0 : 0xFF5544E0;
        float off = std::fmod(t * 60.0f, 18.0f);
        for (float d = off; d < len; d += 18.0f) gfx.disc(x0 + dx * d / len, y0 + dy * d / len, 3.0f, col);
        gfx.disc(x1, y1, 7, col);
        gfx.textShadow(x1 + 10, y1 - 10, formatNumber(a.troops), 16, col | 0xff, 0);
    }
    // boats
    for (size_t k = 0; k < game.boats.size(); ++k) {
        const Boat& b = game.boats[k];
        if (!b.active || b.path.empty()) continue;
        size_t i = std::min(b.path.size() - 1, (size_t)b.pos);
        size_t j = std::min(b.path.size() - 1, i + 1);
        float f = b.pos - (float)i;
        float x = sx((b.path[i] % W) * (1 - f) + (b.path[j] % W) * f + 0.5f);
        float y = sy((b.path[i] / W) * (1 - f) + (b.path[j] / W) * f + 0.5f);
        if (x < -20 || y < -20 || x > W_ + 20 || y > H_ + 20) continue;
        float s = std::max(5.0f, std::min(12.0f, zoom_ * 2.5f));
        float bob = std::sin(t * 6 + k) * 1.5f;
        gfx.rect(x - s - 1, y - s * 0.5f - 1 + bob, s * 2 + 2, s + 2, 0x000000FF);
        gfx.rect(x - s, y - s * 0.5f + bob, s * 2, s, rgbToRgba(game.players[b.owner].color));
        gfx.rect(x - 1, y - s * 1.6f + bob, 2, s * 1.1f, 0xFFFFFFFF);
        if (zoom_ >= 2) gfx.textShadow(x, y + s, formatNumber(b.troops), 13, C_TEXT, 1);
    }
    // names
    for (size_t p = 1; p < game.players.size(); ++p) {
        const Player& pl = game.players[p];
        if (!pl.alive || pl.tiles == 0) continue;
        float size = std::sqrt((float)pl.tiles) * zoom_ * 0.4f;
        if (size < 9) continue;
        size = std::min(size, 34.0f);
        float x = sx(pl.cx + 0.5f), y = sy(pl.cy + 0.5f);
        if (x < -200 || y < -50 || x > W_ + 200 || y > H_ + 50) continue;
        uint32_t c = (int)p == game.humanId ? C_ACCENT : C_TEXT;
        gfx.textShadow(x, y - size * 0.7f, pl.name, size, c, 1);
        if (size >= 12) gfx.textShadow(x, y + size * 0.35f, formatNumber(pl.troops), size * 0.75f, c, 1);
    }
}

void App::renderHUD() {
    const Player& me = game.players[game.humanId];
    char buf[96];
    // ---- top bar
    panel(0, 0, (float)W_, 50);
    snprintf(buf, sizeof buf, "%s  (+%.0f/s)", formatNumber(me.gold).c_str(), game.goldIncome(game.humanId));
    gfx.text(12, 4, "GOUD", 13, C_DIM);
    gfx.text(12, 20, buf, 20, C_ACCENT);
    snprintf(buf, sizeof buf, "%s  (+%.1f/s)", formatNumber(me.materials).c_str(), game.materialIncome(game.humanId));
    gfx.text(222, 4, "GRONDSTOFFEN", 13, C_DIM);
    gfx.text(222, 20, buf, 20, 0xC9D6E3FF);
    snprintf(buf, sizeof buf, "%s / %s  (+%.0f/s)", formatNumber(me.troops).c_str(),
             formatNumber(game.maxTroops(game.humanId)).c_str(), game.troopGrowth(game.humanId));
    gfx.text(420, 4, "TROEPEN", 13, C_DIM);
    gfx.text(420, 20, buf, 20, C_TEXT);
    double pct = 100.0 * me.tiles / std::max(1, game.map.landTiles);
    snprintf(buf, sizeof buf, "%.1f%%  #%d/%d", pct, game.rankOf(game.humanId), (int)game.players.size() - 1);
    gfx.text(680, 4, "LAND  /  PLAATS", 13, C_DIM);
    gfx.text(680, 20, buf, 20, C_TEXT);
    if (netMode_ == NET_OFF) {
        snprintf(buf, sizeof buf, "x%d", speed_);
        if (button(W_ - 186.0f, 5, 80, 40, buf)) speed_ = speed_ >= 3 ? 1 : speed_ + 1;
    }
    if (button(W_ - 100.0f, 5, 94, 40, "Menu")) overlay = OV_PAUSE;

    // ---- leaderboard (right)
    {
        std::vector<int> order;
        for (size_t p = 1; p < game.players.size(); ++p)
            if (game.players[p].alive) order.push_back((int)p);
        struct ByTiles {
            const Game* g;
            bool operator()(int a, int b) const { return g->players[a].tiles > g->players[b].tiles; }
        } cmp = {&game};
        std::sort(order.begin(), order.end(), cmp);
        int rows = std::min(5, (int)order.size());
        float x = W_ - 236.0f, y = 58;
        panel(x, y, 228, 28.0f + rows * 24 + 24);
        snprintf(buf, sizeof buf, "Ranglijst  (%d over)", (int)order.size());
        gfx.text(x + 10, y + 5, buf, 15, C_DIM);
        for (int r = 0; r < rows; ++r) {
            const Player& pl = game.players[order[r]];
            float ry = y + 28 + r * 24;
            if (order[r] == game.humanId && gfx.hasFlag()) gfx.image(x + 8, ry + 4, 18, 12);
            else gfx.rect(x + 10, ry + 4, 12, 12, rgbToRgba(pl.color));
            std::string name = pl.name.size() > 16 ? pl.name.substr(0, 15) + "." : pl.name;
            gfx.text(x + 28, ry, name, 16, order[r] == game.humanId ? C_ACCENT : C_TEXT);
            snprintf(buf, sizeof buf, "%.1f%%", 100.0 * pl.tiles / std::max(1, game.map.landTiles));
            gfx.text(x + 218, ry, buf, 16, C_DIM, 2);
        }
        float ry = y + 28 + rows * 24;
        if (game.settings.teamsEnabled()) snprintf(buf, sizeof buf, "Jij zit in team %d", me.team + 1);
        else if (game.settings.mode == MODE_SANDBOX) snprintf(buf, sizeof buf, "Sandbox: vrij spelen");
        else if (game.settings.mode == MODE_CONQUEST) snprintf(buf, sizeof buf, "Doel: versla iedereen");
        else snprintf(buf, sizeof buf, "Doel: %d%% van het land", game.settings.winPercent);
        gfx.text(x + 10, ry + 2, buf, 14, C_DIM);
    }

    // ---- events (left)
    {
        float y = 58;
        int shown = 0;
        for (int k = (int)game.events.size() - 1; k >= 0 && shown < 5; --k) {
            const GameEvent& e = game.events[k];
            if (!game.spawnPhase && game.tick - e.tick > 150) break;
            if (!game.eventVisible(e)) continue;
            float w = gfx.textWidth(e.text, 16) + 16;
            gfx.rect(8, y, w, 24, 0x00000099);
            gfx.text(16, y + 3, e.text, 16, rgbToRgba(e.color));
            y += 27;
            ++shown;
        }
    }

    if (game.spawnPhase) {
        float w = 640;
        panel((W_ - w) * 0.5f, H_ - 120.0f, w, 90);
        if (me.spawned) {
            gfx.text(W_ * 0.5f, H_ - 110.0f, "Wachten op de andere spelers...", 28, C_ACCENT, 1);
            if (netMode_ == NET_HOST) {
                if (button(W_ * 0.5f - 150, H_ - 74.0f, 300, 40, "Start nu (rest willekeurig)", true)) game.autoSpawnRemaining();
            } else {
                gfx.text(W_ * 0.5f, H_ - 72.0f, "Het spel begint als iedereen een startplek heeft.", 19, C_TEXT, 1);
            }
        } else {
            gfx.text(W_ * 0.5f, H_ - 110.0f, "Kies je startplek", 28, C_ACCENT, 1);
            gfx.text(W_ * 0.5f, H_ - 72.0f, "Tik op vrij land. Slepen en knijpen om te zoeken.", 19, C_TEXT, 1);
        }
        return;
    }

    // ---- bottom bar
    float by = H_ - 82.0f;
    panel(0, by, (float)W_, 82);
    gfx.text(12, by + 4, "TROEPEN STUREN", 13, C_DIM);
    for (int k = 0; k < 5; ++k)
        if (button(10 + k * 60.0f, by + 22, 56, 52, kAttackLabel[k], attackIdx_ == k)) {
            attackIdx_ = k;
            saveSettings();
        }
    gfx.text(326, by + 4, "BOUWEN", 13, C_DIM);
    for (int b = 0; b < B_COUNT; ++b) {
        Cost c = game.buildCost(game.humanId, b);
        if (c.materials > 0) snprintf(buf, sizeof buf, "%s + %s gr", formatNumber(c.gold).c_str(), formatNumber(c.materials).c_str());
        else snprintf(buf, sizeof buf, "%s goud", formatNumber(c.gold).c_str());
        bool afford = me.gold >= c.gold && me.materials >= c.materials;
        if (button(324 + b * 118.0f, by + 22, 112, 52, buildingName(b), buildMode_ == b, me.alive && (afford || buildMode_ == b), buf)) {
            buildMode_ = buildMode_ == b ? -1 : b;
            selBuildingTile_ = -1;
            if (buildMode_ >= 0)
                showToast("Tik op je eigen land om te bouwen");
        }
    }
    if (button(800, by + 22, 112, 52, "Rekruteer", false, me.alive && me.gold >= 1000, "1000 goud")) actRecruit();
    if (button(920, by + 22, 96, 52, "Mijn land", false, me.alive)) focusOnHuman(std::max(zoom_, 3.0f));

    renderBuildingPanel();
}

void App::renderBuildingPanel() {
    if (selBuildingTile_ < 0) return;
    int bi = game.buildingAt(selBuildingTile_);
    if (bi < 0 || game.buildings[bi].owner != game.humanId) {
        selBuildingTile_ = -1;
        return;
    }
    const Building& b = game.buildings[bi];
    float x = 8, y = H_ - 82.0f - 150, w = 420, h = 142;
    panel(x, y, w, h);
    char buf[128];
    snprintf(buf, sizeof buf, "%s  -  niveau %d/%d", buildingName(b.type), b.level, MAX_LEVEL);
    gfx.text(x + 14, y + 10, buf, 22, C_ACCENT);
    switch (b.type) {
        case B_CITY: snprintf(buf, sizeof buf, "+%d max troepen, snellere groei", 10000 * b.level); break;
        case B_MARKET: snprintf(buf, sizeof buf, "+%d goud per seconde", 12 * b.level); break;
        case B_MINE: snprintf(buf, sizeof buf, "+%d grondstoffen per seconde", 3 * b.level); break;
        case B_FORT: snprintf(buf, sizeof buf, "Verdediging x%.1f in de buurt", 1.0 + 0.6 * b.level); break;
    }
    gfx.text(x + 14, y + 42, buf, 18, C_TEXT);
    if (button(x + w - 54, y + 8, 44, 40, "X")) { selBuildingTile_ = -1; return; }
    if (b.level < MAX_LEVEL) {
        Cost c = game.upgradeCost(b);
        snprintf(buf, sizeof buf, "%s goud + %s grondstoffen", formatNumber(c.gold).c_str(), formatNumber(c.materials).c_str());
        const Player& me = game.players[game.humanId];
        if (button(x + 14, y + 74, w - 28, 56, "Verbeteren", false, me.gold >= c.gold && me.materials >= c.materials, buf)) {
            actUpgrade(bi);
        }
    } else {
        gfx.text(x + 14, y + 90, "Hoogste niveau bereikt", 18, C_DIM);
    }
}

void App::handleMapTap(float x, float y) {
    int t = screenToTile(x, y);
    if (t < 0) return;
    std::string why;
    const int me = game.humanId;
    if (game.spawnPhase) {
        actSpawn(t);
        return;
    }
    if (!game.players[me].alive || game.over) {
        if (game.over && overlay == OV_NONE) showToast("Het spel is voorbij");
        return;
    }
    if (buildMode_ >= 0) {
        actBuild(buildMode_, t);
        return;
    }
    if (game.owner[t] == me) {
        // select a building at or near the tap
        int best = -1, bestD = 1 << 30, W = game.map.w;
        float r = std::max(2.0f, 12.0f / zoom_);
        for (size_t k = 0; k < game.buildings.size(); ++k) {
            const Building& b = game.buildings[k];
            if (b.owner != me) continue;
            int dx = b.tile % W - t % W, dy = b.tile / W - t / W;
            int d = dx * dx + dy * dy;
            if (d <= r * r && d < bestD) { bestD = d; best = b.tile; }
        }
        selBuildingTile_ = best;
        if (best < 0) showToast("Dit is jouw land");
        return;
    }
    selBuildingTile_ = -1;
    actAttack(t);
}

void App::renderPause() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x000000A0);
    block(0, 0, (float)W_, (float)H_);
    const bool mp = netMode_ != NET_OFF;
    float w = 460, h = mp ? 340.0f : 636.0f, x = (W_ - w) * 0.5f, y = (H_ - h) * 0.5f;
    panel(x, y, w, h);
    gfx.text(W_ * 0.5f, y + 18, mp ? "Menu (spel loopt door)" : "Gepauzeerd", 32, C_TEXT, 1);
    float bx = x + 40, bw = w - 80, cy = y + 76;
    if (button(bx, cy, bw, 60, "Verder spelen", true)) overlay = OV_NONE;
    cy += 76;
    if (button(bx, cy, bw, 60, musicOn_ ? "Muziek: aan" : "Muziek: uit", musicOn_)) {
        musicOn_ = !musicOn_;
        saveSettings();
    }
    cy += 76;
    if (mp) {
        if (button(bx, cy, bw, 60, netMode_ == NET_HOST ? "Spel stoppen (voor iedereen)" : "Spel verlaten")) {
            leaveMultiplayer();
            screen = SCR_MENU;
            overlay = OV_NONE;
        }
        return;
    }
    gfx.text(bx, cy, "Opslaan in:", 18, C_DIM);
    cy += 26;
    for (int k = 1; k < 4; ++k) {
        if (button(bx + (k - 1) * (bw / 3), cy, bw / 3 - 8, 56, kSlotNames[k], false, !game.spawnPhase)) {
            showToast(saveSlot(kSlots[k]) ? std::string("Opgeslagen in ") + kSlotNames[k] : "Opslaan mislukt");
        }
    }
    cy += 76;
    if (button(bx, cy, bw, 60, "Spel laden")) {
        loadReturn_ = SCR_GAME;
        loadMessage_.clear();
        screen = SCR_LOAD;
    }
    cy += 76;
    if (button(bx, cy, bw, 60, "Nieuw spel")) {
        autosave();
        mpSetup_ = false;
        screen = SCR_NEW;
        overlay = OV_NONE;
    }
    cy += 76;
    if (button(bx, cy, bw, 60, "Hoofdmenu")) {
        autosave();
        screen = SCR_MENU;
        overlay = OV_NONE;
    }
}

void App::renderGameOver() {
    gfx.rect(0, 0, (float)W_, (float)H_, 0x00000090);
    block(0, 0, (float)W_, (float)H_);
    float w = 520, h = 330, x = (W_ - w) * 0.5f, y = (H_ - h) * 0.5f;
    panel(x, y, w, h);
    const Player& me = game.players[game.humanId];
    bool won = game.winnerPlayer == game.humanId ||
               (game.settings.teamsEnabled() && game.winnerTeam >= 0 && game.winnerTeam == me.team);
    gfx.text(W_ * 0.5f, y + 24, won ? "Gewonnen!" : "Verloren", 44, won ? C_GOOD : C_BAD, 1);
    std::string line;
    if (game.settings.teamsEnabled() && game.winnerTeam >= 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "Team %d wint", game.winnerTeam + 1);
        line = buf;
    } else if (game.winnerPlayer > 0) {
        line = game.players[game.winnerPlayer].name + (won ? " - dat ben jij!" : " staat er het best voor");
    }
    gfx.text(W_ * 0.5f, y + 90, line, 22, C_TEXT, 1);
    if (button(x + 30, y + 150, w - 60, 56, "Verder kijken")) overlay = OV_NONE;
    if (button(x + 30, y + 220, (w - 70) / 2, 56, "Nieuw spel", true)) { leaveMultiplayer(); mpSetup_ = false; screen = SCR_NEW; overlay = OV_NONE; }
    if (button(x + 40 + (w - 70) / 2, y + 220, (w - 70) / 2, 56, "Hoofdmenu")) { leaveMultiplayer(); screen = SCR_MENU; overlay = OV_NONE; }
}

}  // namespace fc
