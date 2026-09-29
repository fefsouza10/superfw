/*
 * Play time tracking.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#include <stdint.h>
#include <string.h>
#include "common.h"
#include "config.h"
#include "playtime.h"
#include "save.h"
#include "fatfs/ff.h"
#include "util.h"
#include "nanoprintf.h"

uint32_t playtime_session_base;

// The database is a text file with one "<frames> <key>" line per game.

typedef struct {
  FIL fd;
  char buf[512];
  unsigned pos, cnt;
} t_linereader;

// Reads a line (without the newline). Returns false at EOF.
static bool read_line(t_linereader *lr, char *line, unsigned maxlen) {
  unsigned n = 0;
  while (1) {
    if (lr->pos >= lr->cnt) {
      UINT rd = 0;
      if (FR_OK != f_read(&lr->fd, lr->buf, sizeof(lr->buf), &rd) || !rd) {
        line[n] = 0;
        return n != 0;
      }
      lr->pos = 0;
      lr->cnt = rd;
    }
    char c = lr->buf[lr->pos++];
    if (c == '\n') {
      line[n] = 0;
      return true;
    }
    if (c != '\r' && n < maxlen - 1)
      line[n++] = c;
  }
}

// Splits "<frames> <key>", returns the key (or NULL if malformed).
static const char *parse_line(const char *line, uint32_t *frames) {
  uint32_t v = 0;
  const char *p = line;
  if (*p < '0' || *p > '9')
    return NULL;
  while (*p >= '0' && *p <= '9')
    v = v * 10 + (*p++ - '0');
  if (*p != ' ')
    return NULL;
  *frames = v;
  return p + 1;
}

uint32_t playtime_get(const char *key) {
  t_linereader lr;
  if (FR_OK != f_open(&lr.fd, PLAYTIME_FILEPATH, FA_READ))
    return 0;
  lr.pos = lr.cnt = 0;

  char line[MAX_FN_LEN + 16];
  uint32_t ret = 0;
  while (read_line(&lr, line, sizeof(line))) {
    uint32_t v;
    const char *k = parse_line(line, &v);
    if (k && !strcmp(k, key)) {
      ret = v;
      break;
    }
  }
  f_close(&lr.fd);
  return ret;
}

static bool write_entry(FIL *fd, uint32_t frames, const char *key) {
  char line[MAX_FN_LEN + 16];
  unsigned l = npf_snprintf(line, sizeof(line), "%u %s\n", (unsigned)frames, key);
  UINT wr;
  return FR_OK == f_write(fd, line, l, &wr) && wr == l;
}

bool playtime_add(const char *key, uint32_t frames) {
  FIL fo;
  if (FR_OK != f_open(&fo, PLAYTIME_TMP_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  bool found = false, ok = true;
  t_linereader lr;
  if (FR_OK == f_open(&lr.fd, PLAYTIME_FILEPATH, FA_READ)) {
    lr.pos = lr.cnt = 0;
    char line[MAX_FN_LEN + 16];
    while (ok && read_line(&lr, line, sizeof(line))) {
      uint32_t v;
      const char *k = parse_line(line, &v);
      if (!k)
        continue;          // Drop malformed lines
      if (!found && !strcmp(k, key)) {
        found = true;
        v = (v + frames < v) ? 0xFFFFFFFF : v + frames;
      }
      ok = write_entry(&fo, v, k);
    }
    f_close(&lr.fd);
  }
  if (ok && !found)
    ok = write_entry(&fo, frames, key);
  f_close(&fo);

  if (!ok) {
    f_unlink(PLAYTIME_TMP_FILEPATH);
    return false;
  }
  f_unlink(PLAYTIME_FILEPATH);
  return FR_OK == f_rename(PLAYTIME_TMP_FILEPATH, PLAYTIME_FILEPATH);
}

static uint32_t parsehex(const char *s) {
  uint32_t v = 0;
  while (1) {
    char c = *s++;
    if (c >= '0' && c <= '9')      v = (v << 4) | (c - '0');
    else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
    else return v;
  }
}

uint32_t playtime_start(const char *key, int savetype) {
  // Games without save or using SRAM/EEPROM leave the end of bank 0 unused.
  bool use_sram = savetype >= SaveTypeNone && savetype <= SaveTypeEEPROM64K;
  uint8_t orig[4] = {0};
  if (use_sram) {
    const uint8_t zero[4] = {0};
    read_sram_buffer(orig, PLAYTIME_SRAM_OFF, sizeof(orig));
    write_sram_buffer(zero, PLAYTIME_SRAM_OFF, sizeof(zero));
  }

  f_unlink(PLAYTIME_IGM_FILEPATH);

  // The session file tells the next boot which game was played.
  FIL fo;
  if (FR_OK == f_open(&fo, PLAYTIME_SESSION_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS)) {
    char line[MAX_FN_LEN + 32];
    unsigned l = npf_snprintf(line, sizeof(line), "%s\n%x %02x%02x%02x%02x\n", key,
                              use_sram ? PLAYTIME_SRAM_OFF : 0, orig[0], orig[1], orig[2], orig[3]);
    UINT wr;
    f_write(&fo, line, l, &wr);
    f_close(&fo);
  }

  set_fiq_regs(0, use_sram ? 0x0E000000 + PLAYTIME_SRAM_OFF : 0);

  playtime_session_base = playtime_get(key);
  return playtime_session_base;
}

void playtime_flush() {
  t_linereader lr;
  if (FR_OK != f_open(&lr.fd, PLAYTIME_SESSION_FILEPATH, FA_READ))
    return;
  lr.pos = lr.cnt = 0;

  char key[MAX_FN_LEN], info[32];
  bool valid = read_line(&lr, key, sizeof(key)) && read_line(&lr, info, sizeof(info));
  f_close(&lr.fd);

  if (valid && key[0]) {
    uint32_t frames = 0;

    // Counter written by the in-game menu (games without SRAM mirror)
    FIL fd;
    if (FR_OK == f_open(&fd, PLAYTIME_IGM_FILEPATH, FA_READ)) {
      char tmp[16];
      UINT rd = 0;
      if (FR_OK == f_read(&fd, tmp, sizeof(tmp) - 1, &rd)) {
        tmp[rd] = 0;
        for (const char *d = tmp; *d >= '0' && *d <= '9'; d++)
          frames = frames * 10 + (*d - '0');
      }
      f_close(&fd);
    }

    // Counter mirrored in SRAM: read it and restore the game's bytes.
    uint32_t off = parsehex(info);
    const char *origs = strchr(info, ' ');
    if (off == PLAYTIME_SRAM_OFF && origs) {
      uint8_t cnt[4], orig[4];
      uint32_t o = parsehex(origs + 1);
      orig[0] = o >> 24; orig[1] = o >> 16; orig[2] = o >> 8; orig[3] = o;
      read_sram_buffer(cnt, off, sizeof(cnt));
      write_sram_buffer(orig, off, sizeof(orig));
      uint32_t sf = cnt[0] | (cnt[1] << 8) | (cnt[2] << 16) | (cnt[3] << 24);
      if (sf > frames && sf < 0x80000000)
        frames = sf;
    }

    if (frames)
      playtime_add(key, frames);
  }

  f_unlink(PLAYTIME_IGM_FILEPATH);
  f_unlink(PLAYTIME_SESSION_FILEPATH);
}
