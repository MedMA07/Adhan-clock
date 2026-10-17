#pragma once
#include <Arduino.h>
// 4-bit alpha bitmap, nibble-RLE compressed (see tools/gen_assets.py): high nibble first, row-major.
struct AlphaBmp { uint16_t w, h; const uint8_t* data; };
