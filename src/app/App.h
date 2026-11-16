// Screens, touch input and HUD.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../core/Game.h"
#include "../net/Session.h"
#include "Gfx.h"
#include "SDL.h"

namespace fc {

enum Screen { SCR_MENU, SCR_NEW, SCR_LOAD, SCR_INFO, SCR_GAME, SCR_FLAG, SCR_MP, SCR_JOIN, SCR_LOBBY, SCR_NAME };
enum NetMode { NET_OFF, NET_HOST, NET_CLIENT };
enum Overlay { OV_NONE, OV_PAUSE, OV_GAMEOVER };

class App {
public:
    bool init(const std::string& resDir, const std::string& saveDir, std::string* err);
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void update(double dtSeconds);
    // vpX/vpY: where the picture starts inside the window (letterboxing on desktop)
    void render(int winW, int winH, int drawW, int drawH, int vpX = 0, int vpY = 0);
    // desktop: window pixels -> logical 1024x768 coordinates
    void setPointerTransform(float scale, float offX, float offY) { ptrScale_ = scale; ptrOffX_ = offX; ptrOffY_ = offY; }

    // ---- multiplayer (the only part that uses the network) ----
    NetMode netMode() const { return netMode_; }
    bool hostLobby(int port, std::string* err);       // open a lobby with newSettings
    bool hostStart();                                  // start the game for everyone in the lobby
    bool joinGame(const std::string& ip, int port, std::string* err);
    void leaveMultiplayer();
    HostSession& hostSession() { return host_; }
    bool lookupPublicIp = true;  // off in automated runs (no calls to outside services)
    void setPlayerName(const std::string& n) { playerName_ = cleanPlayerName(n); }
    const std::string& playerName() const { return playerName_; }
    ClientSession& clientSession() { return client_; }
    void autosave();
    bool quitRequested() const { return quit_; }

    // used by the automated screenshot run on desktop
    Gfx gfx;
    Game game;
    Screen screen = SCR_MENU;
    Overlay overlay = OV_NONE;
    Settings newSettings;
    bool startGame(const Settings& s);
    bool loadSlot(const std::string& slot);
    bool saveSlot(const std::string& slot);
    void focusOnHuman(float zoom);
    void fitMap();
    std::string slotPath(const std::string& slot) const { return saveDir_ + slot + ".json"; }
    // player flag: Documents/flag.png (chosen from Photos or copied in via iTunes/Filza)
    bool loadFlag();
    void setFlagPixels(const uint8_t* rgba, int w, int h);
    void removeFlag();

private:
    std::string resDir_, mapsDir_, saveDir_;
    std::vector<MapEntry> maps_;
    bool quit_ = false;

    // camera: tile at the centre of the screen and pixels per tile
    float camX_ = 0, camY_ = 0, zoom_ = 1;
    int W_ = 1024, H_ = 768;

    // input
    struct Touch {
        float x, y;
    };
    std::map<SDL_FingerID, Touch> touches_;
    bool tapPending_ = false;
    float tapX_ = 0, tapY_ = 0;
    float downX_ = 0, downY_ = 0;
    Uint32 downTime_ = 0;
    bool moved_ = false, multi_ = false, downOnUI_ = false, mouseDown_ = false;
    float pinchDist_ = 0, pinchMidX_ = 0, pinchMidY_ = 0;
    std::vector<SDL_Rect> blocks_, blocksPrev_;

    // game UI state
    int attackIdx_ = 1;
    int buildMode_ = -1;
    int selBuildingTile_ = -1;
    int speed_ = 1;
    double simAcc_ = 0;
    bool gameOverSeen_ = false;
    bool uiLocked_ = false;
    bool wasSpawned_ = false;
    float ptrScale_ = 1, ptrOffX_ = 0, ptrOffY_ = 0;

    // multiplayer
    NetMode netMode_ = NET_OFF;
    HostSession host_;
    ClientSession client_;
    bool mpSetup_ = false;          // "new game" screen is being used to set up a hosted game
    std::string joinIp_, portText_ = "47800";
    std::string playerName_ = "Speler";
    PublicIp publicIp_;
    void renderName();
    void playerList(float x, float y, const std::vector<std::string>& names, int self);
    int field_ = 0;                 // join screen: 0 = IP, 1 = port
    void enterGameScreen();
    std::string lobbyInfo() const;
    int portNumber() const;
    void keypad(float x, float y, std::string& target, size_t maxLen, bool dot);
    void typeChar(char c);
    void renderMP();
    void renderJoin();
    void renderLobby();
    void actSpawn(int tile);
    void actAttack(int tile);
    void actBuild(int type, int tile);
    void actUpgrade(int buildingIndex);
    void actRecruit();
    bool saveOk_ = false;
    bool musicOn_ = true;
    std::string toast_;
    Uint32 toastUntil_ = 0;
    Uint32 lastAutosave_ = 0;
    std::string loadMessage_;
    Screen loadReturn_ = SCR_MENU;

    // helpers
    void pointerDown(float x, float y);
    void pointerMove(float x, float y, float dx, float dy);
    void pointerUp(float x, float y);
    bool overUI(float x, float y) const;
    void block(float x, float y, float w, float h);
    bool button(float x, float y, float w, float h, const std::string& label, bool selected = false,
                bool enabled = true, const std::string& sub = "");
    bool hit(float x, float y, float w, float h);
    void panel(float x, float y, float w, float h);
    void showToast(const std::string& s);
    void clampCamera();
    void zoomAt(float sx, float sy, float factor);
    int screenToTile(float sx, float sy) const;
    float sx(float tx) const { return (tx - camX_) * zoom_ + W_ * 0.5f; }
    float sy(float ty) const { return (ty - camY_) * zoom_ + H_ * 0.5f; }
    void saveSettings();
    void loadSettings();

    void renderMenu();
    void renderNew();
    void renderLoad();
    void renderInfo();
    void renderFlag();
    bool flagRect(float out[4]) const;
    void renderGame();
    void renderWorldOverlays();
    void renderHUD();
    void renderBuildingPanel();
    void renderPause();
    void renderGameOver();
    void handleMapTap(float x, float y);
};

}  // namespace fc
