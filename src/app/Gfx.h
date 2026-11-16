// OpenGL ES 2.0 drawing: the map texture plus one batched shader for
// rectangles and text. Small and A5-friendly: RGB565 map texture, only
// changed 64x64 blocks are re-uploaded.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "SDL_opengles2.h"

namespace fc {

class Game;

struct Glyph {
    float x, y, w, h, xoff, yoff, adv;
};

class Gfx {
public:
    bool init(const std::string& resDir, std::string* err);
    void shutdown();

    // logical size (points) and drawable size (pixels)
    void beginFrame(int winW, int winH, int drawW, int drawH);
    void endFrame() { flush(); }

    // ---- map ----
    void setMap(const Game& g);
    void updateMap(Game& g);  // consumes g.changed
    // flagRect: x, y, w, h in tiles where the player's flag is stretched (or null)
    void drawMap(float camX, float camY, float zoom, const float* flagRect = 0);
    void refreshMap(const Game& g);  // recolour every tile (after the flag changed)

    // ---- player flag ----
    void setFlag(const uint8_t* rgba, int w, int h);  // null = no flag
    void setFlagOwner(int p) { flagOwner_ = p; }
    bool hasFlag() const { return flagTex_ != 0; }
    int flagW() const { return flagW_; }
    int flagH() const { return flagH_; }
    void image(float x, float y, float w, float h);  // draws the flag
    bool hasMap() const { return mapTex_ != 0; }

    // ---- 2D ----
    void rect(float x, float y, float w, float h, uint32_t rgba);
    void frame(float x, float y, float w, float h, float t, uint32_t rgba);
    void line(float x0, float y0, float x1, float y1, float t, uint32_t rgba);
    void disc(float cx, float cy, float r, uint32_t rgba);
    // align: 0 left, 1 centre, 2 right. y is the top of the text. Returns width.
    float text(float x, float y, const std::string& s, float size, uint32_t rgba, int align = 0);
    float textShadow(float x, float y, const std::string& s, float size, uint32_t rgba, int align = 0);
    float textWidth(const std::string& s, float size) const;
    void flush();

    int winW() const { return winW_; }
    int winH() const { return winH_; }

    // read back the frame (for automated screenshots on desktop)
    bool screenshotBMP(const std::string& path);

private:
    struct Vtx {
        float x, y, u, v;
        uint8_t r, g, b, a;
    };
    GLuint prog_ = 0, fontTex_ = 0, mapTex_ = 0, curTex_ = 0;
    GLint locProj_ = -1, locTex_ = -1, locMode_ = -1, locFlag_ = -1, locMapSize_ = -1, locFlagRect_ = -1;
    int curMode_ = -1;  // 0 colour*texture, 1 text (alpha), 2 map with flag
    GLuint flagTex_ = 0;
    int flagW_ = 0, flagH_ = 0, flagOwner_ = 1;
    std::vector<Vtx> verts_;
    Glyph glyphs_[96];
    float fontSize_ = 30, fontAscent_ = 24;
    int winW_ = 1024, winH_ = 768, drawW_ = 1024, drawH_ = 768;

    // map state
    int mapW_ = 0, mapH_ = 0;
    std::vector<uint16_t> pix_;
    std::vector<uint16_t> terrainPix_;
    std::vector<uint8_t> dirty_;
    std::vector<uint16_t> upload_;
    int chunksX_ = 0, chunksY_ = 0;
    std::vector<uint16_t> playerCol_, playerBorder_;

    void use(GLuint tex, int mode);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba);
    uint16_t tileColor(const Game& g, int t) const;
    void markDirty(int t);
};

}  // namespace fc
