#include "Png.h"
#include <string.h>
#include <vector>
#include "../core/Json.h"

namespace fc {

static uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static void be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(&out[start], out.size() - start));
}

bool writePngRGB(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    // raw scanlines: filter byte 0 + RGB
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            raw.push_back(p[0]);
            raw.push_back(p[1]);
            raw.push_back(p[2]);
        }
    }
    // zlib stream with "stored" (uncompressed) deflate blocks
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    do {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(n & 255));
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 255));
        z.push_back((uint8_t)((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;  // adler32
    for (size_t i = 0; i < raw.size(); ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> png;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w);
    be32(ihdr, (uint32_t)h);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: RGB
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", std::vector<uint8_t>());
    return writeFileAtomic(path, std::string(png.begin(), png.end()));
}

}  // namespace fc
