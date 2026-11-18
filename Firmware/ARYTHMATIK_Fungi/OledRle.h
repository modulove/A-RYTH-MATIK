#pragma once
// Draws run-length coded SSD1306 pages (made by Firmware/tools/oled_rle.py) straight into the
// Adafruit_SSD1306 display buffer: no per-pixel drawBitmap() work, and the frames take about a
// third of the flash of plain bitmaps. Identical copy in every firmware folder that uses it.
#include <avr/pgmspace.h>
#include <stdint.h>

// Reverses the bits of a byte (for the 180-degree panel).
static inline uint8_t oledRleFlip(uint8_t b) {
  b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}

// Draws frame `frame` of a sprite from oled_rle.py with its left edge at column x0 and its top
// on display page page0 (y = 8 * page0). Pixels are ORed into the buffer (clear it first for a
// plain copy); columns outside 0..127 and pages outside 0..7 are skipped.
//   rle, ofs  the generated <PREFIX>_RLE and <PREFIX>_OFS arrays (flash)
//   w, pages  <PREFIX>_W and <PREFIX>_PAGES
//   mirror    flipped left-right
//   rotated   panel mounted 180 degrees (ROTATE_PANEL): the buffer is filled upside down, the way
//             Adafruit_SSD1306 does it with setRotation(2)
// Main loop only (not for interrupts): it takes about 0.1-0.5 ms per frame.
static void oledRleDraw(uint8_t *buf, const uint8_t *rle, const uint16_t *ofs, uint8_t frame,
                        uint8_t w, uint8_t pages, int16_t x0, uint8_t page0, bool mirror, bool rotated) {
  const uint8_t *p = rle + pgm_read_word(&ofs[frame]);
  uint16_t left = (uint16_t)w * pages;  // bytes still to decode
  uint8_t col = 0, page = 0;
  while (left) {
    uint8_t c = pgm_read_byte(p++);
    bool run = c >= 128;
    uint8_t n = run ? (uint8_t)(c - 125) : (uint8_t)(c + 1);
    uint8_t v = run ? pgm_read_byte(p++) : 0;
    if (n > left) n = (uint8_t)left;  // corrupt data can't write past the sprite
    left -= n;
    while (n--) {
      if (!run) v = pgm_read_byte(p++);
      if (v) {
        int16_t x = x0 + (mirror ? (int16_t)(w - 1 - col) : (int16_t)col);
        uint8_t pg = page0 + page;
        if (x >= 0 && x < 128 && pg < 8) {
          if (rotated) buf[(7 - pg) * 128 + (127 - x)] |= oledRleFlip(v);
          else buf[pg * 128 + x] |= v;
        }
      }
      if (++col == w) {
        col = 0;
        page++;
      }
    }
  }
}
