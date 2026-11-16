// Frontier Classic - entry point (SDL2 + OpenGL ES 2.0).
// iOS: landscape only, 30 fps cap, autosave when the app goes to the background.
// Desktop (tests): "--shots <dir>" renders a scripted set of screenshots and exits.
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include "App.h"
#include "SDL.h"

using namespace fc;

static std::string g_saveDir;

#if defined(__IPHONEOS__)
extern "C" char* fc_ios_save_dir(void);
#endif

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
    // the app's own Documents folder (see ios/ios_shims.m)
    char* p = fc_ios_save_dir();
    std::string r = p ? p : "./";
    free(p);
    return r;
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
#if defined(__IPHONEOS__)
    app.render(w, h, dw, dh);
#else
    // Desktop: the game always thinks in 1024x768 (like the iPad) and is scaled
    // to the window, with black bars when the shape differs.
    if (w < 1 || h < 1 || dw < 1 || dh < 1) return;
    float s = std::min(dw / 1024.0f, dh / 768.0f);
    int vw = (int)(1024 * s), vh = (int)(768 * s);
    int vx = (dw - vw) / 2, vy = (dh - vh) / 2;
    float px = dw / (float)w;  // drawable pixels per window point
    app.setPointerTransform(px / s, vx / px, vy / px);
    app.render(1024, 768, vw, vh, vx, vy);
#endif
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

static int spawnNear(Game& g, const char* nation) {
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

static void runMultiplayerShots(App& app, SDL_Window* win, const std::string& dir) {
    const int port = 47955;
    app.lookupPublicIp = false;  // automated run: no calls to outside services
    app.setPlayerName("Kees");
    app.screen = SCR_NAME;
    shot(app, win, dir + "/15b_name.bmp");
    app.setServer("mijnnas.synology.me", true);
    app.screen = SCR_MP;
    shot(app, win, dir + "/15c_multiplayer_server.bmp");
    app.screen = SCR_JOIN;
    shot(app, win, dir + "/16b_join_server.bmp");
    app.setServer("", false);
    app.screen = SCR_MP;
    shot(app, win, dir + "/15_multiplayer.bmp");
    app.screen = SCR_JOIN;
    shot(app, win, dir + "/16_join.bmp");

    app.newSettings = Settings();
    app.newSettings.mapId = "europe";
    app.newSettings.numAI = 25;
    std::string err;
    if (!app.hostLobby(port, &err)) {
        printf("MP: host failed: %s\n", err.c_str());
        return;
    }
    ClientSession cl;
    Game cg;
    std::string mapsDir = resourceDir() + "maps";
    if (!cl.connect("127.0.0.1", port, "Sanne", &err)) {
        printf("MP: connect failed: %s\n", err.c_str());
        return;
    }
    for (int k = 0; k < 400 && !(cl.state() == ClientSession::LOBBY && cl.lobbyNames().size() == 2); ++k) {
        app.update(0);
        cl.pump(cg, mapsDir);
        SDL_Delay(5);
    }
    printf("MP: lobby players=%d client slot=%d/%d\n", 1 + app.hostSession().clientCount(), cl.lobbySlot(), cl.lobbyCount());
    shot(app, win, dir + "/17_lobby_host.bmp");
    if (!app.hostStart()) return;
    bool started = false;
    for (int k = 0; k < 400 && !started; ++k) {
        app.update(0);
        started = cl.pump(cg, mapsDir);
        SDL_Delay(5);
    }
    printf("MP: client started=%d as player %d\n", (int)started, cg.humanId);
    if (!started) return;
    app.game.spawnHuman(spawnNear(app.game, "France"));
    cl.sendOrder(ORD_SPAWN, spawnNear(cg, "Poland"), 0, 0);
    for (int k = 0; k < 900; ++k) {
        app.hostSession().pumpGame(app.game);
        runTicks(app, 1);
        if (k % 30 == 10 && cg.players[cg.humanId].alive) {
            // the remote player expands too: free land near its own territory
            const Player& me = cg.players[cg.humanId];
            for (int tries = 0; tries < 300; ++tries) {
                int x = (int)me.cx + rand() % 61 - 30, y = (int)me.cy + rand() % 61 - 30;
                if (x < 0 || y < 0 || x >= cg.map.w || y >= cg.map.h) continue;
                int t = y * cg.map.w + x;
                if (cg.map.isLand(t) && cg.owner[t] != cg.humanId) {
                    cl.sendOrder(ORD_ATTACK, t, 0, 0.5f);
                    break;
                }
            }
        }
        if (k % 3 == 0) app.hostSession().broadcast(app.game, true);
        cl.pump(cg, mapsDir);
    }
    for (int k = 0; k < 50; ++k) {
        app.hostSession().pumpGame(app.game);
        app.hostSession().broadcast(app.game, true);
        SDL_Delay(4);
        cl.pump(cg, mapsDir);
    }
    size_t diff = 0;
    for (size_t t = 0; t < cg.owner.size() && t < app.game.owner.size(); ++t)
        if (cg.owner[t] != app.game.owner[t]) ++diff;
    printf("MP: after 900 ticks: tiles different between host and client: %d, client land %d, host land %d\n", (int)diff,
           cg.players[cg.humanId].tiles, app.game.players[1].tiles);
    app.fitMap();
    shot(app, win, dir + "/18_multiplayer_game.bmp");
    cl.close();
    app.leaveMultiplayer();
    app.screen = SCR_MENU;
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
    app.newSettings.cheatTroops = app.newSettings.cheatGod = true;
    app.screen = SCR_CHEATS;
    shot(app, win, dir + "/03c_cheats.bmp");
    app.newSettings.cheatTroops = app.newSettings.cheatGod = false;

    // a generated test flag (3 stripes + a diagonal band) to check flag rendering
    {
        const int fw = 120, fh = 80;
        std::vector<unsigned char> px(fw * fh * 4);
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                unsigned char* p = &px[(y * fw + x) * 4];
                int band = y * 3 / fh;
                unsigned char r = band == 0 ? 200 : (band == 1 ? 255 : 30), g = band == 0 ? 30 : (band == 1 ? 255 : 70),
                              b = band == 0 ? 40 : (band == 1 ? 255 : 160);
                if (std::abs(x * fh / fw - y) < 6) { r = 250; g = 200; b = 0; }
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            }
        app.setFlagPixels(&px[0], fw, fh);
    }
    app.screen = SCR_FLAG;
    shot(app, win, dir + "/03b_flag.bmp");

    Settings s;
    s.mapId = "world";
    s.mode = MODE_FFA;
    s.numAI = 40;
    app.startGame(s);
    shot(app, win, dir + "/04_spawn_phase.bmp");
    app.game.spawnHuman(spawnNear(app.game, "Germany"));
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

    runMultiplayerShots(app, win, dir);

    {   // the Netherlands, with cheats
        Settings nl;
        nl.mapId = "netherlands";
        nl.numAI = 40;
        nl.cheatTroops = nl.cheatGold = nl.cheatFast = true;
        if (app.startGame(nl)) {
            app.game.spawnHuman(spawnNear(app.game, "Utrecht"));
            runTicks(app, 600);
            app.fitMap();
            shot(app, win, dir + "/19_netherlands_cheats.bmp");
            app.focusOnHuman(3.0f);
            shot(app, win, dir + "/20_netherlands_zoom.bmp");
        }
    }

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
        app.game.spawnHuman(spawnNear(app.game, home[m]));
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

    // self-test of the public IP lookup: prints the result and exits (no window)
    for (int k = 1; k < argc; ++k)
        if (!strcmp(argv[k], "--public-ip")) {
            netInit();
            PublicIp ip;
            ip.start();
            for (int i = 0; i < 2500 && ip.busy(); ++i) {
                ip.pump();
                SDL_Delay(10);
            }
            printf(ip.ip().empty() ? "PUBLIC_IP: not found%s" : "PUBLIC_IP: %s", ip.ip().c_str());
            printf("\n");
            return ip.ip().empty() ? 1 : 0;
        }

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_IDLE_TIMER_DISABLED, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#if defined(_WIN32)
    // Windows has no OpenGL ES: desktop OpenGL 2.1 runs the same shader
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
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
#if !defined(__IPHONEOS__)
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
#endif
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
