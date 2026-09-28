/*
 * SuperFW video player (.gbv files).
 *
 * SuperFW loads this player to the cart SDRAM and appends the .gbv file right
 * after it (4-byte aligned). The video is decoded from SDRAM straight into a
 * double-buffered Mode 4 screen, and the IMA ADPCM audio is played through
 * DirectSound A, restarted every V-blank.
 *
 * File format (little endian):
 *   Header (64 bytes): "GBV1", u16 width, u16 height, u16 samples per vblank,
 *   u16 reserved, u32 frame count, u32 index offset, u32 total vblanks,
 *   char title[40].
 *   Frames: u32 size, u8 flags (1: palette), u8 duration (vblanks),
 *   u16 audio bytes, [u16 palette[256]], [audio chunk], video data.
 *   Audio chunk: s16 predictor, u8 step index, u8 pad, 4-bit samples.
 *   Video data: 2-bit op per 8x8 macroblock (skip, fill, split), packed four
 *   per byte, then a byte stream. A split macroblock has one byte with four
 *   2-bit ops for its 4x4 blocks (skip, fill, two-color, raw) and their data.
 *   Skipped blocks keep what the back buffer had (the frame before the
 *   previous one), which is what the encoder assumes.
 *   Index: u32 count, then {u32 frame, u32 offset, u32 vblank} per keyframe.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#include <stdint.h>
#include <stdbool.h>
#include "font.h"

#define IWRAM_CODE  __attribute__((section(".iwram_code"), long_call, target("arm"), noinline))
#define EWRAM_BSS   __attribute__((section(".ewram_bss")))

#define REG16(a)    (*(volatile uint16_t*)(a))
#define REG32(a)    (*(volatile uint32_t*)(a))
#define REG_DISPCNT   REG16(0x04000000)
#define REG_DISPSTAT  REG16(0x04000004)
#define REG_BG2PA     REG16(0x04000020)
#define REG_BG2PB     REG16(0x04000022)
#define REG_BG2PC     REG16(0x04000024)
#define REG_BG2PD     REG16(0x04000026)
#define REG_BG2X      REG32(0x04000028)
#define REG_BG2Y      REG32(0x0400002C)
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
#define REG_FIFO_A    0x040000A0
#define REG_DMA1SAD   REG32(0x040000BC)
#define REG_DMA1DAD   REG32(0x040000C0)
#define REG_DMA1CNT_H REG16(0x040000C6)
#define REG_TM0CNT_L  REG16(0x04000100)
#define REG_TM0CNT_H  REG16(0x04000102)
#define REG_KEYINPUT  REG16(0x04000130)
#define REG_IE        REG16(0x04000200)
#define REG_IF        REG16(0x04000202)
#define REG_IME       REG16(0x04000208)
#define BIOS_IF       REG16(0x03007FF8)
#define IRQ_HANDLER   REG32(0x03007FFC)

#define MEM_PAL       ((volatile uint16_t*)0x05000000)
#define MEM_VRAM      ((volatile uint8_t*)0x06000000)
#define MEM_OAM       ((volatile uint16_t*)0x07000000)
#define OBJ_TILES     ((volatile uint32_t*)0x06014000)   // Tile 512 onwards (bitmap modes)

#define KEY_A       0x001
#define KEY_B       0x002
#define KEY_SELECT  0x004
#define KEY_START   0x008
#define KEY_RIGHT   0x010
#define KEY_LEFT    0x020
#define KEY_UP      0x040
#define KEY_DOWN    0x080
#define KEY_R       0x100
#define KEY_L       0x200

#define VBLANK_HZ   59.7275
#define MAX_DUR     8          // Max vblanks per video frame
#define MAX_SPV     304        // Max audio samples per vblank
#define OSD_H       32
#define OSD_TIME    150        // OSD visible time (vblanks) after a key press

typedef struct {
  char magic[4];
  uint16_t width, height;
  uint16_t spv, rsv;
  uint32_t nframes;
  uint32_t index_off;
  uint32_t total_vblanks;
  char title[40];
} t_gbv_header;

typedef struct {
  uint32_t frame, offset, vblank;
} t_gbv_index;

extern uint8_t __rom_end[];

// GCC might emit calls to these.
void *memset(void *d, int c, unsigned n) {
  uint8_t *p = d;
  while (n--)
    *p++ = c;
  return d;
}
void *memcpy(void *d, const void *s, unsigned n) {
  uint8_t *p = d;
  const uint8_t *q = s;
  while (n--)
    *p++ = *q++;
  return d;
}
void exit_to_firmware(void);

// Video file state
static const t_gbv_header *hdr;
static const uint8_t *vbase;          // File start
static const uint8_t *fptr;           // Next frame to decode
static uint32_t next_frame;           // Its number
static uint32_t next_vblank;          // Its start time (vblanks)
static unsigned mbw, mbh;             // Size in 8x8 macroblocks

// Playback state, shared with the V-blank IRQ
static volatile bool playing;
static volatile bool frame_ready;     // Back buffer has the next frame
static volatile bool first_frame;     // Flip as soon as ready
static volatile uint8_t ready_dur;    // Duration of the ready frame
static volatile uint8_t cur_dur;      // Duration of the displayed frame
static volatile uint8_t cur_sub;      // Vblanks it has been displayed
static volatile uint8_t cur_half;     // Audio ring half being played
static volatile uint32_t shown_vblank;  // Start time of the displayed frame
static volatile uint32_t vbcount;     // Free running vblank counter
static unsigned volume = 4;           // 0..4
volatile uint32_t late_vblanks;       // Vblanks where the decoder was late (debug)

EWRAM_BSS static int8_t audio_ring[2][MAX_DUR * MAX_SPV] __attribute__((aligned(4)));
EWRAM_BSS static uint16_t pal_buf[256];
static volatile bool pal_pending;     // Palette for the ready frame
EWRAM_BSS static int8_t silence[MAX_SPV + 16] __attribute__((aligned(4)));
EWRAM_BSS static uint32_t osd_canvas[4 * 32 * 8];  // 4 sprites of 64x32, 4bpp

// ------------------------------------------------------------------ IRQ ---

static inline void audio_start(const void *src) {
  REG_DMA1CNT_H = 0;
  REG_DMA1SAD = (uint32_t)src;
  REG_DMA1CNT_H = 0xB640;   // Enable, FIFO timing, repeat, 32 bit, fixed dest
}

IWRAM_CODE static void irq_handler(void) {
  uint16_t flags = REG_IF & REG_IE;
  if (flags & 1) {
    vbcount++;
    if (playing) {
      if (frame_ready && (first_frame || cur_sub >= cur_dur)) {
        REG_DISPCNT ^= 0x10;          // Show the other page
        if (pal_pending) {
          REG32(0x040000D4) = (uint32_t)pal_buf;    // DMA3 copy to the palette
          REG32(0x040000D8) = 0x05000000;
          REG32(0x040000DC) = 0x84000000 | 128;
          pal_pending = false;
        }
        shown_vblank = next_vblank - ready_dur;
        cur_dur = ready_dur;
        cur_half ^= 1;
        cur_sub = 0;
        first_frame = false;
        frame_ready = false;
      }
      if (hdr->spv) {
        if (cur_sub < cur_dur)
          audio_start(&audio_ring[cur_half][cur_sub * hdr->spv]);
        else {
          audio_start(silence);       // Decoder is late
          late_vblanks++;
        }
      }
      if (cur_sub < 255)
        cur_sub++;
    }
  }
  REG_IF = flags;
  BIOS_IF |= flags;
}

static void vblank_wait(void) {
  asm volatile ("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory");
}

// --------------------------------------------------------------- Decode ---

static const int16_t ima_steps[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
};
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

IWRAM_CODE static void decode_audio(const uint8_t *src, int8_t *dst, unsigned count, unsigned vol) {
  int pred = (int16_t)(src[0] | (src[1] << 8));
  int idx = src[2];
  src += 4;
  for (unsigned i = 0; i < count; i++) {
    unsigned nib = (i & 1) ? (*src++ >> 4) : (*src & 15);
    int step = ima_steps[idx];
    int diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    pred += (nib & 8) ? -diff : diff;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    idx += ima_index[nib];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    dst[i] = ((pred >> 8) * (int)vol) >> 2;
  }
}

// Byte masks for the 4 pixels of a two-color block row.
static const uint32_t nibble_mask[16] = {
  0x00000000, 0x000000FF, 0x0000FF00, 0x0000FFFF,
  0x00FF0000, 0x00FF00FF, 0x00FFFF00, 0x00FFFFFF,
  0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF,
  0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF,
};

IWRAM_CODE static const uint8_t *decode_block(const uint8_t *d, unsigned op, volatile uint32_t *dst) {
  switch (op) {
  case 1: {      // Fill
      uint32_t c = *d++ * 0x01010101U;
      dst[0] = c; dst[60] = c; dst[120] = c; dst[180] = c;
    }
    break;
  case 2: {      // Two colors, 16-bit mask (bit set: second color)
      uint32_t c0 = d[0] * 0x01010101U, c1 = d[1] * 0x01010101U;
      unsigned m = d[2] | (d[3] << 8);
      d += 4;
      for (unsigned r = 0; r < 4; r++, m >>= 4) {
        uint32_t bm = nibble_mask[m & 15];
        dst[r * 60] = (c1 & bm) | (c0 & ~bm);
      }
    }
    break;
  case 3:        // Raw 4x4
    for (unsigned r = 0; r < 4; r++, d += 4)
      dst[r * 60] = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
    break;
  };
  return d;
}

// Decodes the video part of a frame into the given page (stride 240).
IWRAM_CODE static void decode_video(const uint8_t *ops, volatile uint32_t *page) {
  unsigned nmb = mbw * mbh;
  const uint8_t *d = ops + ((nmb + 3) >> 2);
  unsigned mb = 0;
  for (unsigned y = 0; y < mbh; y++) {
    volatile uint32_t *row = page + y * 8 * 60;
    for (unsigned x = 0; x < mbw; x++, mb++) {
      unsigned op = (ops[mb >> 2] >> ((mb & 3) * 2)) & 3;
      volatile uint32_t *dst = row + x * 2;
      if (op == 1) {
        uint32_t c = *d++ * 0x01010101U;
        for (unsigned r = 0; r < 8; r++) {
          dst[r * 60] = c;
          dst[r * 60 + 1] = c;
        }
      }
      else if (op == 2) {
        unsigned sub = *d++;
        d = decode_block(d, sub & 3, dst);
        d = decode_block(d, (sub >> 2) & 3, dst + 1);
        d = decode_block(d, (sub >> 4) & 3, dst + 240);
        d = decode_block(d, (sub >> 6) & 3, dst + 241);
      }
    }
  }
}

static volatile uint32_t *back_page(void) {
  return (volatile uint32_t*)(MEM_VRAM + ((REG_DISPCNT & 0x10) ? 0 : 0xA000));
}

// Decodes the next frame into the back buffer and the free audio ring half.
static bool decode_next(void) {
  if (next_frame >= hdr->nframes)
    return false;

  const uint8_t *p = fptr;
  uint32_t size = *(const uint32_t*)p;
  unsigned flags = p[4], dur = p[5];
  unsigned abytes = p[6] | (p[7] << 8);
  p += 8;
  if (dur > MAX_DUR)
    dur = MAX_DUR;
  if (flags & 1) {
    // New palette: applied by the IRQ when this frame is shown.
    for (unsigned i = 0; i < 256; i++)
      pal_buf[i] = p[i * 2] | (p[i * 2 + 1] << 8);
    pal_pending = true;
    p += 512;
  }
  if (abytes) {
    // The chunk holds dur*spv samples, played from the free ring half.
    decode_audio(p, audio_ring[cur_half ^ 1], dur * hdr->spv, volume);
    p += abytes;
  }
  decode_video(p, back_page());

  fptr += size;
  next_frame++;
  next_vblank += dur;
  ready_dur = dur;
  frame_ready = true;
  return true;
}

// ------------------------------------------------------------------ OSD ---

static void osd_pixel(unsigned x, unsigned y, unsigned c) {
  unsigned s = x >> 6, lx = x & 63;
  unsigned tile = s * 32 + (y >> 3) * 8 + (lx >> 3);
  uint32_t *t = &osd_canvas[tile * 8 + (y & 7)];
  unsigned sh = (lx & 7) * 4;
  *t = (*t & ~(15U << sh)) | (c << sh);
}

static void osd_text(unsigned x, unsigned y, const char *s) {
  for (; *s && x + 8 <= 240; s++, x += 8) {
    unsigned c = (uint8_t)*s;
    if (c < 32 || c > 126)
      c = '?';
    const uint8_t *g = font8x16[c - 32];
    for (unsigned r = 0; r < 16; r++)
      for (unsigned b = 0; b < 8; b++)
        if (g[r] & (1 << b))
          osd_pixel(x + b, y + r, 1);
  }
}

static char *fmt_time(char *o, uint32_t vblanks) {
  unsigned secs = (unsigned)(vblanks / VBLANK_HZ);
  unsigned m = secs / 60, s = secs % 60;
  if (m >= 100) *o++ = '0' + m / 100;
  if (m >= 10) *o++ = '0' + (m / 10) % 10;
  *o++ = '0' + m % 10;
  *o++ = ':';
  *o++ = '0' + s / 10;
  *o++ = '0' + s % 10;
  *o = 0;
  return o;
}

static void osd_draw(void) {
  for (unsigned i = 0; i < sizeof(osd_canvas) / 4; i++)
    osd_canvas[i] = 0x22222222;      // Dark background

  char line[48], *o = line;
  if (!playing) {
    const char *ps = "|| ";
    while (*ps) *o++ = *ps++;
  }
  for (unsigned i = 0; i < sizeof(hdr->title) && hdr->title[i] && o < &line[29]; i++)
    *o++ = hdr->title[i];
  *o = 0;
  osd_text(4, 0, line);

  o = fmt_time(line, shown_vblank);
  *o++ = '/';
  o = fmt_time(o, hdr->total_vblanks);
  osd_text(4, 16, line);

  // Progress bar and volume
  unsigned bx0 = 4 + 8 * 12, bx1 = 240 - 4 - 8 * 3;
  unsigned pos = bx0 + (uint64_t)(bx1 - bx0) * shown_vblank / (hdr->total_vblanks ? hdr->total_vblanks : 1);
  for (unsigned x = bx0; x < bx1; x++)
    for (unsigned y = 22; y < 26; y++)
      osd_pixel(x, y, x <= pos ? 3 : 4);
  char v[4] = { 'V', '0' + volume, 0 };
  osd_text(240 - 4 - 16, 16, v);

  volatile uint32_t *dst = OBJ_TILES;
  for (unsigned i = 0; i < sizeof(osd_canvas) / 4; i++)
    dst[i] = osd_canvas[i];
}

static void osd_show(bool show) {
  for (unsigned s = 0; s < 4; s++) {
    volatile uint16_t *o = &MEM_OAM[s * 4];
    if (show) {
      o[0] = (160 - OSD_H) | (1 << 14);           // Wide shape
      o[1] = (s * 64) | (3 << 14);                // 64x32
      o[2] = (512 + s * 32) | (15 << 12);         // Palette bank 15
    } else
      o[0] = 0x200;                               // Hidden
  }
}

// ----------------------------------------------------------------- Main ---

static const t_gbv_header *find_video(void) {
  const uint8_t *p = (const uint8_t*)(((uintptr_t)__rom_end + 3) & ~3U);
  for (unsigned i = 0; i < 1024; i += 4)
    if (p[i] == 'G' && p[i + 1] == 'B' && p[i + 2] == 'V' && p[i + 3] == '1')
      return (const t_gbv_header*)&p[i];
  return 0;
}

static void seek_to(uint32_t target_vblank) {
  const uint32_t *ix = (const uint32_t*)(vbase + hdr->index_off);
  unsigned cnt = ix[0];
  const t_gbv_index *e = (const t_gbv_index*)&ix[1];
  unsigned best = 0;
  for (unsigned i = 0; i < cnt; i++)
    if (e[i].vblank <= target_vblank)
      best = i;
  bool was_playing = playing;
  playing = false;
  REG_DMA1CNT_H = 0;
  frame_ready = false;
  fptr = vbase + e[best].offset;
  next_frame = e[best].frame;
  next_vblank = e[best].vblank;
  shown_vblank = e[best].vblank;
  first_frame = true;
  cur_sub = 0;
  cur_dur = 0;
  // Show the keyframe even while paused.
  decode_next();
  REG_DISPCNT ^= 0x10;
  if (pal_pending) {
    for (unsigned i = 0; i < 256; i++)
      MEM_PAL[i] = pal_buf[i];
    pal_pending = false;
  }
  first_frame = false;
  cur_half ^= 1;
  cur_dur = ready_dur;
  frame_ready = false;
  playing = was_playing;
}

static void error_screen(const char *msg) {
  REG_DISPCNT = 0x0404 | 0x1000 | 0x40;
  MEM_PAL[256 + 15 * 16 + 1] = 0x7FFF;
  MEM_PAL[256 + 15 * 16 + 2] = 0x0000;
  static const t_gbv_header empty = { .magic = "GBV1" };
  hdr = &empty;
  for (unsigned i = 0; i < sizeof(osd_canvas) / 4; i++)
    osd_canvas[i] = 0x22222222;
  osd_text(4, 8, msg);
  volatile uint32_t *dst = OBJ_TILES;
  for (unsigned i = 0; i < sizeof(osd_canvas) / 4; i++)
    dst[i] = osd_canvas[i];
  osd_show(true);
  while (1) {
    if (!(REG_KEYINPUT & (KEY_B | KEY_START)))
      exit_to_firmware();
  }
}

int main(void) {
  hdr = find_video();
  if (!hdr)
    error_screen("Video nao encontrado (B)");
  vbase = (const uint8_t*)hdr;
  mbw = hdr->width / 8;
  mbh = hdr->height / 8;

  // Mode 4, BG2 scaled to fill the screen, OBJs in 1D mode for the OSD.
  REG_DISPCNT = 0x0080;
  unsigned sx = (hdr->width << 8) / 240, sy = (hdr->height << 8) / 160;
  REG_BG2PA = sx; REG_BG2PB = 0;
  REG_BG2PC = 0; REG_BG2PD = sy;
  REG_BG2X = 0; REG_BG2Y = 0;
  for (unsigned i = 0; i < 0x14000 / 4; i++)
    ((volatile uint32_t*)MEM_VRAM)[i] = 0;
  MEM_PAL[256 + 15 * 16 + 1] = 0x7FFF;   // OSD text
  MEM_PAL[256 + 15 * 16 + 2] = 0x0842;   // OSD background
  MEM_PAL[256 + 15 * 16 + 3] = 0x03FF;   // Progress
  MEM_PAL[256 + 15 * 16 + 4] = 0x294A;   // Progress background
  for (unsigned i = 0; i < 128; i++)
    MEM_OAM[i * 4] = 0x200;
  REG_DISPCNT = 0x0404 | 0x1000 | 0x40;

  // Audio: DirectSound A on both speakers, Timer 0 at spv samples per vblank.
  if (hdr->spv) {
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0;
    REG_SOUNDCNT_H = 0x0B04 | 0x0800;
    REG_DMA1DAD = REG_FIFO_A;
    REG_TM0CNT_H = 0;
    REG_TM0CNT_L = 65536 - 280896 / hdr->spv;
    REG_TM0CNT_H = 0x80;
  }

  IRQ_HANDLER = (uint32_t)irq_handler;
  REG_DISPSTAT = 0x08;
  REG_IE = 1;
  REG_IME = 1;

  fptr = vbase + 64;
  next_frame = 0;
  next_vblank = 0;
  first_frame = true;
  playing = true;

  unsigned prevkeys = 0x3FF, osd_timer = OSD_TIME, lastsec = ~0U;
  bool osd_on = false, osd_pinned = false;
  while (1) {
    if (playing && !frame_ready && next_frame < hdr->nframes)
      decode_next();
    else
      vblank_wait();

    unsigned keys = ~REG_KEYINPUT & 0x3FF;
    unsigned newk = keys & ~prevkeys;
    prevkeys = keys;
    if (newk) {
      osd_timer = OSD_TIME;
      lastsec = ~0U;
    }

    if (newk & (KEY_A | KEY_START)) {
      playing = !playing;
      if (!playing)
        REG_DMA1CNT_H = 0;
      else if (next_frame >= hdr->nframes && !frame_ready)
        seek_to(0);   // Restart at the end
    }
    if (newk & KEY_B) {
      if (!playing)
        exit_to_firmware();
      playing = false;
      REG_DMA1CNT_H = 0;
    }
    if (newk & KEY_SELECT)
      osd_pinned = !osd_pinned;
    if (newk & KEY_UP && volume < 4)
      volume++;
    if (newk & KEY_DOWN && volume > 0)
      volume--;
    int seek = (newk & KEY_RIGHT) ? 10 : (newk & KEY_LEFT) ? -10 :
               (newk & KEY_R) ? 60 : (newk & KEY_L) ? -60 : 0;
    if (seek) {
      int t = (int)shown_vblank + (int)(seek * VBLANK_HZ);
      if (seek > 0)
        t += (int)(2 * VBLANK_HZ);   // Keyframes are sparse, favour moving forward
      if (t < 0)
        t = 0;
      seek_to(t);
    }

    // End of video: pause on the last frame.
    if (playing && next_frame >= hdr->nframes && !frame_ready && cur_sub >= cur_dur) {
      playing = false;
      REG_DMA1CNT_H = 0;
      osd_timer = OSD_TIME;
    }

    bool show = osd_pinned || !playing || osd_timer;
    if (osd_timer)
      osd_timer--;
    unsigned sec = (unsigned)(shown_vblank / VBLANK_HZ);
    if (show && (sec != lastsec || !osd_on)) {
      osd_draw();
      lastsec = sec;
    }
    if (show != osd_on) {
      osd_show(show);
      osd_on = show;
    }
  }
}
