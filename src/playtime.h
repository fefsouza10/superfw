/*
 * Play time tracking.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#ifndef _PLAYTIME_H_
#define _PLAYTIME_H_

#include <stdint.h>
#include <stdbool.h>

// The in-game menu V-blank hook counts frames in the (otherwise unused) FIQ
// mode r8 register. If r9 is not zero, it also mirrors the counter to that
// SRAM address, so the time survives a power off. This is only done for games
// that leave the end of the SRAM bank 0 unused (no save, SRAM or EEPROM).
// The original bytes are saved and restored before the SRAM is written back
// to the .sav file. Other games (flash saves) store the counter when the
// in-game menu is opened.
#define PLAYTIME_SRAM_OFF     0xFFD0        // Just below the DirectSave config

// Frames to seconds (the GBA runs at 16777216 / 280896 = ~59.73 fps)
static inline uint32_t playtime_secs(uint32_t frames) {
  return ((uint64_t)frames * 280896) >> 24;
}

// Formats the time as "12h 05m" (or "5m", "<1m").
static inline void playtime_format(uint32_t frames, char *buf) {
  uint32_t mins = playtime_secs(frames) / 60;
  uint32_t h = mins / 60, m = mins % 60;
  char tmp[16];
  unsigned n = 0;
  if (!mins)
    *buf++ = '<', m = 1;
  if (h) {
    do { tmp[n++] = '0' + h % 10; h /= 10; } while (h);
    while (n) *buf++ = tmp[--n];
    *buf++ = 'h'; *buf++ = ' ';
    *buf++ = '0' + m / 10;
  } else if (m >= 10)
    *buf++ = '0' + m / 10;
  *buf++ = '0' + m % 10;
  *buf++ = 'm';
  *buf = 0;
}

#ifndef NO_PLAYTIME_DB
// Play time stored for the game being launched (passed to the in-game menu).
extern uint32_t playtime_session_base;

// Returns the stored play time (in frames) for a game (ROM path or NOR name).
uint32_t playtime_get(const char *key);

// Adds frames to a game's play time. Returns false on SD errors.
bool playtime_add(const char *key, uint32_t frames);

// Called right before launching a game (after preparing the SRAM contents).
// Returns the stored play time so far (in frames).
uint32_t playtime_start(const char *key, int savetype);

// Called at boot: adds the time played in the last session (if any).
void playtime_flush();
#endif

#endif
