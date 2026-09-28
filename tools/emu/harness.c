// Minimal mGBA-based test harness for SuperFW (built with EMU_HARNESS=1).
//
// Emulates just enough of a SuperCard for the menu to run:
//  - Mode register at 0x09FFFFFE (0xA55A x2 + mode x2): bit0 selects the
//    firmware flash (read-only) or a writable 32MiB SDRAM buffer.
//  - Sector I/O through the magic register block at 0x09F00000 (see
//    fatfs/diskio.c), backed by a raw FAT image file.
//
// Usage: harness <superfw.gba> <sd.img> <script> <outdir>
// Script lines: "wait N", "press KEYS N" (KEYS like A, B, SEL, START, R+D),
//               "dump NAME BYTES" (first BYTES of SDRAM to NAME.bin),
//               "peek ADDR" (prints a 32-bit word),
//               "shot NAME" (writes NAME.ppm), "pc" (prints the CPU PC).

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/timing.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDRAM_SIZE (32 * 1024 * 1024)

static struct mCore *core;
static struct GBA *gba;
static uint8_t *flash_buf, *sdram_buf;
static size_t flash_size;
static FILE *sdimg;
static uint16_t modeseq[4];
static unsigned mode = 0;
static uint32_t dregs[4];

static void (*orig_store32)(struct ARMCore*, uint32_t, int32_t, int*);
static void (*orig_store16)(struct ARMCore*, uint32_t, int16_t, int*);
static void (*orig_store8)(struct ARMCore*, uint32_t, int8_t, int*);
static uint32_t (*orig_storem)(struct ARMCore*, uint32_t, int, enum LSMDirection, int*);

static void map_rom(void) {
  if (mode & 1) {
    gba->memory.rom = (uint32_t*)sdram_buf;
    gba->memory.romSize = SDRAM_SIZE;
    gba->memory.romMask = SDRAM_SIZE - 1;
  } else {
    gba->memory.rom = (uint32_t*)flash_buf;
    gba->memory.romSize = SDRAM_SIZE;
    gba->memory.romMask = SDRAM_SIZE - 1;
  }
}

static void disk_op(void) {
  uint32_t op = dregs[0], buf = dregs[1], sec = dregs[2], cnt = dregs[3];
  static uint8_t tmp[512];
  for (uint32_t i = 0; i < cnt; i++) {
    fseek(sdimg, (long)(sec + i) * 512, SEEK_SET);
    if (op == 1) {
      memset(tmp, 0, 512);
      if (fread(tmp, 1, 512, sdimg)) {}
      for (int j = 0; j < 512; j++)
        core->rawWrite8(core, buf + i * 512 + j, -1, tmp[j]);
    } else {
      for (int j = 0; j < 512; j++)
        tmp[j] = core->rawRead8(core, buf + i * 512 + j, -1);
      fwrite(tmp, 1, 512, sdimg);
      fflush(sdimg);
    }
  }
}

static uint32_t watch_lo, watch_hi;
static unsigned watch_hits;

static bool cart_write(uint32_t addr, uint32_t val, unsigned size) {
  if (addr >= watch_lo && addr < watch_hi && watch_hits++ < 20)
    printf("WATCH store%u [%08x]=%08x pc=%08x lr=%08x\n", size * 8, addr, val,
           gba->cpu->gprs[15], gba->cpu->gprs[14]);
  if (addr < 0x08000000 || addr >= 0x0A000000)
    return false;
  uint32_t off = addr & (SDRAM_SIZE - 1);
  if (off == 0x1F00010) {
    // Debug marker: prints the value and the current cycle count.
    printf("MARK %u cycles=%u\n", val, (unsigned)mTimingCurrentTime(&gba->timing));
    return true;
  }
  if (off >= 0x1F00000 && off < 0x1F00010) {
    dregs[(off >> 2) & 3] = val;
    if ((off & 0xF) == 0)
      disk_op();
    return true;
  }
  if (off == 0x1FFFFFE && size == 2) {
    memmove(modeseq, modeseq + 1, 6);
    modeseq[3] = val & 0xFFFF;
    if (modeseq[0] == 0xA55A && modeseq[1] == 0xA55A && modeseq[2] == modeseq[3]) {
      mode = modeseq[2];
      map_rom();
    }
    return true;
  }
  if (mode & 1)
    memcpy(&sdram_buf[off & ~(size - 1)], &val, size);
  return true;
}

static void h_store32(struct ARMCore *cpu, uint32_t a, int32_t v, int *cc) {
  if (!cart_write(a, v, 4)) orig_store32(cpu, a, v, cc);
}
static void h_store16(struct ARMCore *cpu, uint32_t a, int16_t v, int *cc) {
  if (!cart_write(a, (uint16_t)v, 2)) orig_store16(cpu, a, v, cc);
}
static void h_store8(struct ARMCore *cpu, uint32_t a, int8_t v, int *cc) {
  if (!cart_write(a, (uint8_t)v, 1)) orig_store8(cpu, a, v, cc);
}
static uint32_t h_storem(struct ARMCore *cpu, uint32_t base, int mask, enum LSMDirection dir, int *cc) {
  if (base < 0x08000000 || base >= 0x0A000000)
    return orig_storem(cpu, base, mask, dir, cc);
  int n = __builtin_popcount(mask);
  uint32_t addr = base & ~3u;
  switch (dir) {
  case LSM_IA: break;
  case LSM_IB: addr += 4; break;
  case LSM_DA: addr = addr - n * 4 + 4; break;
  case LSM_DB: addr -= n * 4; break;
  }
  uint32_t a = addr;
  for (int r = 0; r < 16; r++)
    if (mask & (1 << r)) {
      cart_write(a, cpu->gprs[r], 4);
      a += 4;
    }
  return (dir & LSM_D) ? base - n * 4 : base + n * 4;
}

static unsigned parse_keys(const char *s) {
  unsigned k = 0;
  char buf[64];
  snprintf(buf, sizeof(buf), "%s", s);
  for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
    if (!strcmp(t, "A")) k |= 1 << 0;
    else if (!strcmp(t, "B")) k |= 1 << 1;
    else if (!strcmp(t, "SEL")) k |= 1 << 2;
    else if (!strcmp(t, "START")) k |= 1 << 3;
    else if (!strcmp(t, "RIGHT")) k |= 1 << 4;
    else if (!strcmp(t, "LEFT")) k |= 1 << 5;
    else if (!strcmp(t, "U")) k |= 1 << 6;
    else if (!strcmp(t, "D")) k |= 1 << 7;
    else if (!strcmp(t, "R")) k |= 1 << 8;
    else if (!strcmp(t, "L")) k |= 1 << 9;
  }
  return k;
}

static color_t *vbuf;
static unsigned vw, vh;

static void shot(const char *dir, const char *name) {
  char fn[512];
  snprintf(fn, sizeof(fn), "%s/%s.ppm", dir, name);
  FILE *f = fopen(fn, "wb");
  fprintf(f, "P6\n%u %u\n255\n", vw, vh);
  for (unsigned i = 0; i < vw * vh; i++) {
    uint32_t c = vbuf[i];
    uint8_t px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

static void run_frames(unsigned n) {
  for (unsigned i = 0; i < n; i++)
    core->runFrame(core);
}

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s rom sdimg script outdir\n", argv[0]);
    return 1;
  }
  FILE *rf = fopen(argv[1], "rb");
  fseek(rf, 0, SEEK_END);
  flash_size = ftell(rf);
  fclose(rf);

  core = GBACoreCreate();
  core->init(core);
  mCoreInitConfig(core, NULL);
  core->desiredVideoDimensions(core, &vw, &vh);
  vbuf = calloc(vw * vh, sizeof(color_t));
  core->setVideoBuffer(core, vbuf, vw);
  struct VFile *vf = VFileOpen(argv[1], O_RDONLY);
  if (!core->loadROM(core, vf)) {
    fprintf(stderr, "loadROM failed\n");
    return 1;
  }
  core->reset(core);

  gba = (struct GBA*)core->board;
  flash_buf = calloc(1, SDRAM_SIZE);
  sdram_buf = calloc(1, SDRAM_SIZE);
  // Firmware flash mirrors every 4MiB (the boot stub uses this to tell
  // whether it runs from flash or from SDRAM).
  for (size_t o = 0; o < SDRAM_SIZE; o += 4 * 1024 * 1024)
    memcpy(flash_buf + o, gba->memory.rom, flash_size);
  map_rom();
  // Re-point the active code region (we are about to execute from ROM)
  gba->cpu->memory.activeRegion = (uint32_t*)flash_buf;
  gba->cpu->memory.activeMask = SDRAM_SIZE - 1;

  orig_store32 = gba->cpu->memory.store32;
  orig_store16 = gba->cpu->memory.store16;
  orig_store8 = gba->cpu->memory.store8;
  orig_storem = gba->cpu->memory.storeMultiple;
  gba->cpu->memory.store32 = h_store32;
  gba->cpu->memory.store16 = h_store16;
  gba->cpu->memory.store8 = h_store8;
  gba->cpu->memory.storeMultiple = h_storem;

  if (getenv("WATCH_LO")) {
    watch_lo = strtoul(getenv("WATCH_LO"), NULL, 16);
    watch_hi = strtoul(getenv("WATCH_HI"), NULL, 16);
  }
  sdimg = fopen(argv[2], "r+b");
  FILE *sc = fopen(argv[3], "r");
  char line[256];
  while (fgets(line, sizeof(line), sc)) {
    char cmd[32], a1[64];
    unsigned n = 0;
    a1[0] = 0;
    int cnt = sscanf(line, "%31s %63s %u", cmd, a1, &n);
    if (cnt <= 0 || cmd[0] == '#')
      continue;
    if (!strcmp(cmd, "wait")) {
      core->setKeys(core, 0);
      run_frames(atoi(a1));
    } else if (!strcmp(cmd, "press")) {
      core->setKeys(core, parse_keys(a1));
      run_frames(n ? n : 2);
      core->setKeys(core, 0);
      run_frames(2);
    } else if (!strcmp(cmd, "shot")) {
      shot(argv[4], a1);
      printf("shot %s pc=%08x cpsr=%08x\n", a1, gba->cpu->gprs[15], gba->cpu->cpsr.packed);
    } else if (!strcmp(cmd, "trace")) {
      // Hold keys, step until the PC leaves known memory; dump the last PCs.
      core->setKeys(core, parse_keys(a1));
      static uint32_t ring[256];
      uint32_t iwend = getenv("IWRAM_END") ? strtoul(getenv("IWRAM_END"), NULL, 16) : 0x03003000;
      unsigned ri = 0;
      for (unsigned long i = 0; i < 400000000UL; i++) {
        uint32_t pc = gba->cpu->gprs[15];
        ring[ri++ & 255] = pc;
        unsigned rg = pc >> 24;
        bool badiw = rg == 3 && ((pc >= iwend && pc < 0x03007000) || pc >= 0x03008000);
        if (badiw || !((rg == 0 && pc < 0x4000) || rg == 2 || rg == 3 || rg == 8 || rg == 9)) {
          printf("escaped at step %lu\n", i);
          for (unsigned j = 0; j < 256; j++)
            printf("%08x%c", ring[(ri + j) & 255], (j % 8 == 7) ? '\n' : ' ');
          printf("regs:");
          for (int r = 0; r < 16; r++) printf(" r%d=%08x", r, gba->cpu->gprs[r]);
          printf("\n");
          break;
        }
        core->step(core);
      }
      core->setKeys(core, 0);
    } else if (!strcmp(cmd, "profile")) {
      // Hold keys and sample every executed PC for N frames; prints hot spots.
      static uint32_t hist[1 << 16];
      memset(hist, 0, sizeof(hist));
      core->setKeys(core, parse_keys(a1));
      uint32_t f0 = core->frameCounter(core);
      while (core->frameCounter(core) - f0 < n) {
        uint32_t pc = gba->cpu->gprs[15];
        if ((pc >> 24) == 2)
          hist[(pc >> 4) & 0xFFFF]++;
        core->step(core);
      }
      core->setKeys(core, 0);
      for (int k = 0; k < 25; k++) {
        unsigned best = 0;
        for (unsigned i = 1; i < (1 << 16); i++)
          if (hist[i] > hist[best]) best = i;
        if (!hist[best]) break;
        printf("HOT %08x %u\n", 0x02000000 | (best << 4) | 0x7c0000 * 0, hist[best]);
        hist[best] = 0;
      }
    } else if (!strcmp(cmd, "peek")) {
      // Prints the 32-bit word at a GBA address (hex)
      uint32_t a = strtoul(a1, NULL, 16);
      printf("peek %08x = %08x\n", a, core->busRead32(core, a));
    } else if (!strcmp(cmd, "dump")) {
      // Dumps the first N bytes of the cart SDRAM to outdir/<name>.bin
      char fn[512];
      snprintf(fn, sizeof(fn), "%s/%s.bin", argv[4], a1);
      FILE *df = fopen(fn, "wb");
      fwrite(sdram_buf, 1, n, df);
      fclose(df);
    } else if (!strcmp(cmd, "pc")) {
      printf("pc=%08x lr=%08x cpsr=%08x\n", gba->cpu->gprs[15], gba->cpu->gprs[14], gba->cpu->cpsr.packed);
    }
  }
  fclose(sdimg);
  return 0;
}
