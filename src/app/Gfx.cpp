#include "Gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../core/Game.h"
#include "SDL.h"

namespace fc {

static const char* kVS =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "attribute vec4 aCol;\n"
    "uniform vec4 uProj;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos.x * uProj.x + uProj.z, aPos.y * uProj.y + uProj.w, 0.0, 1.0);\n"
    "  vUV = aUV;\n"
    "  vCol = aCol;\n"
    "}\n";

static const char* kFS =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "  vec4 t = texture2D(uTex, vUV);\n"
    "  if (uAlpha > 0.5) gl_FragColor = vec4(vCol.rgb, vCol.a * t.a);\n"
    "  else gl_FragColor = vCol * t;\n"
    "}\n";

static GLuint compile(GLenum type, const char* src, std::string* err) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, 0, log);
        if (err) *err = std::string("shader: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Gfx::init(const std::string& resDir, std::string* err) {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS, err), fs = compile(GL_FRAGMENT_SHADER, kFS, err);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glBindAttribLocation(prog_, 0, "aPos");
    glBindAttribLocation(prog_, 1, "aUV");
    glBindAttribLocation(prog_, 2, "aCol");
    glLinkProgram(prog_);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        if (err) *err = "shader link failed";
        return false;
    }
    locProj_ = glGetUniformLocation(prog_, "uProj");
    locTex_ = glGetUniformLocation(prog_, "uTex");
    locAlpha_ = glGetUniformLocation(prog_, "uAlpha");

    // font
    std::string meta, bin;
    if (!readFile(resDir + "font/font.json", meta) || !readFile(resDir + "font/font.bin", bin)) {
        if (err) *err = "font files missing";
        return false;
    }
    JVal j;
    if (!JVal::parse(meta, j, err)) return false;
    int aw = j["atlasW"].i(), ah = j["atlasH"].i();
    if ((int)bin.size() != aw * ah) {
        if (err) *err = "font atlas size mismatch";
        return false;
    }
    fontSize_ = (float)j["size"].num(30);
    fontAscent_ = (float)j["ascent"].num(24);
    memset(glyphs_, 0, sizeof glyphs_);
    for (int c = 32; c < 127; ++c) {
        char key[8];
        snprintf(key, sizeof key, "%d", c);
        const JVal& g = j["glyphs"][key];
        Glyph& gl = glyphs_[c - 32];
        gl.x = (float)g[0].num() / aw;
        gl.y = (float)g[1].num() / ah;
        gl.w = (float)g[2].num();
        gl.h = (float)g[3].num();
        gl.xoff = (float)g[4].num();
        gl.yoff = (float)g[5].num();
        gl.adv = (float)g[6].num();
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, aw, ah, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bin.data());
    verts_.reserve(6 * 4096);
    return true;
}

void Gfx::shutdown() {
    if (mapTex_) glDeleteTextures(1, &mapTex_);
    if (fontTex_) glDeleteTextures(1, &fontTex_);
    if (prog_) glDeleteProgram(prog_);
    mapTex_ = fontTex_ = prog_ = 0;
}

void Gfx::beginFrame(int winW, int winH, int drawW, int drawH) {
    winW_ = winW;
    winH_ = winH;
    drawW_ = drawW;
    drawH_ = drawH;
    glViewport(0, 0, drawW, drawH);
    glClearColor(0.07f, 0.11f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    // logical pixels -> clip space (y down)
    glUniform4f(locProj_, 2.0f / winW, -2.0f / winH, -1.0f, 1.0f);
    glUniform1i(locTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    curTex_ = 0;
    verts_.clear();
}

void Gfx::use(GLuint tex, bool alphaTex) {
    if (tex == curTex_ && alphaTex == curAlpha_) return;
    flush();
    curTex_ = tex;
    curAlpha_ = alphaTex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1f(locAlpha_, alphaTex ? 1.0f : 0.0f);
}

void Gfx::flush() {
    if (verts_.empty()) return;
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    const char* base = (const char*)&verts_[0];
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), base);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), base + 8);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), base + 16);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());
    verts_.clear();
}

void Gfx::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c) {
    Vtx a = {x0, y0, u0, v0, (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
    Vtx b = a, cc = a, d = a;
    b.x = x1; b.u = u1;
    cc.x = x1; cc.y = y1; cc.u = u1; cc.v = v1;
    d.y = y1; d.v = v1;
    verts_.push_back(a);
    verts_.push_back(b);
    verts_.push_back(cc);
    verts_.push_back(a);
    verts_.push_back(cc);
    verts_.push_back(d);
    if (verts_.size() > 6 * 4000) flush();
}

// the font atlas has a solid white block at (0..3, 0..3)
static const float kSolidU = 1.5f / 512.0f, kSolidV = 1.5f / 256.0f;

void Gfx::rect(float x, float y, float w, float h, uint32_t rgba) {
    use(fontTex_, true);
    quad(x, y, x + w, y + h, kSolidU, kSolidV, kSolidU, kSolidV, rgba);
}

void Gfx::frame(float x, float y, float w, float h, float t, uint32_t rgba) {
    rect(x, y, w, t, rgba);
    rect(x, y + h - t, w, t, rgba);
    rect(x, y + t, t, h - 2 * t, rgba);
    rect(x + w - t, y + t, t, h - 2 * t, rgba);
}

void Gfx::line(float x0, float y0, float x1, float y1, float t, uint32_t c) {
    use(fontTex_, true);
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    Vtx v[4];
    float px[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx};
    float py[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};
    for (int k = 0; k < 4; ++k) {
        Vtx q = {px[k], py[k], kSolidU, kSolidV, (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
        v[k] = q;
    }
    verts_.push_back(v[0]); verts_.push_back(v[1]); verts_.push_back(v[2]);
    verts_.push_back(v[0]); verts_.push_back(v[2]); verts_.push_back(v[3]);
}

void Gfx::disc(float cx, float cy, float r, uint32_t c) {
    use(fontTex_, true);
    const int n = 12;
    for (int k = 0; k < n; ++k) {
        float a0 = k * 6.2831853f / n, a1 = (k + 1) * 6.2831853f / n;
        Vtx o = {cx, cy, kSolidU, kSolidV, (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
        Vtx p = o, q = o;
        p.x = cx + std::cos(a0) * r; p.y = cy + std::sin(a0) * r;
        q.x = cx + std::cos(a1) * r; q.y = cy + std::sin(a1) * r;
        verts_.push_back(o); verts_.push_back(p); verts_.push_back(q);
    }
}

float Gfx::textWidth(const std::string& s, float size) const {
    float sc = size / fontSize_, w = 0;
    for (size_t k = 0; k < s.size(); ++k) {
        int c = (unsigned char)s[k];
        if (c < 32 || c > 126) c = '?';
        w += glyphs_[c - 32].adv * sc;
    }
    return w;
}

float Gfx::text(float x, float y, const std::string& s, float size, uint32_t rgba, int align) {
    float w = textWidth(s, size);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    use(fontTex_, true);
    float sc = size / fontSize_;
    float pen = x;
    for (size_t k = 0; k < s.size(); ++k) {
        int c = (unsigned char)s[k];
        if (c < 32 || c > 126) c = '?';
        const Glyph& g = glyphs_[c - 32];
        if (g.w > 0) {
            float x0 = pen + g.xoff * sc, y0 = y + g.yoff * sc;
            quad(x0, y0, x0 + g.w * sc, y0 + g.h * sc, g.x, g.y, g.x + g.w / 512.0f, g.y + g.h / 256.0f, rgba);
        }
        pen += g.adv * sc;
    }
    return w;
}

float Gfx::textShadow(float x, float y, const std::string& s, float size, uint32_t rgba, int align) {
    uint32_t sh = 0x000000u | ((rgba & 0xff) * 3 / 4);
    text(x + 1, y + 1, s, size, sh, align);
    return text(x, y, s, size, rgba, align);
}

// ------------------------------------------------------------------ map texture

static uint16_t rgb565(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void unpack565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) << 3;
    g = ((c >> 5) & 63) << 2;
    b = (c & 31) << 3;
}

static const int kTerrainRGB[7][3] = {
    {28, 58, 104},   // deep water
    {48, 92, 146},   // shallow water
    {112, 150, 86},  // plains
    {136, 136, 92},  // hills
    {124, 114, 106}, // mountains
    {204, 184, 128}, // desert
    {196, 206, 204}, // tundra
};

void Gfx::setMap(const Game& g) {
    mapW_ = g.map.w;
    mapH_ = g.map.h;
    const int n = mapW_ * mapH_;
    terrainPix_.resize(n);
    for (int t = 0; t < n; ++t) {
        int tr = g.map.terrain(t);
        if (tr > 6) tr = 2;
        uint32_t h = (uint32_t)t * 2654435761u;
        int v = (int)((h >> 27) & 7) - 3;  // small per-tile variation
        double f = g.map.countryBorder(t) ? 0.82 : 1.0;
        terrainPix_[t] = rgb565((int)((kTerrainRGB[tr][0] + v * 2) * f), (int)((kTerrainRGB[tr][1] + v * 2) * f),
                                (int)((kTerrainRGB[tr][2] + v * 2) * f));
    }
    playerCol_.assign(g.players.size(), 0);
    playerBorder_.assign(g.players.size(), 0);
    for (size_t p = 0; p < g.players.size(); ++p) {
        uint32_t c = g.players[p].color;
        int r = (c >> 16) & 255, gg = (c >> 8) & 255, b = c & 255;
        playerCol_[p] = rgb565(r, gg, b);
        playerBorder_[p] = rgb565(r * 55 / 100, gg * 55 / 100, b * 55 / 100);
    }
    pix_.resize(n);
    for (int t = 0; t < n; ++t) pix_[t] = tileColor(g, t);
    chunksX_ = (mapW_ + 63) / 64;
    chunksY_ = (mapH_ + 63) / 64;
    dirty_.assign(chunksX_ * chunksY_, 0);
    upload_.resize(64 * 64);

    if (!mapTex_) glGenTextures(1, &mapTex_);
    glBindTexture(GL_TEXTURE_2D, mapTex_);
    curTex_ = 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mapW_, mapH_, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &pix_[0]);
}

uint16_t Gfx::tileColor(const Game& g, int t) const {
    int o = g.owner[t];
    if (!o || o >= (int)playerCol_.size()) return terrainPix_[t];
    int x = t % mapW_, y = t / mapW_;
    bool border = (x > 0 && g.owner[t - 1] != o) || (x < mapW_ - 1 && g.owner[t + 1] != o) ||
                  (y > 0 && g.owner[t - mapW_] != o) || (y < mapH_ - 1 && g.owner[t + mapW_] != o);
    if (border) return playerBorder_[o];
    int r1, g1, b1, r2, g2, b2;
    unpack565(terrainPix_[t], r1, g1, b1);
    unpack565(playerCol_[o], r2, g2, b2);
    return rgb565((r1 * 2 + r2 * 3) / 5, (g1 * 2 + g2 * 3) / 5, (b1 * 2 + b2 * 3) / 5);
}

void Gfx::markDirty(int t) {
    int x = t % mapW_, y = t / mapW_;
    dirty_[(y >> 6) * chunksX_ + (x >> 6)] = 1;
}

void Gfx::updateMap(Game& g) {
    if (!mapTex_ || g.map.w != mapW_ || g.map.h != mapH_) { g.changed.clear(); return; }
    if (g.players.size() != playerCol_.size()) { setMap(g); g.changed.clear(); return; }
    const std::vector<int>& ch = g.changed;
    for (size_t k = 0; k < ch.size(); ++k) {
        int t = ch[k];
        int x = t % mapW_, y = t / mapW_;
        pix_[t] = tileColor(g, t);
        markDirty(t);
        if (x > 0) { pix_[t - 1] = tileColor(g, t - 1); markDirty(t - 1); }
        if (x < mapW_ - 1) { pix_[t + 1] = tileColor(g, t + 1); markDirty(t + 1); }
        if (y > 0) { pix_[t - mapW_] = tileColor(g, t - mapW_); markDirty(t - mapW_); }
        if (y < mapH_ - 1) { pix_[t + mapW_] = tileColor(g, t + mapW_); markDirty(t + mapW_); }
    }
    g.changed.clear();
    int count = 0;
    for (size_t k = 0; k < dirty_.size(); ++k) count += dirty_[k];
    if (!count) return;
    flush();
    glBindTexture(GL_TEXTURE_2D, mapTex_);
    curTex_ = 0;
    if (count * 2 > (int)dirty_.size()) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mapW_, mapH_, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &pix_[0]);
    } else {
        for (int cy = 0; cy < chunksY_; ++cy)
            for (int cx = 0; cx < chunksX_; ++cx) {
                if (!dirty_[cy * chunksX_ + cx]) continue;
                int x0 = cx * 64, y0 = cy * 64;
                int w = std::min(64, mapW_ - x0), h = std::min(64, mapH_ - y0);
                for (int r = 0; r < h; ++r) memcpy(&upload_[r * w], &pix_[(y0 + r) * mapW_ + x0], w * 2);
                glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &upload_[0]);
            }
    }
    std::fill(dirty_.begin(), dirty_.end(), 0);
}

void Gfx::drawMap(float camX, float camY, float zoom) {
    if (!mapTex_) return;
    use(mapTex_, false);
    float x0 = (0 - camX) * zoom + winW_ * 0.5f, y0 = (0 - camY) * zoom + winH_ * 0.5f;
    float x1 = (mapW_ - camX) * zoom + winW_ * 0.5f, y1 = (mapH_ - camY) * zoom + winH_ * 0.5f;
    quad(x0, y0, x1, y1, 0, 0, 1, 1, 0xffffffff);
    flush();
}

bool Gfx::screenshotBMP(const std::string& path) {
    flush();
    std::vector<uint8_t> px((size_t)drawW_ * drawH_ * 4);
    glReadPixels(0, 0, drawW_, drawH_, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, drawW_, drawH_, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < drawH_; ++y)
        memcpy((uint8_t*)s->pixels + y * s->pitch, &px[(size_t)(drawH_ - 1 - y) * drawW_ * 4], drawW_ * 4);
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}  // namespace fc
