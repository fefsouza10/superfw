/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "gbahw.h"
#include "settings.h"
#include "save.h"
#include "patchengine.h"
#include "supercard_driver.h"
#include "nanoprintf.h"
#include "fonts/font_render.h"
#include "common.h"
#include "fatfs/ff.h"
#include "flash.h"

// Global variables
FATFS sdfs;          // FatFS mounted filesystem
bool isgba = true;   // Has some alternative paths for NDS.
bool fastew = false; // EWRAM can be overclocked (from the look of it at least).
bool slowsd = true;  // Whether we use slow SD mirrors for SD operations.

t_flash_info flashinfo;
t_card_info sd_info;
t_patchdb_info pdbinfo;

void *font_base_addr = (void*)ROM_FONTBASE_U8;

static void wait_for_vblank() {
  while (!(REG_DISPSTAT & DISPSTAT_VBLANK));
}

extern volatile unsigned frame_count;

// Same as wait_for_vblank(), but halts the CPU while waiting (saves battery).
// Needs the V-blank IRQ to be enabled.
static void wait_for_vblank_halt() {
  unsigned f = frame_count;
  while (!(REG_DISPSTAT & DISPSTAT_VBLANK) && f == frame_count)
    asm volatile ("swi 0x02" ::: "r0", "r1", "r2", "r3", "memory");
}

void setup_video() {
  // Stop screen, clear VRAM and palette RAM.
  REG_DISPCNT = 0x80;
  dma_memset16(MEM_VRAM, 0xffff, MEM_VRAM_SIZE / 2);
  dma_memset16(MEM_PALETTE, 0xffff, MEM_PALETTE_SIZE / 2);
  MEM_PALETTE[0] = 0x0;

  // Setup BG mode 4, with single buffering, enable display now!
  wait_for_vblank();
  REG_DISPCNT = 0x4 | 0x1400 | 0x40;
}

#define display_info_msg_fmt(msg, ...) {   \
  unsigned off = (isgba ? (SCREEN_HEIGHT - 32) * SCREEN_WIDTH + 16 : \
                          (NDS_SCREEN_HEIGHT - 32) * NDS_SCREEN_WIDTH + 16); \
  uint8_t *basept = (uint8_t*)&MEM_VRAM_U8[off]; \
  char buf[40];                       \
  npf_snprintf(buf, sizeof(buf), msg, __VA_ARGS__); \
  draw_text_idx8_bus16(buf, basept, isgba ? SCREEN_WIDTH : NDS_SCREEN_WIDTH, 0x5); \
}

#define display_info_msg(msg) {   \
  unsigned off = (isgba ? (SCREEN_HEIGHT - 32) * SCREEN_WIDTH + 16 : \
                          (NDS_SCREEN_HEIGHT - 32) * NDS_SCREEN_WIDTH + 16); \
  uint8_t *basept = (uint8_t*)&MEM_VRAM_U8[off]; \
  draw_text_idx8_bus16(msg, basept, isgba ? SCREEN_WIDTH : NDS_SCREEN_WIDTH, 0x5); \
}

#define display_info_clear() {   \
  unsigned off = (isgba ? (SCREEN_HEIGHT - 32) * SCREEN_WIDTH + 16 : \
                          (NDS_SCREEN_HEIGHT - 32) * NDS_SCREEN_WIDTH + 16); \
  uint8_t *basept = (uint8_t*)&MEM_VRAM_U8[off]; \
  dma_memset16(basept, 0x0000, 8 * (isgba ? SCREEN_WIDTH : NDS_SCREEN_WIDTH)); \
}

#define fatal_init_error(msg, ...) {    \
  display_info_msg_fmt(msg, __VA_ARGS__);   \
  while(1);    /* Hang in here, not much to do! */ \
}

void init_sdcard_and_mount() {
  // Init the SD card hardware
  #ifdef EMU_HARNESS
  unsigned ret = 0;
  #else
  unsigned ret = sdcard_init(&sd_info);
  #endif
  if (ret)
    fatal_init_error("Fatal SD card init err: %d", ret);

  // Attempt to mount the FAT/exFAT filesystem, see if it is valid!
  ret = f_mount(&sdfs, "0:", 1);
  if (ret)
    fatal_init_error("Cannot mount FATfs: %d", ret);
}

void check_pending_saves() {
  // Check if there's a pending save in the SRAM, and dump it.
  // TODO: add some key combo to skip this?
  if (FR_OK == f_stat(PENDING_SAVE_FILEPATH, NULL)) {
    display_info_msg("Writing previous savegame ...");

    unsigned ecode = flush_pending_sram();
    if (ecode == ERR_SAVE_FLUSH_WRITEFAIL) {
      // Display error messages briefly if any
      display_info_clear();
      display_info_msg("Failed to write savegame to SD!");
      wait_ms(4000);
    }

    // Delete the sentinel file unconditionally.
    f_unlink(PENDING_SAVE_FILEPATH);
  }
}

volatile unsigned frame_count = 0;
volatile uint8_t key_presses[10];     // Presses not consumed yet, per key
static uint16_t irq_prev_keys = 0;

void irq_handler_fn() {
  // Clear all IRQs just in case
  REG_IF = 0xFFFF;
  // Gets called on every V-blank IRQ.
  frame_count++;
  // Count key presses, so that short (or repeated) presses are not lost
  // while the menu is busy (ie. loading a cover).
  uint16_t keys = REG_KEYINPUT ^ 0x3FF;
  uint16_t pressed = keys & ~irq_prev_keys;
  irq_prev_keys = keys;
  for (unsigned i = 0; pressed; i++, pressed >>= 1)
    if ((pressed & 1) && key_presses[i] < 4)
      key_presses[i]++;
}

// Menu auto sleep: after some idle time, blank the screen and stop the CPU
// until the wake up combo (same as the in-game menu sleep) is pressed.
#define REG_KEYCNT_U16     (*((volatile uint16_t *) 0x04000132))
#define IRQ_KEYPAD         0x1000

static void menu_sleep() {
  uint16_t dispcnt = REG_DISPCNT;
  uint16_t wake = (~hotkey_list[sleep_combo].mask) & 0x3FF;

  REG_DISPCNT = dispcnt | 0x80;         // Forced blank (LCD off)
  REG_IME = 0;
  REG_KEYCNT_U16 = 0xC000 | wake;       // IRQ when all keys are pressed
  REG_IE = IRQ_KEYPAD;
  REG_IF = 0xFFFF;
  REG_IME = 1;

  asm volatile ("swi 0x03" ::: "r0", "r1", "r2", "r3", "memory");

  REG_IME = 0;
  REG_KEYCNT_U16 = 0;
  REG_IF = 0xFFFF;
  REG_IE = 0x0001;
  REG_IME = 1;

  // Wait for the combo to be released and drop it, so it does not reach the menu.
  while (REG_KEYINPUT != 0x3FF)
    wait_for_vblank_halt();
  for (unsigned i = 0; i < sizeof(key_presses); i++)
    key_presses[i] = 0;
  REG_DISPCNT = dispcnt;
}

static void check_autosleep() {
  static unsigned last_active = 0, last_seen = 0;
  // Long blocking tasks (ie. flashing a game) count as activity too.
  if (REG_KEYINPUT != 0x3FF || !autosleep_opt || frame_count - last_seen > 30)
    last_active = frame_count;
  else if (frame_count - last_active >= autosleep_mins[autosleep_opt] * 60U * 60U) {
    menu_sleep();
    last_active = frame_count;
  }
  last_seen = frame_count;
}

uint32_t systime() {
  return (frame_count * 50) / 3;
}

static int main_gba() {
  // Setup (if enabled) the SIO in UART mode so we can log stuff.
  #ifdef ENABLE_UART_LOGGING
    REG_RCNT   = 0x0000;
    REG_SIOCNT = 0x0000;
    REG_SIOCNT = 0x3583;  // UART MODE (115200bps)
  #endif

  // Setup WAITCNT for faster SD-card access.
  REG_WAITCNT = 0x40c0;    // 0x8-0x9: Use 4/2 waitstates (default, slow for SDRAM)
                           // 0xA-0xB: Use 2/1 for fast SD interface access

  // Video is configured in Mode 4 with the logo rendered on the screen.

  // Setup the ROM mapping to allow SD driver. Allow SDRAM usage (as buffer)
  set_supercard_mode(MAPPED_SDRAM, true, true);

  // This hangs on failure since it is fatal.
  init_sdcard_and_mount();

  // Check if we need to save SRAM before doing anything else.
  check_pending_saves();

  // Check if there's a pending SRAM test and perform it.
  int sram_tres = check_peding_sram_test();

  // Load settings files
  load_settings();

  // Load patchdb info.
  set_supercard_mode(MAPPED_SDRAM, true, false);
  memset(&pdbinfo, 0, sizeof(pdbinfo));
  patchmem_dbinfo((uint8_t*)ROM_PATCHDB_U8, &pdbinfo.patch_count, pdbinfo.version, pdbinfo.date, pdbinfo.creator);
  set_supercard_mode(MAPPED_SDRAM, true, true);

  // Configure video mode so we can render the menu.
  setup_video();

  // Setup the IRQ handler, we track V-Blank interrupt (to count frames)
  REG_DISPSTAT |= DISPSTAT_VBLANK_IRQ;
  REG_IRQ_HANDLER_ADDR = (uintptr_t)&gba_irq_handler;
  REG_IF = 0xFFFF;
  REG_IE = 0x0001;
  REG_IME = 1;
  set_irq_enable(true);

  // Initialize menu, start displaying some UI to the user.
  menu_init(sram_tres);

  menu_render(1);
  menu_flip();

  unsigned prev_frame = frame_count;
  while (1) {
    uint16_t mkeys = get_keypress();
    if (mkeys)
      menu_keypress(mkeys);

    unsigned cframe = frame_count;
    menu_render(frame_count - prev_frame);

    wait_for_vblank_halt();    // Avoid tearing (CPU halted meanwhile).
    menu_flip();
    prev_frame = cframe;

    check_autosleep();
  }

  return 0;
}

static int main_nds() {
  // Video is already ON and inited along with some basic initialization.

  // Mount SD card.
  set_supercard_mode(MAPPED_SDRAM, true, true);
  init_sdcard_and_mount();

  // Check if we need to save SRAM before doing anything else.
  check_pending_saves();

  // Proceed to load BOOT.NDS from disk, if exists
  unsigned errc = load_nds("/BOOT.NDS", (void*)dldi_payload);

  if (errc)
    fatal_init_error("Cannot load BOOT.NDS: %d", errc);

  // Proceed with ARM7 sync and reset to homebrew.
  nds_launch();

  // Should never reach here :D
  while (1);

  return 0;
}

int main() {
  // Detect whether we are running on GBA or NDS.
  isgba = !running_on_nds();
  // Similarly detect if EWRAM seems overclockable (GBA but not micro).
  fastew = test_fast_ewram();

  // Take a look at what flash we have.
  flash_identify(&flashinfo);

  if (isgba)
    return main_gba();
  else
    return main_nds();
}

