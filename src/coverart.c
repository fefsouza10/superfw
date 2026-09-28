/*
 * Cover-art / title-screen preview for the ROM browser.  See coverart.h.
 *
 * Reads "/IMGS/{c0}/{c1}/{CODE}.bmp" (120x80, 16bpp X1R5G5B5) directly off the SD
 * card, scales it down to half size (2x2 average), maps each pixel to a fixed
 * 6x6x6 palette cube (MEM_PALETTE[20..235]) and keeps the resulting 8bpp image
 * in a cache in cart SDRAM, so revisited and prefetched entries show instantly.
 */
#include <string.h>
#include <stdbool.h>

#include "gbahw.h"
#include "fatfs/ff.h"
#include "common.h"
#include "nanoprintf.h"
#include "coverart.h"

#define COVER_DIR  "/IMGS"

// Cache layout (lives in cart SDRAM, provided by the menu). SDRAM is only
// written with 16/32 bit accesses, so every field is a word.
#define GCODE_SLOTS     1024      // ROM path -> game code (direct mapped)
#define MISS_SLOTS      64        // game codes known to have no cover (ring)

#define GC_UNKNOWN      0         // Slot unused
#define GC_NOGAME       1         // Not a GBA ROM / unreadable header
#define GC_VALID        2         // gcode field is valid

typedef struct {
  uint32_t keyhash;               // Hash of the ROM path
  uint32_t fsize;                 // File size (part of the key)
  uint32_t gcode;                 // Game code (4 chars)
  uint32_t state;                 // GC_*
} t_gcode_ent;

typedef struct {
  uint32_t gcode;                 // 0 = empty slot
  uint32_t lastuse;               // LRU stamp
  uint8_t pix[COVER_BUF_SIZE];
} t_pix_ent;

typedef struct {
  t_gcode_ent gcodes[GCODE_SLOTS];
  uint32_t missing[MISS_SLOTS];
  t_pix_ent pix[COVER_CACHE_SLOTS];
} t_cover_cache;

_Static_assert (sizeof(t_cover_cache) <= COVER_CACHE_SIZE, "cover cache fits its buffer");
_Static_assert (COVER_BUF_SIZE % 4 == 0, "cover buffer is word-sized");

static t_cover_cache *cache;
static bool     cube_built;
static uint16_t cube_pal[CUBE_NCOLORS];   // the fixed color cube (GBA BGR555)
static uint8_t  q80[125];                 // 2x2 sum (0..124) -> 0..80 level scale

// Current selection state.
static uint32_t sel_key;          // Hash identifying the selected entry
static int      sel_slot;         // Pixel slot to draw (-1: none)
static bool     sel_resolved;     // Nothing left to load for the selection
static unsigned sel_wait;         // Idle frames spent on the selection
static bool     prefetch_done;    // Neighbours of the selection are cached
static bool     io_used;          // An SD read already happened this frame
static uint32_t use_stamp;
static unsigned miss_next;

// Build the 6x6x6 cube once. Each channel uses 6 evenly spread 5-bit levels.
static void build_cube(void) {
  static const uint8_t lvl[6] = { 0, 6, 12, 19, 25, 31 };
  for (unsigned r = 0; r < 6; r++)
    for (unsigned g = 0; g < 6; g++)
      for (unsigned b = 0; b < 6; b++)
        cube_pal[r * 36 + g * 6 + b] = (lvl[b] << 10) | (lvl[g] << 5) | lvl[r];
  // sum * 20 / 31 maps the sum of four 5-bit samples to 0..80 (5 steps of 16).
  for (unsigned s = 0; s < sizeof(q80); s++)
    q80[s] = s * 20 / 31;
  cube_built = true;
}

// 4x4 ordered-dither (Bayer) thresholds, 0..15.
static const uint8_t bayer4[4][4] = {
  {  0,  8,  2, 10 },
  { 12,  4, 14,  6 },
  {  3, 11,  1,  9 },
  { 15,  7, 13,  5 },
};

// Quantize the sum of a 2x2 block channel (0..124) to one of the 6 cube levels,
// dithered by `t` (0..15): the threshold adds the fractional part so
// neighbouring pixels alternate between adjacent levels.
static inline unsigned dither6(unsigned sum, unsigned t) {
  unsigned q = (q80[sum] + t) >> 4;
  return q > 5 ? 5 : q;
}

// Spreads a X1B5G5R5 pixel so that four of them can be summed at once:
// red at bits 0..6, blue at 10..16 and green at 21..27 (7 bits each).
static inline uint32_t spread(const uint8_t *p) {
  uint32_t v = p[0] | (p[1] << 8);
  return (v & 0x7C1F) | ((v & 0x03E0) << 16);
}

// Averages a 2x2 block of BMP 16-bit pixels into a cube index (already biased
// by base), dithered by its screen position.
// The EZ-Flash-Omega pack stores pixels GBA-native (X1B5G5R5): red is the LOW
// 5 bits, blue the high 5 bits (not the standard X1R5G5B5 BMP layout).
static inline uint8_t block_to_cube(const uint8_t *r0, const uint8_t *r1,
                                    unsigned x0, unsigned x1, unsigned dx, unsigned dy) {
  uint32_t s = spread(&r0[x0 * 2]) + spread(&r0[x1 * 2]) +
               spread(&r1[x0 * 2]) + spread(&r1[x1 * 2]);
  unsigned t = bayer4[dy & 3][dx & 3];
  return CUBE_PAL_BASE + dither6(s & 0x7F, t) * 36 +
                         dither6((s >> 21) & 0x7F, t) * 6 +
                         dither6((s >> 10) & 0x7F, t);
}

static bool gcode_is_alnum(uint32_t gc) {
  for (unsigned i = 0; i < 4; i++) {
    uint8_t ch = gc >> (i * 8);
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z')))
      return false;
  }
  return true;
}

// Reads and converts the BMP for `gc` into `pix`.
static bool load_cover_file(uint32_t gc, uint8_t *pix) {
  char path[64];
  int c0 = gc & 0xFF, c1 = (gc >> 8) & 0xFF, c2 = (gc >> 16) & 0xFF, c3 = gc >> 24;
  npf_snprintf(path, sizeof(path), "%s/%c/%c/%c%c%c%c.bmp",
               COVER_DIR, c0, c1, c0, c1, c2, c3);

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
      dma_memset16(pix, dup8(CUBE_PAL_BASE), COVER_W * COVER_H / 2);

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
        dma_memcpy16(&pix[dy * COVER_W], rowpix, COVER_W / 2);
      }
    }
  }

  f_close(&fd);
  return ok;
}

// 32 bit FNV-1a.
static uint32_t hash_str(const char *s, uint32_t h) {
  while (*s)
    h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

static t_gcode_ent *gcode_ent(uint32_t keyhash) {
  return &cache->gcodes[keyhash % GCODE_SLOTS];
}

static bool is_missing(uint32_t gc) {
  for (unsigned i = 0; i < MISS_SLOTS; i++)
    if (cache->missing[i] == gc)
      return true;
  return false;
}

static int find_pix(uint32_t gc) {
  for (unsigned i = 0; i < COVER_CACHE_SLOTS; i++)
    if (cache->pix[i].gcode == gc) {
      cache->pix[i].lastuse = ++use_stamp;
      return i;
    }
  return -1;
}

// Loads the cover for `gc` into the least recently used slot.
static int load_pix(uint32_t gc) {
  // Never evict the cover being displayed.
  unsigned victim = sel_slot == 0 ? 1 : 0;
  for (unsigned i = 0; i < COVER_CACHE_SLOTS; i++)
    if ((int)i != sel_slot && cache->pix[i].lastuse < cache->pix[victim].lastuse)
      victim = i;

  io_used = true;
  cache->pix[victim].gcode = 0;
  if (!gcode_is_alnum(gc) || !load_cover_file(gc, cache->pix[victim].pix)) {
    cache->missing[miss_next++ % MISS_SLOTS] = gc;
    return -1;
  }
  cache->pix[victim].gcode = gc;
  cache->pix[victim].lastuse = ++use_stamp;
  return victim;
}

// Resolves a game code to a cover slot. Returns -1 if it has no cover, -2 if
// it is not cached and `allow_io` is false.
static int resolve_gcode(uint32_t gc, bool allow_io) {
  if (is_missing(gc))
    return -1;
  int slot = find_pix(gc);
  if (slot >= 0)
    return slot;
  return allow_io ? load_pix(gc) : -2;
}

// Same for a ROM file, reading its header when the game code is not cached.
static int resolve_file(const char *path, uint32_t fsize, uint32_t keyhash, bool allow_io) {
  t_gcode_ent *e = gcode_ent(keyhash);
  if (e->state == GC_UNKNOWN || e->keyhash != keyhash || e->fsize != fsize) {
    if (!allow_io)
      return -2;
    t_rom_header romh;
    io_used = true;
    uint32_t state = GC_NOGAME, gc = 0;
    if (!preload_gba_rom(path, fsize, &romh)) {
      memcpy(&gc, romh.gcode, 4);
      state = GC_VALID;
    }
    e->keyhash = keyhash;
    e->fsize = fsize;
    e->gcode = gc;
    e->state = state;
    // Only one SD operation per call: the image loads on the next one.
    if (state == GC_VALID && !is_missing(gc) && find_pix(gc) < 0)
      return -2;
  }
  return e->state == GC_VALID ? resolve_gcode(e->gcode, allow_io) : -1;
}

static bool keys_idle(void) {
  return (REG_KEYINPUT & 0x3FF) == 0x3FF;
}

// Common per-frame selection handling. Returns whether an SD read is allowed.
static bool select_key(uint32_t key) {
  io_used = false;
  if (key != sel_key) {
    sel_key = key;
    sel_slot = -1;
    sel_resolved = false;
    sel_wait = 0;
    prefetch_done = false;
  }
  if (!keys_idle()) {
    sel_wait = 0;
    return false;
  }
  if (sel_wait < 255)
    sel_wait++;
  return sel_wait >= COVER_LOAD_DELAY;
}

static void select_result(int slot) {
  if (slot != -2) {
    sel_slot = slot;
    sel_resolved = true;
  }
}

void coverart_init(void *cachemem) {
  cache = (t_cover_cache*)cachemem;
  // Clear it in chunks (the DMA count is 16 bits).
  for (unsigned off = 0; off < sizeof(t_cover_cache); off += 0x8000)
    dma_memset16((uint8_t*)cache + off, 0, MIN(0x8000u, sizeof(t_cover_cache) - off) / 2);
  miss_next = 0;
  use_stamp = 0;
  coverart_invalidate();
}

void coverart_invalidate(void) {
  sel_key = 0;
  sel_slot = -1;
  sel_resolved = true;
  prefetch_done = true;
}

void coverart_update(const char *rom_fullpath, uint32_t filesize, bool is_gba) {
  uint32_t key = hash_str(rom_fullpath, 2166136261u);
  bool io = select_key(key);
  if (!is_gba)
    select_result(-1);
  else if (!sel_resolved)
    select_result(resolve_file(rom_fullpath, filesize, key, io));
}

void coverart_update_gcode(const uint8_t gcode[4]) {
  uint32_t gc;
  memcpy(&gc, gcode, 4);
  bool io = select_key(gc ^ 0x5A5A5A5A);
  if (!sel_resolved)
    select_result(resolve_gcode(gc, io));
}

bool coverart_prefetch_ready(void) {
  return sel_resolved && !prefetch_done && !io_used && keys_idle() &&
         sel_wait >= COVER_PREFETCH_DELAY;
}

void coverart_prefetch_finished(void) {
  prefetch_done = true;
}

bool coverart_prefetch(const char *rom_fullpath, uint32_t filesize) {
  resolve_file(rom_fullpath, filesize, hash_str(rom_fullpath, 2166136261u), true);
  return io_used;
}

bool coverart_prefetch_gcode(const uint8_t gcode[4]) {
  uint32_t gc;
  memcpy(&gc, gcode, 4);
  resolve_gcode(gc, true);
  return io_used;
}

bool coverart_available(void) {
  return sel_slot >= 0;
}

void coverart_draw(volatile uint8_t *frame) {
  if (!coverart_available())
    return;
  // Re-assert our palette every frame: the logo (info tab) shares the
  // MEM_PALETTE[20..235] range and may have overwritten the cube.
  dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], cube_pal, CUBE_NCOLORS);
  const uint8_t *pix = cache->pix[sel_slot].pix;
  for (unsigned r = 0; r < COVER_H; r++)
    dma_memcpy16(&frame[(COVER_PANE_Y + r) * 240 + COVER_PANE_X],
                 &pix[r * COVER_W], COVER_W / 2);
}
