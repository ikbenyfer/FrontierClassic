# Frontier Classic

An offline territory conquest game for the **iPad 2** (A1395, iOS 9.3.5, armv7, 1024×768).
It plays like OpenFront and territorial games: grow your land, send troops, beat the AI.
Everything is written from scratch: own code, own look, own maps.

**Single player is 100% offline.** No server, account, login, ads or analytics.
The only network code is the optional multiplayer (plain TCP sockets, `src/net`); the CI build checks that the iPad binary contains no web/HTTP frameworks.

There is also a **Windows version** (64-bit, one `.exe`), built from the same source.

## What's in it

| Part | Contents |
|---|---|
| Maps | World Map, Europe, Asia, North America, South America, Africa. Real country shapes (Natural Earth, public domain), terrain (plains, hills, mountains, desert, tundra), faint country borders. AI nations start in their own country. |
| Armies | Troops grow automatically (more land and cities = faster). Tap an area: your troops attack along the whole border. Across the sea a ship sails out (animated). The buttons 10–100% set how many troops you send. Two armies attacking each other meet in the middle. |
| Combat | Cost per tile depends on terrain (mountains 2.4×), the defender's troop density and forts nearby. |
| Economy | Gold (land + markets) and resources (land + mines; mines can be built anywhere on your land). Buildings: City, Market, Mine, Fort, each upgradable to level 3. Recruiting: 1000 gold → 1500 troops. |
| AI | 10–50 opponents at 3 levels. They expand, build, upgrade, attack the weakest neighbour, strike back at whoever attacks them (including you) and use ships when they are boxed in. |
| Modes | Free For All, Teams (2–4), Conquest (last one standing), Sandbox (plenty of gold, AI can be peaceful), Custom Game (teams, starting gold, win %, troop growth). |
| Controls | Drag = move, pinch = zoom, tap = attack / build / select. Big buttons, landscape only. |
| Saving | Autosave every minute and when the app goes to the background, plus 3 slots. Plain JSON files in the app's `Documents` folder (also visible through iTunes file sharing). Your new-game settings are remembered. |

## Multiplayer

Up to 8 people, iPad and Windows mixed. No central server: one player hosts.

- **Host:** Multiplayer → Spel hosten → choose settings → Open lobby → Start spel. Forward the TCP port (default 47800) in your router to the hosting device and give your public IP address + port to the others.
- **Join:** Multiplayer → Meedoen → enter the host's IP address and port.
- The host runs the simulation and sends the state (changed tiles, player numbers, buildings, ships) about 7 times per second; the others only send their orders. Empty seats and players who leave are played by the AI.
- Multiplayer games are not saved.

## Technology

- C++11, own engine: `src/core` (simulation, AI, JSON, save/load; no SDL) and `src/app` (SDL2 + OpenGL ES 2.0).
- SDL 2.0.22 (zlib licence), compiled from source in the build. Only video/events/timers/threads/files; audio, controllers, sensors, Metal and Vulkan are off (`ios/SDL_config_overrides.h`).
- Graphics: one RGB565 map texture (max 880×740), only changed 64×64 blocks are re-uploaded; everything else (text, buttons, buildings, ships) goes through one batched shader. 30 fps cap, simulation at 10 ticks/s.
- Memory: largest map ±0.65 M tiles → a few MB of game state.

## Building (no Mac needed)

Every push builds in GitHub Actions (`.github/workflows/build.yml`):

0. **Multiplayer test**: a host and two clients over localhost; every client must end with exactly the host's map.
1. **Simulation tests** (gcc + clang, `-Werror`, plus AddressSanitizer/UBSan): all 6 maps × 5 modes with 50 AI, consistency checks, save→load→compare.
2. **Screenshots**: the desktop version plays automatically under Linux (Mesa, OpenGL ES 2.0) and takes pictures (artifact `screenshots`).
3. **iPad app**: Theos + iOS SDK 14.5, `armv7`, minimum iOS 9.3, fake-signed with `ldid`. Checks: armv7 only, iOS 9.3, no network frameworks or functions, Info.plist landscape, all files in the app. Produces `FrontierClassic.ipa` (+ `.deb`) as an artifact and under Releases.
4. **Windows**: cross-compiled with MinGW, SDL2 linked statically, desktop OpenGL 2.1. Produces `FrontierClassic-Windows-x.y.z.zip`.

Regenerating the assets yourself (only needed if you change them):
```
python -I tools/make_maps.py ne_50m_admin_0_countries.geojson Resources/maps
python -I tools/make_assets.py Resources
```

## Installing on the iPad 2

Needs a jailbroken iPad (Phœnix) with **AppSync Unified**, just like your other apps.
Download `FrontierClassic-x.y.z.ipa` from Releases and install it with the **iPad IPA Installer** (USB) or Filza.

## Credits

Maps: Natural Earth (public domain). Font: Aileron (CC0). SDL2 (zlib licence).
