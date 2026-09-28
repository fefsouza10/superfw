/*
 * Cover-art / title-screen preview for the ROM browser.  See coverart.h.
 *
 * Reads "/IMGS/{c0}/{c1}/{CODE}.bmp" (120x80, 16bpp X1R5G5B5) directly off the SD
 * card, scales it down to half size (2x2 average), maps each pixel to a fixed
 * 6x6x6 palette cube (MEM_PALETTE[20..235]) and caches the resulting 8bpp image
 * for fast per-frame blits.
 */
#include <string.h>
#include <stdbool.h>

#include "gbahw.h"
#include "fatfs/ff.h"
#include "common.h"
#include "nanoprintf.h"
#include "coverart.h"

#define COVER_DIR  "/IMGS"

// Keys go in EWRAM (.sbss); the default .bss lives in scarce IWRAM.
#define EWRAM_BSS  __attribute__((section(".sbss")))

// The 8bpp image lives in cart SDRAM (provided by the menu, see coverart_init)
// to save EWRAM. SDRAM is only written with 16 bit accesses.
static uint8_t *cover_pix;
static EWRAM_BSS char cover_key[MAX_FN_LEN];     // ROM path the current state belongs to
static bool     cover_have;               // a valid cover is loaded (.bss/IWRAM -> zeroed)
static uint16_t cube_pal[CUBE_NCOLORS];   // the fixed color cube (GBA BGR555)
static bool     cube_built;
// Selection that was requested but not loaded yet (see COVER_LOAD_DELAY).
static EWRAM_BSS char pending_key[MAX_FN_LEN];
static unsigned pending_cnt;

// Build the 6x6x6 cube once. Each channel uses 6 evenly spread 5-bit levels.
static void build_cube(void) {
  static const uint8_t lvl[6] = { 0, 6, 12, 19, 25, 31 };
  for (unsigned r = 0; r < 6; r++)
    for (unsigned g = 0; g < 6; g++)
      for (unsigned b = 0; b < 6; b++)
        cube_pal[r * 36 + g * 6 + b] = (lvl[b] << 10) | (lvl[g] << 5) | lvl[r];
  cube_built = true;
}

// 4x4 ordered-dither (Bayer) thresholds, 0..15.
static const uint8_t bayer4[4][4] = {
  {  0,  8,  2, 10 },
  { 12,  4, 14,  6 },
  {  3, 11,  1,  9 },
  { 15,  7, 13,  5 },
};

// Quantize the sum of four 5-bit samples (a 2x2 block, 0..124) to one of the
// 6 cube levels, dithered by `t` (0..15). sum * 20 / 31 maps 0..124 to 0..80
// (5 steps of 16); the threshold adds the fractional part so neighbouring
// pixels alternate between adjacent levels.
static inline unsigned dither6(unsigned sum, unsigned t) {
  unsigned q = (sum * 20 / 31 + t) >> 4;
  return q > 5 ? 5 : q;
}

// Averages a 2x2 block of BMP 16-bit pixels into a cube index (already biased
// by base), dithered by its screen position.
// The EZ-Flash-Omega pack stores pixels GBA-native (X1B5G5R5): red is the LOW
// 5 bits, blue the high 5 bits (not the standard X1R5G5B5 BMP layout).
static inline uint8_t block_to_cube(const uint8_t *r0, const uint8_t *r1,
                                    unsigned x0, unsigned x1, unsigned dx, unsigned dy) {
  unsigned p[4] = {
    r0[x0 * 2] | (r0[x0 * 2 + 1] << 8), r0[x1 * 2] | (r0[x1 * 2 + 1] << 8),
    r1[x0 * 2] | (r1[x0 * 2 + 1] << 8), r1[x1 * 2] | (r1[x1 * 2 + 1] << 8),
  };
  unsigned r = 0, g = 0, b = 0;
  for (unsigned i = 0; i < 4; i++) {
    r += p[i] & 0x1F;
    g += (p[i] >> 5) & 0x1F;
    b += (p[i] >> 10) & 0x1F;
  }
  unsigned t = bayer4[dy & 3][dx & 3];
  return CUBE_PAL_BASE + dither6(r, t) * 36 + dither6(g, t) * 6 + dither6(b, t);
}

static bool gcode_is_alnum(const uint8_t *c) {
  for (unsigned i = 0; i < 4; i++) {
    uint8_t ch = c[i];
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z')))
      return false;
  }
  return true;
}

static bool load_cover_file(const uint8_t gcode[4]) {
  char path[64];
  npf_snprintf(path, sizeof(path), "%s/%c/%c/%c%c%c%c.bmp",
               COVER_DIR, gcode[0], gcode[1],
               gcode[0], gcode[1], gcode[2], gcode[3]);

  FIL fd;
  if (FR_OK != f_open(&fd, path, FA_READ))
    return false;

  bool ok = false;
  UINT rd;
  uint8_t hdr[54];

  if (FR_OK == f_read(&fd, hdr, sizeof(hdr), &rd) && rd == sizeof(hdr) &&
      hdr[0] == 'B' && hdr[1] == 'M') {
    uint32_t dataoff = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
    int32_t  width   = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
    int32_t  rawh    = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
    unsigned bpp     = hdr[28] | (hdr[29] << 8);
    bool topdown = rawh < 0;
    int32_t height = topdown ? -rawh : rawh;

    if (bpp == 16 && width > 0 && width <= COVER_SRC_W &&
        height > 0 && height <= COVER_SRC_H && FR_OK == f_lseek(&fd, dataoff)) {
      if (!cube_built)
        build_cube();

      // Pad letterbox (smaller images) with cube index 0 (= black).
      dma_memset16(cover_pix, dup8(CUBE_PAL_BASE), COVER_W * COVER_H / 2);

      // The image is shown at half size: every 2x2 source block is averaged
      // into one pixel, so two source rows are read per output row.
      unsigned dw = ((unsigned)width + 1) / 2, dh = ((unsigned)height + 1) / 2;
      unsigned rowbytes = ((unsigned)width * 2 + 3) & ~3u;   // 4-byte aligned rows
      uint8_t rowbuf[2 * COVER_SRC_W * 2];
      __attribute__((aligned(4))) uint8_t rowpix[COVER_W];
      memset(rowpix, CUBE_PAL_BASE, sizeof(rowpix));
      ok = true;
      for (unsigned sy = 0; sy < (unsigned)height; sy += 2) {
        unsigned nrows = (sy + 1 < (unsigned)height) ? 2 : 1;
        if (FR_OK != f_read(&fd, rowbuf, rowbytes * nrows, &rd) || rd != rowbytes * nrows) {
          ok = false;
          break;
        }
        const uint8_t *r0 = rowbuf, *r1 = &rowbuf[(nrows - 1) * rowbytes];
        unsigned dy = topdown ? sy / 2 : dh - 1 - sy / 2;
        for (unsigned dx = 0; dx < dw; dx++) {
          unsigned x0 = dx * 2, x1 = MIN(x0 + 1, (unsigned)width - 1);
          rowpix[dx] = block_to_cube(r0, r1, x0, x1, dx, dy);
        }
        dma_memcpy16(&cover_pix[dy * COVER_W], rowpix, COVER_W / 2);
      }

      if (ok)
        dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], cube_pal, CUBE_NCOLORS);
    }
  }

  f_close(&fd);
  return ok;
}

void coverart_init(uint8_t *pixbuf) {
  cover_pix = pixbuf;
  coverart_invalidate();
}

void coverart_invalidate(void) {
  cover_key[0] = 0;
  pending_key[0] = 0;
  cover_have = false;
}

// Tracks the current selection and decides when its cover must be read.
// Returns true once `key` has been requested for COVER_LOAD_DELAY consecutive
// calls and is not the cover already loaded, so scrolling through a list does
// not hit the SD card for every entry it passes.
static bool needs_load(const char *key) {
  if (strncmp(pending_key, key, sizeof(pending_key) - 1)) {
    strncpy(pending_key, key, sizeof(pending_key) - 1);
    pending_key[sizeof(pending_key) - 1] = 0;
    pending_cnt = 0;
  }
  if (0 == strncmp(cover_key, key, sizeof(cover_key) - 1))
    return false;
  // Do not load while any key is held (scrolling, letter jumps): the delay
  // only starts counting once the user lets go.
  if ((REG_KEYINPUT & 0x3FF) != 0x3FF) {
    pending_cnt = 0;
    return false;
  }
  return ++pending_cnt >= COVER_LOAD_DELAY;
}

void coverart_update(const char *rom_fullpath, uint32_t filesize, bool is_gba) {
  if (!needs_load(rom_fullpath))
    return;

  strncpy(cover_key, rom_fullpath, sizeof(cover_key) - 1);
  cover_key[sizeof(cover_key) - 1] = 0;
  cover_have = false;

  if (!is_gba)
    return;

  t_rom_header romh;
  if (0 != preload_gba_rom(rom_fullpath, filesize, &romh))
    return;

  if (gcode_is_alnum(romh.gcode))
    cover_have = load_cover_file(romh.gcode);
}

void coverart_update_gcode(const char *cachekey, const uint8_t gcode[4]) {
  if (!needs_load(cachekey))
    return;

  strncpy(cover_key, cachekey, sizeof(cover_key) - 1);
  cover_key[sizeof(cover_key) - 1] = 0;
  cover_have = false;

  if (gcode_is_alnum(gcode))
    cover_have = load_cover_file(gcode);
}

bool coverart_available(void) {
  // Only show the loaded cover while its entry is still the selected one.
  return cover_have && !strncmp(cover_key, pending_key, sizeof(cover_key) - 1);
}

void coverart_draw(volatile uint8_t *frame) {
  if (!coverart_available())
    return;
  // Re-assert our palette every frame: the logo (info tab) shares the
  // MEM_PALETTE[20..235] range and may have overwritten the cube.
  dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], cube_pal, CUBE_NCOLORS);
  for (unsigned r = 0; r < COVER_H; r++)
    dma_memcpy16(&frame[(COVER_PANE_Y + r) * 240 + COVER_PANE_X],
                 &cover_pix[r * COVER_W], COVER_W / 2);
}
