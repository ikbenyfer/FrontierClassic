// Tiny PNG writer (RGB, uncompressed deflate). Used to save a drawn flag.
#pragma once
#include <cstdint>
#include <string>

namespace fc {

// rgba: w*h*4 bytes, row 0 = top. Alpha is ignored. Returns false on a write error.
bool writePngRGB(const std::string& path, const uint8_t* rgba, int w, int h);

}  // namespace fc
