/*
 * Cover-art / title-screen preview for the ROM browser.  See coverart.h.
 *
 * Reads "/COVERS/{c0}/{c1}/{CODE}.bmp" or "/IMGS/{c0}/{c1}/{CODE}.bmp" directly
 * off the SD card, either:
 *  - 8bpp with its own palette (<= 216 colors, <= 120x80, made by tools/covers),
 *    copied as is, or
 *  - 16bpp EZ-Flash-Omega (120x80), dithered to a fixed 6x6x6 palette cube.
 * Both are scaled down to the selected display size.
 * Images use MEM_PALETTE[20..235] and are kept in a cache in cart SDRAM, so
 * revisited and prefetched entries show instantly.
 */
#include <string.h>
#include <stdbool.h>

#include "gbahw.h"
#include "fatfs/ff.h"
#include "common.h"
#include "nanoprintf.h"
#include "coverart.h"

#define COVER_DIR     "/IMGS"      // EZ-Flash Omega pack
#define COVER_DIR_HQ  "/COVERS"    // tools/covers output

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
  uint32_t ownpal;                // pal holds the image palette (else: the cube)
  uint16_t pal[CUBE_NCOLORS];
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
static __attribute__((noinline)) void build_cube(void) {
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

static inline uint32_t rd32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// Fits a width x height image in the display size (never enlarging it).
static void fit_size(unsigned width, unsigned height, unsigned *dw, unsigned *dh) {
  if (width * COVER_H <= height * COVER_W) {
    *dh = MIN(height, COVER_H);
    *dw = MAX(1u, width * *dh / height);
  } else {
    *dw = MIN(width, COVER_W);
    *dh = MAX(1u, height * *dw / width);
  }
}

// Returns the output row sampling source row `r` (sy = dy * height / dh), or
// -1 if there is none.
static int row_for(unsigned r, unsigned height, unsigned dh) {
  unsigned dy = (r * dh + height - 1) / height;
  return (dy < dh && dy * height / dh == r) ? (int)dy : -1;
}

// 16bpp (EZ-Flash Omega) BMP: scaled to the display size (when shrinking,
// each output pixel averages a 2x2 source block) and mapped to the fixed color
// cube. Rows are streamed: an output row is emitted as soon as the source rows
// it needs have been read, whatever the row order of the file.
static bool load_bmp16(FIL *fd, unsigned width, unsigned height, bool topdown,
                       t_pix_ent *slot) {
  if (width > COVER_MAX_W || height > COVER_MAX_H)
    return false;
  if (!cube_built)
    build_cube();

  unsigned dw, dh;
  fit_size(width, height, &dw, &dh);
  unsigned ox = (COVER_W - dw) / 2, oy = (COVER_H - dh) / 2;
  bool avgx = dw < width, avgy = dh < height;
  uint8_t xs[COVER_MAX_W];
  for (unsigned dx = 0; dx < dw; dx++)
    xs[dx] = dx * width / dw;

  unsigned rowbytes = (width * 2 + 3) & ~3u;   // 4-byte aligned rows
  uint8_t rowbuf[2][COVER_MAX_W * 2];
  __attribute__((aligned(4))) uint8_t rowpix[COVER_MAX_W];
  memset(rowpix, CUBE_PAL_BASE, sizeof(rowpix));

  for (unsigned fr = 0; fr < height; fr++) {
    uint8_t *cur = rowbuf[fr & 1], *prev = rowbuf[(fr & 1) ^ 1];
    UINT rd;
    if (FR_OK != f_read(fd, cur, rowbytes, &rd) || rd != rowbytes)
      return false;

    // Image row just read, and the pair of rows (lo, lo + 1) now available.
    unsigned r = topdown ? fr : height - 1 - fr;
    const uint8_t *rlo = cur, *rhi = cur;
    unsigned lo = r;
    int dy;
    if (!avgy)
      dy = row_for(r, height, dh);
    else {
      if (fr == 0 && r != height - 1)
        continue;          // Needs the next row too
      if (fr) {
        lo = topdown ? r - 1 : r;
        rlo = topdown ? prev : cur;
        rhi = topdown ? cur : prev;
      }
      dy = row_for(lo, height, dh);
      if (dy < 0 && topdown && r == height - 1) {
        // A top-down file ends on its last row: emit rows sampling it alone.
        dy = row_for(r, height, dh);
        rlo = rhi = cur;
      }
    }
    if (dy < 0)
      continue;

    for (unsigned dx = 0; dx < dw; dx++) {
      unsigned x0 = xs[dx], x1 = avgx ? MIN(x0 + 1, width - 1) : x0;
      rowpix[ox + dx] = block_to_cube(rlo, rhi, x0, x1, dx, dy);
    }
    dma_memcpy16(&slot->pix[(oy + dy) * COVER_W], rowpix, COVER_W / 2);
  }
  slot->ownpal = 0;
  return true;
}

// 8bpp BMP with its own palette (at most CUBE_NCOLORS colors), as made by
// tools/covers: copied as is (nearest pixel when shrinking), much nicer than
// dithering to the fixed cube and faster to load.
static bool load_bmp8(FIL *fd, const uint8_t *hdr, unsigned width, unsigned height,
                      bool topdown, t_pix_ent *slot) {
  unsigned ncolors = rd32(&hdr[46]) ? rd32(&hdr[46]) : 256;
  if (width > COVER_MAX_W || height > COVER_MAX_H || ncolors > CUBE_NCOLORS)
    return false;

  // The palette follows the info header (BGRA quads).
  UINT rd;
  uint8_t quads[CUBE_NCOLORS * 4];
  if (FR_OK != f_lseek(fd, 14 + rd32(&hdr[14])) ||
      FR_OK != f_read(fd, quads, ncolors * 4, &rd) || rd != ncolors * 4)
    return false;
  uint16_t pal[CUBE_NCOLORS];
  memset(pal, 0, sizeof(pal));
  for (unsigned i = 0; i < ncolors; i++)
    pal[i] = (quads[i * 4 + 2] >> 3) | ((quads[i * 4 + 1] >> 3) << 5) | ((quads[i * 4] >> 3) << 10);
  dma_memcpy16(slot->pal, pal, CUBE_NCOLORS);

  if (FR_OK != f_lseek(fd, rd32(&hdr[10])))
    return false;
  unsigned dw, dh;
  fit_size(width, height, &dw, &dh);
  unsigned ox = (COVER_W - dw) / 2, oy = (COVER_H - dh) / 2;
  uint8_t xs[COVER_MAX_W];
  for (unsigned dx = 0; dx < dw; dx++)
    xs[dx] = dx * width / dw;

  unsigned rowbytes = (width + 3) & ~3u;
  uint8_t rowbuf[COVER_MAX_W];
  __attribute__((aligned(4))) uint8_t rowpix[COVER_MAX_W];
  memset(rowpix, CUBE_PAL_BASE, sizeof(rowpix));
  for (unsigned fr = 0; fr < height; fr++) {
    if (FR_OK != f_read(fd, rowbuf, rowbytes, &rd) || rd != rowbytes)
      return false;
    int dy = row_for(topdown ? fr : height - 1 - fr, height, dh);
    if (dy < 0)
      continue;
    for (unsigned dx = 0; dx < dw; dx++)
      rowpix[ox + dx] = CUBE_PAL_BASE + MIN(rowbuf[xs[dx]], ncolors - 1);
    dma_memcpy16(&slot->pix[(oy + dy) * COVER_W], rowpix, COVER_W / 2);
  }
  slot->ownpal = 1;
  return true;
}

// Reads the BMP for `gc` from `dir` into the cache slot.
static bool load_cover_from(const char *dir, uint32_t gc, t_pix_ent *slot) {
  char path[64];
  int c0 = gc & 0xFF, c1 = (gc >> 8) & 0xFF, c2 = (gc >> 16) & 0xFF, c3 = gc >> 24;
  npf_snprintf(path, sizeof(path), "%s/%c/%c/%c%c%c%c.bmp",
               dir, c0, c1, c0, c1, c2, c3);

  FIL fd;
  if (FR_OK != f_open(&fd, path, FA_READ))
    return false;

  bool ok = false;
  UINT rd;
  uint8_t hdr[54];

  if (FR_OK == f_read(&fd, hdr, sizeof(hdr), &rd) && rd == sizeof(hdr) &&
      hdr[0] == 'B' && hdr[1] == 'M') {
    int32_t  width   = rd32(&hdr[18]);
    int32_t  rawh    = rd32(&hdr[22]);
    unsigned bpp     = hdr[28] | (hdr[29] << 8);
    unsigned compr   = rd32(&hdr[30]);
    bool topdown = rawh < 0;
    int32_t height = topdown ? -rawh : rawh;

    if (width > 0 && height > 0) {
      // Pad letterbox (smaller images) with cube index 0 (= black).
      dma_memset16(slot->pix, dup8(CUBE_PAL_BASE), COVER_W * COVER_H / 2);
      if (bpp == 8 && compr == 0)
        ok = load_bmp8(&fd, hdr, width, height, topdown, slot);
      else if (bpp == 16 && FR_OK == f_lseek(&fd, rd32(&hdr[10])))
        ok = load_bmp16(&fd, width, height, topdown, slot);
    }
  }

  f_close(&fd);
  return ok;
}

// High quality covers (tools/covers) are looked up first, then the EZ-Flash
// Omega pack, so both can be installed side by side.
static bool load_cover_file(uint32_t gc, t_pix_ent *slot) {
  return load_cover_from(COVER_DIR_HQ, gc, slot) ||
         load_cover_from(COVER_DIR, gc, slot);
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
  if (!gcode_is_alnum(gc) || !load_cover_file(gc, &cache->pix[victim])) {
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

unsigned coverart_w = COVER_MAX_W, coverart_h = COVER_MAX_H;

void coverart_init(void *cachemem, unsigned size) {
  static const uint8_t sizes[COVER_SIZE_CNT][2] = {
    { 60, 40 }, { 60, 40 }, { 90, 60 }, { COVER_MAX_W, COVER_MAX_H },
  };
  size = MIN(size, COVER_SIZE_CNT - 1);
  coverart_w = sizes[size][0];
  coverart_h = sizes[size][1];
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
  coverart_draw_at(frame, COVER_PANE_X, COVER_PANE_Y);
}

// ------------------------------------------------------------- Carousel ---

void coverart_draw_at(volatile uint8_t *frame, unsigned x, unsigned y) {
  if (!coverart_available())
    return;
  // Re-assert our palette every frame: the logo (info tab) shares the
  // MEM_PALETTE[20..235] range and may have overwritten the cube.
  const t_pix_ent *slot = &cache->pix[sel_slot];
  dma_memcpy16(&MEM_PALETTE[CUBE_PAL_BASE], slot->ownpal ? slot->pal : cube_pal, CUBE_NCOLORS);
  for (unsigned r = 0; r < COVER_H; r++)
    dma_memcpy16(&frame[(y + r) * 240 + x], &slot->pix[r * COVER_W], COVER_W / 2);
}

int coverart_peek(const char *rom_fullpath, uint32_t filesize) {
  int slot = resolve_file(rom_fullpath, filesize, hash_str(rom_fullpath, 2166136261u), false);
  return slot >= 0 ? slot : -1;
}

int coverart_peek_gcode(const uint8_t gcode[4]) {
  uint32_t gc;
  memcpy(&gc, gcode, 4);
  int slot = resolve_gcode(gc, false);
  return slot >= 0 ? slot : -1;
}

uint32_t coverart_slot_id(int slot) {
  return slot >= 0 ? cache->pix[slot].gcode : 0;
}

void coverart_obj_palette(void) {
  if (!cube_built)
    build_cube();
  dma_memcpy16(&MEM_PALETTE[256 + OBJ_CUBE_BASE], cube_pal, CUBE_NCOLORS);
}

// Level (0..5) of the cube closest to a 5-bit channel value.
static inline unsigned cube_level(unsigned v) {
  return (v * 5 + 15) / 31;
}

void coverart_thumb(int slot, volatile uint16_t *tiles) {
  const t_pix_ent *e = &cache->pix[slot];
  uint8_t map[CUBE_NCOLORS];
  for (unsigned i = 0; i < CUBE_NCOLORS; i++) {
    if (e->ownpal) {
      unsigned c = e->pal[i];
      map[i] = OBJ_CUBE_BASE + cube_level(c & 31) * 36 + cube_level((c >> 5) & 31) * 6 +
               cube_level((c >> 10) & 31);
    } else
      map[i] = OBJ_CUBE_BASE + i;
  }
  // 64x32 sprite (8bpp, 1D mapping: 8x4 tiles of 8x8), picture centered.
  const unsigned x0 = (64 - THUMB_W) / 2;
  for (unsigned y = 0; y < THUMB_H; y++) {
    const uint8_t *srow = &e->pix[(y * COVER_H / THUMB_H) * COVER_W];
    for (unsigned x = 0; x < 64; x += 2) {
      unsigned px[2];
      for (unsigned k = 0; k < 2; k++) {
        unsigned tx = x + k;
        if (tx < x0 || tx >= x0 + THUMB_W)
          px[k] = 0;
        else {
          unsigned v = srow[(tx - x0) * COVER_W / THUMB_W];
          px[k] = map[MIN(v - CUBE_PAL_BASE, CUBE_NCOLORS - 1)];
        }
      }
      unsigned off = (((y >> 3) * 8 + (x >> 3)) * 64 + (y & 7) * 8 + (x & 7)) / 2;
      tiles[off] = px[0] | (px[1] << 8);
    }
  }
}
