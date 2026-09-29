// Soft-patch demo ROM for SuperFW: shows a few lines of text on a colored
// background. The IPS/UPS/BPS patches change the text and the color, and
// append a block at the end of the ROM (checked through its magic).
#include <stdint.h>

#define REG_DISPCNT  (*(volatile uint16_t*)0x04000000)
#define VRAM         ((volatile uint16_t*)0x06000000)
#define RGB(r,g,b)   ((r) | ((g) << 5) | ((b) << 10))
#define ROM_END_BLK  ((const volatile char*)0x08040000)

typedef struct {
  char magic[8];          // "DEMOCFG!" (easy to find in the ROM)
  uint16_t bg, fg;
  char line[3][16];
} t_cfg;

__attribute__((used, section(".rodata.cfg")))
const volatile t_cfg cfg = {
  "DEMOCFG!", RGB(3, 5, 16), RGB(31, 31, 31),
  { "ROM ORIGINAL", "SEM PATCH", "" }
};

extern const uint8_t font8x16[95][16];

static void draw_char(int x, int y, char c, uint16_t col) {
  if (c < 32 || c > 126)
    return;
  const uint8_t *g = font8x16[c - 32];
  for (int r = 0; r < 16; r++)
    for (int b = 0; b < 8; b++)
      if (g[r] & (0x80 >> b)) {
        volatile uint16_t *p = &VRAM[(y + r*2) * 240 + x + b*2];
        p[0] = col; p[1] = col; p[240] = col; p[241] = col;
      }
}

static void draw_text(int y, const volatile char *s, unsigned maxlen, uint16_t col) {
  unsigned n = 0;
  while (n < maxlen && s[n]) n++;
  int x = (240 - (int)n * 16) / 2;
  for (unsigned i = 0; i < n; i++)
    draw_char(x + i * 16, y, s[i], col);
}

static int block_ok(void) {
  const char m[8] = {'B','I','G','R','O','M','!','!'};
  for (int i = 0; i < 8; i++)
    if (ROM_END_BLK[i] != m[i])
      return 0;
  return 1;
}

int main(void) {
  REG_DISPCNT = 3 | 0x400;
  uint16_t bg = cfg.bg, fg = cfg.fg;
  for (int i = 0; i < 240 * 160; i++)
    VRAM[i] = bg;
  draw_text(4, "SUPERFW DEMO", 16, RGB(31, 31, 0));
  for (int i = 0; i < 3; i++)
    draw_text(38 + i * 30, cfg.line[i], 16, fg);
  if (block_ok())
    draw_text(128, &ROM_END_BLK[8], 16, RGB(31, 31, 0));
  while (1);
}
