// Frontier Classic - entry point (SDL2 + OpenGL ES 2.0).
// iOS: landscape only, 30 fps cap, autosave when the app goes to the background.
// Desktop (tests): "--shots <dir>" renders a scripted set of screenshots and exits.
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "App.h"
#include "SDL.h"

using namespace fc;

static std::string g_saveDir;

static std::string resourceDir() {
    char* base = SDL_GetBasePath();
    std::string r = base ? base : "./";
    if (base) SDL_free(base);
    if (!r.empty() && r[r.size() - 1] != '/' && r[r.size() - 1] != '\\') r += '/';
#if !defined(__IPHONEOS__)
    // desktop builds keep the resources next to the source tree
    FILE* f = fopen((r + "maps/index.json").c_str(), "rb");
    if (f) fclose(f);
    else r += "Resources/";
#endif
    return r;
}

static std::string saveDirectory() {
#if defined(__IPHONEOS__)
    // the app's own Documents folder (local, visible in iTunes file sharing)
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/Documents/";
#else
    char* p = SDL_GetPrefPath("YF", "FrontierClassic");
    std::string r = p ? p : "./";
    if (p) SDL_free(p);
    return r;
#endif
}

static bool g_background = false;

static int SDLCALL appEvents(void* userdata, SDL_Event* e) {
    App* app = (App*)userdata;
    switch (e->type) {
        case SDL_APP_TERMINATING:
        case SDL_APP_WILLENTERBACKGROUND:
            app->autosave();
            g_background = true;
            return 0;
        case SDL_APP_DIDENTERBACKGROUND:
            g_background = true;
            return 0;
        case SDL_APP_WILLENTERFOREGROUND:
        case SDL_APP_DIDENTERFOREGROUND:
            g_background = false;
            return 0;
        default:
            return 1;
    }
}

// ---- scripted screenshots (desktop CI only) ----
static void frame(App& app, SDL_Window* win) {
    int w, h, dw, dh;
    SDL_GetWindowSize(win, &w, &h);
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    app.render(w, h, dw, dh);
}

static void shot(App& app, SDL_Window* win, const std::string& path) {
    frame(app, win);
    if (!app.gfx.screenshotBMP(path)) printf("screenshot failed: %s\n", path.c_str());
    else printf("wrote %s\n", path.c_str());
    SDL_GL_SwapWindow(win);
}

static void runTicks(App& app, int n) {
    Game& g = app.game;
    for (int k = 0; k < n; ++k) {
        if (k % 20 == 0 && g.players[g.humanId].alive) {
            // expand like a player would: free land first, then the weakest neighbour
            int best = 0;
            double bestT = 1e18;
            for (size_t q = 2; q < g.players.size(); ++q)
                if (g.players[q].alive && g.adj(g.humanId, (int)q) > 0 && g.players[q].troops < bestT) {
                    bestT = g.players[q].troops;
                    best = (int)q;
                }
            int target = -1;
            if (g.adj(g.humanId, 0) > 0) {
                for (int t = 0; t < (int)g.owner.size() && target < 0; t += 7)
                    if (g.owner[t] == 0 && g.map.isLand(t)) {
                        int x = t % g.map.w, y = t / g.map.w;
                        if (std::abs(x - (int)g.players[g.humanId].cx) < 40 && std::abs(y - (int)g.players[g.humanId].cy) < 40) target = t;
                    }
            }
            if (target < 0 && best) target = (int)(g.players[best].cy) * g.map.w + (int)g.players[best].cx;
            if (target >= 0) g.orderAttack(g.humanId, target, g.players[g.humanId].troops * 0.5, 0);
        }
        if (k % 150 == 0) {
            for (int tries = 0; tries < 80; ++tries) {
                int t = rand() % (int)g.owner.size();
                if (g.owner[t] == g.humanId && g.build(g.humanId, (k / 150) % B_COUNT, t, 0)) break;
            }
        }
        g.step();
    }
}

static int spawnNear(App& app, const char* nation) {
    Game& g = app.game;
    for (size_t k = 0; k < g.map.nations.size(); ++k)
        if (g.map.nations[k].name == nation) {
            int x = g.map.nations[k].x, y = g.map.nations[k].y;
            for (int r = 0; r < 60; ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= g.map.w || yy >= g.map.h) continue;
                        int t = yy * g.map.w + xx;
                        if (g.canSpawnAt(t)) return t;
                    }
        }
    for (int t = (int)g.owner.size() / 2; t < (int)g.owner.size(); ++t)
        if (g.canSpawnAt(t)) return t;
    return -1;
}

static void runShots(App& app, SDL_Window* win, const std::string& dir) {
    srand(5);
    app.screen = SCR_MENU;
    shot(app, win, dir + "/01_menu.bmp");
    app.screen = SCR_NEW;
    app.newSettings.mode = MODE_CUSTOM;
    app.newSettings.customTeams = true;
    shot(app, win, dir + "/02_new_game.bmp");
    app.screen = SCR_INFO;
    shot(app, win, dir + "/03_info.bmp");

    Settings s;
    s.mapId = "world";
    s.mode = MODE_FFA;
    s.numAI = 40;
    app.startGame(s);
    shot(app, win, dir + "/04_spawn_phase.bmp");
    app.game.spawnHuman(spawnNear(app, "Germany"));
    runTicks(app, 1800);
    app.fitMap();
    shot(app, win, dir + "/05_world_3min.bmp");
    app.focusOnHuman(4.0f);
    shot(app, win, dir + "/06_world_zoom.bmp");
    app.overlay = OV_PAUSE;
    shot(app, win, dir + "/07_pause.bmp");
    app.overlay = OV_NONE;
    app.saveSlot("slot1");
    app.screen = SCR_LOAD;
    shot(app, win, dir + "/08_load.bmp");
    if (app.loadSlot("slot1")) shot(app, win, dir + "/09_loaded.bmp");

    const char* maps[5] = {"europe", "asia", "northamerica", "southamerica", "africa"};
    const char* home[5] = {"France", "India", "Mexico", "Brazil", "Nigeria"};
    int modes[5] = {MODE_TEAMS, MODE_CONQUEST, MODE_SANDBOX, MODE_CUSTOM, MODE_FFA};
    for (int m = 0; m < 5; ++m) {
        Settings t;
        t.mapId = maps[m];
        t.mode = modes[m];
        t.numAI = 50;
        t.numTeams = 3;
        t.customTeams = modes[m] == MODE_CUSTOM;
        t.difficulty = DIFF_HARD;
        if (!app.startGame(t)) continue;
        app.game.spawnHuman(spawnNear(app, home[m]));
        runTicks(app, 1200);
        app.fitMap();
        char buf[256];
        snprintf(buf, sizeof buf, "%s/%02d_%s.bmp", dir.c_str(), 10 + m, maps[m]);
        shot(app, win, buf);
    }
}

int main(int argc, char* argv[]) {
    std::string shotsDir;
    for (int k = 1; k + 1 < argc; ++k)
        if (!strcmp(argv[k], "--shots")) shotsDir = argv[k + 1];

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_IDLE_TIMER_DISABLED, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

#if defined(__IPHONEOS__)
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
    int ww = 0, wh = 0;
#else
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    int ww = 1024, wh = 768;
#endif
    SDL_Window* win = SDL_CreateWindow("Frontier Classic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!win) {
        printf("SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        printf("SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    App* app = new App();
    g_saveDir = saveDirectory();
    std::string err;
    if (!app->init(resourceDir(), g_saveDir, &err)) {
        printf("init failed: %s\n", err.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Frontier Classic", err.c_str(), win);
        return 1;
    }
    SDL_SetEventFilter(appEvents, app);

    if (!shotsDir.empty()) {
        runShots(*app, win, shotsDir);
        app->shutdown();
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    Uint32 last = SDL_GetTicks();
    bool running = true;
    while (running) {
        Uint32 frameStart = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            app->handleEvent(e);
        }
        if (g_background) {  // iOS forbids GL drawing in the background
            SDL_Delay(100);
            last = SDL_GetTicks();
            continue;
        }
        Uint32 now = SDL_GetTicks();
        double dt = (now - last) / 1000.0;
        last = now;
        if (dt > 0.25) dt = 0.25;
        app->update(dt);
        frame(*app, win);
        SDL_GL_SwapWindow(win);
        Uint32 spent = SDL_GetTicks() - frameStart;
        if (spent < 33) SDL_Delay(33 - spent);  // ~30 fps is plenty and saves battery on the A5
    }
    app->autosave();
    app->shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
