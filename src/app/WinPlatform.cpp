// Windows-only parts: OpenGL function loading, music (MCI) and the flag
// (GDI+ image decoding + the standard "open file" dialog).
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <objidl.h>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>
#include "Flag.h"
#include "GL.h"
#include "Music.h"
#include "SDL.h"

// ---------------------------------------------------------------- OpenGL
namespace fcgl {
PFNGLCREATESHADERPROC CreateShader;
PFNGLSHADERSOURCEPROC ShaderSource;
PFNGLCOMPILESHADERPROC CompileShader;
PFNGLGETSHADERIVPROC GetShaderiv;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
PFNGLDELETESHADERPROC DeleteShader;
PFNGLCREATEPROGRAMPROC CreateProgram;
PFNGLATTACHSHADERPROC AttachShader;
PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
PFNGLLINKPROGRAMPROC LinkProgram;
PFNGLGETPROGRAMIVPROC GetProgramiv;
PFNGLDELETEPROGRAMPROC DeleteProgram;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
PFNGLUSEPROGRAMPROC UseProgram;
PFNGLUNIFORM4FPROC Uniform4f;
PFNGLUNIFORM2FPROC Uniform2f;
PFNGLUNIFORM1IPROC Uniform1i;
PFNGLUNIFORM1FPROC Uniform1f;
PFNGLACTIVETEXTUREPROC ActiveTexture;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;

bool load() {
    bool ok = true;
#define FC_LOAD(var, type, name) \
    var = (type)SDL_GL_GetProcAddress(name); \
    if (!var) ok = false;
    FC_LOAD(CreateShader, PFNGLCREATESHADERPROC, "glCreateShader")
    FC_LOAD(ShaderSource, PFNGLSHADERSOURCEPROC, "glShaderSource")
    FC_LOAD(CompileShader, PFNGLCOMPILESHADERPROC, "glCompileShader")
    FC_LOAD(GetShaderiv, PFNGLGETSHADERIVPROC, "glGetShaderiv")
    FC_LOAD(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog")
    FC_LOAD(DeleteShader, PFNGLDELETESHADERPROC, "glDeleteShader")
    FC_LOAD(CreateProgram, PFNGLCREATEPROGRAMPROC, "glCreateProgram")
    FC_LOAD(AttachShader, PFNGLATTACHSHADERPROC, "glAttachShader")
    FC_LOAD(BindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC, "glBindAttribLocation")
    FC_LOAD(LinkProgram, PFNGLLINKPROGRAMPROC, "glLinkProgram")
    FC_LOAD(GetProgramiv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv")
    FC_LOAD(DeleteProgram, PFNGLDELETEPROGRAMPROC, "glDeleteProgram")
    FC_LOAD(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation")
    FC_LOAD(UseProgram, PFNGLUSEPROGRAMPROC, "glUseProgram")
    FC_LOAD(Uniform4f, PFNGLUNIFORM4FPROC, "glUniform4f")
    FC_LOAD(Uniform2f, PFNGLUNIFORM2FPROC, "glUniform2f")
    FC_LOAD(Uniform1i, PFNGLUNIFORM1IPROC, "glUniform1i")
    FC_LOAD(Uniform1f, PFNGLUNIFORM1FPROC, "glUniform1f")
    FC_LOAD(ActiveTexture, PFNGLACTIVETEXTUREPROC, "glActiveTexture")
    FC_LOAD(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray")
    FC_LOAD(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer")
#undef FC_LOAD
    return ok;
}
}  // namespace fcgl

static std::wstring widen(const char* utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, 0, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    while (!w.empty() && w[w.size() - 1] == L'\0') w.erase(w.size() - 1);
    return w;
}

// ---------------------------------------------------------------- music (MCI plays MP3 natively)
static bool g_musicLoaded = false, g_musicPlaying = false;

int fc_music_load(const char* path) {
    std::wstring cmd = L"open \"" + widen(path) + L"\" type mpegvideo alias fcmusic";
    g_musicLoaded = mciSendStringW(cmd.c_str(), 0, 0, 0) == 0;
    if (g_musicLoaded) mciSendStringW(L"setaudio fcmusic volume to 600", 0, 0, 0);
    return g_musicLoaded ? 1 : 0;
}

void fc_music_set_playing(int playing) {
    if (!g_musicLoaded || (playing != 0) == g_musicPlaying) return;
    g_musicPlaying = playing != 0;
    mciSendStringW(g_musicPlaying ? L"play fcmusic repeat" : L"pause fcmusic", 0, 0, 0);
}

// ---------------------------------------------------------------- flag
static bool gdiStart() {
    static bool started = false, ok = false;
    if (!started) {
        started = true;
        Gdiplus::GdiplusStartupInput in;
        ULONG_PTR token = 0;
        ok = Gdiplus::GdiplusStartup(&token, &in, 0) == Gdiplus::Ok;
    }
    return ok;
}

// Loads an image and scales it onto a white background. Caller deletes.
static Gdiplus::Bitmap* loadScaled(const std::wstring& path, int maxSide) {
    if (!gdiStart()) return 0;
    Gdiplus::Bitmap src(path.c_str());
    if (src.GetLastStatus() != Gdiplus::Ok || src.GetWidth() < 1 || src.GetHeight() < 1) return 0;
    double k = std::min(1.0, (double)maxSide / std::max(src.GetWidth(), src.GetHeight()));
    int W = std::max(1, (int)(src.GetWidth() * k)), H = std::max(1, (int)(src.GetHeight() * k));
    Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(W, H, PixelFormat32bppARGB);
    Gdiplus::Graphics g(dst);
    g.Clear(Gdiplus::Color(255, 255, 255, 255));
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(&src, 0, 0, W, H);
    return dst;
}

unsigned char* fc_flag_decode(const char* path, int maxSide, int* w, int* h) {
    Gdiplus::Bitmap* bmp = loadScaled(widen(path), maxSide);
    if (!bmp) return NULL;
    int W = (int)bmp->GetWidth(), H = (int)bmp->GetHeight();
    unsigned char* px = (unsigned char*)malloc((size_t)W * H * 4);
    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, W, H);
    if (!px || bmp->LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        free(px);
        delete bmp;
        return NULL;
    }
    for (int y = 0; y < H; ++y) {
        const unsigned char* s = (const unsigned char*)bd.Scan0 + (size_t)y * bd.Stride;
        unsigned char* d = px + (size_t)y * W * 4;
        for (int x = 0; x < W; ++x) {  // BGRA -> RGBA
            d[x * 4 + 0] = s[x * 4 + 2];
            d[x * 4 + 1] = s[x * 4 + 1];
            d[x * 4 + 2] = s[x * 4 + 0];
            d[x * 4 + 3] = 255;
        }
    }
    bmp->UnlockBits(&bd);
    delete bmp;
    *w = W;
    *h = H;
    return px;
}

static bool pngEncoder(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<unsigned char> buf(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)&buf[0];
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT k = 0; k < num; ++k)
        if (wcscmp(info[k].MimeType, L"image/png") == 0) {
            *clsid = info[k].Clsid;
            return true;
        }
    return false;
}

static int g_flagPicked = 0;

int fc_flag_pick(const char* savePath) {
    wchar_t file[1024] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"Afbeeldingen (png, jpg, bmp, gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Alle bestanden\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = L"Kies een afbeelding voor je vlag";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return 1;  // cancelled: the dialog itself did work
    Gdiplus::Bitmap* bmp = loadScaled(file, 256);
    CLSID png;
    if (bmp && pngEncoder(&png) && bmp->Save(widen(savePath).c_str(), &png, 0) == Gdiplus::Ok) g_flagPicked = 1;
    delete bmp;
    return 1;
}

int fc_flag_picked(void) {
    int r = g_flagPicked;
    g_flagPicked = 0;
    return r;
}
#endif  // _WIN32
