// Screens, touch input and HUD.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../core/Game.h"
#include "Gfx.h"
#include "SDL.h"

namespace fc {

enum Screen { SCR_MENU, SCR_NEW, SCR_LOAD, SCR_INFO, SCR_GAME };
enum Overlay { OV_NONE, OV_PAUSE, OV_GAMEOVER };

class App {
public:
    bool init(const std::string& resDir, const std::string& saveDir, std::string* err);
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void update(double dtSeconds);
    void render(int winW, int winH, int drawW, int drawH);
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
    bool saveOk_ = false;
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
    void renderGame();
    void renderWorldOverlays();
    void renderHUD();
    void renderBuildingPanel();
    void renderPause();
    void renderGameOver();
    void handleMapTap(float x, float y);
};

}  // namespace fc
