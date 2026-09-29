/*
 * Soft-patching (IPS/UPS/BPS) applied while loading a ROM to SDRAM.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * The ROM is loaded to SDRAM as usual and then patched in place. SDRAM
 * only accepts 16/32 bit writes, so byte writes are done as read-modify-write.
 * Patch data is read from the SD card with a small buffer, toggling the SD
 * interface on for every refill (it hides the upper 16MiB of SDRAM).
 */

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "softpatch.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"
#include "util.h"
#include "compiler.h"

#define ROM8(off)      (((volatile uint8_t*)0x08000000)[off])
#define RDBUF_SZ       1024
#define CPBUF_SZ       512

static inline void sd_on(void)  { set_supercard_mode(MAPPED_SDRAM, true, true); }
static inline void sd_off(void) { set_supercard_mode(MAPPED_SDRAM, true, false); }

// Writes/XORs a byte buffer into SDRAM (ROM space) using halfword accesses.
// Runs from IWRAM in ARM mode, it is the hot loop for big patches.
ARM_CODE IWRAM_CODE NOINLINE
static void sdram_write(uint32_t off, const uint8_t *src, unsigned len, bool xor) {
  volatile uint16_t *p = (volatile uint16_t*)(0x08000000 + (off & ~1U));
  if (!len)
    return;
  if (off & 1) {
    uint16_t v = xor ? (*p ^ (src[0] << 8)) : ((*p & 0xFF) | (src[0] << 8));
    *p++ = v;
    src++; len--;
  }
  if (xor) {
    for (; len >= 2; len -= 2, src += 2)
      *p++ ^= src[0] | (src[1] << 8);
  }
  else if (!((uintptr_t)src & 1)) {
    const uint16_t *s16 = (const uint16_t*)src;
    for (; len >= 2; len -= 2, src += 2)
      *p++ = *s16++;
  }
  else {
    for (; len >= 2; len -= 2, src += 2)
      *p++ = src[0] | (src[1] << 8);
  }
  if (len)
    *p = xor ? (*p ^ src[0]) : ((*p & 0xFF00) | src[0]);
}

static void sdram_read(uint32_t off, uint8_t *dst, unsigned len) {
  for (unsigned i = 0; i < len; i++)
    dst[i] = ROM8(off + i);
}

static void sdram_zero(uint32_t start, uint32_t end) {
  uint8_t z[64];
  memset(z, 0, sizeof(z));
  while (start < end) {
    unsigned n = MIN(sizeof(z), end - start);
    sdram_write(start, z, n, false);
    start += n;
  }
}

// Buffered patch file reader (SD interface is off between refills).
typedef struct {
  FIL fd;
  uint32_t remain;     // File bytes not yet buffered
  unsigned pos, cnt;   // Buffer cursor and fill
  bool err;
  uint8_t buf[RDBUF_SZ];
} t_preader;

static bool rd_refill(t_preader *r) {
  unsigned n = MIN(RDBUF_SZ, r->remain);
  UINT rdbytes = 0;
  if (!n) {
    r->err = true;
    return false;
  }
  sd_on();
  FRESULT res = f_read(&r->fd, r->buf, n, &rdbytes);
  sd_off();
  if (res != FR_OK || rdbytes != n) {
    r->err = true;
    return false;
  }
  r->remain -= n;
  r->pos = 0;
  r->cnt = n;
  return true;
}

static inline uint8_t rd_u8(t_preader *r) {
  if (r->pos >= r->cnt && !rd_refill(r))
    return 0;
  return r->buf[r->pos++];
}

// Bytes left to read in the file (buffered or not).
static inline uint32_t rd_left(const t_preader *r) {
  return r->remain + (r->cnt - r->pos);
}

static uint32_t rd_be(t_preader *r, unsigned bytes) {
  uint32_t v = 0;
  while (bytes--)
    v = (v << 8) | rd_u8(r);
  return v;
}

// Variable length integer, as used by UPS and BPS.
static uint32_t rd_vlq(t_preader *r) {
  uint32_t data = 0, shift = 1;
  while (!r->err) {
    uint8_t x = rd_u8(r);
    data += (x & 0x7f) * shift;
    if (x & 0x80)
      break;
    shift <<= 7;
    data += shift;
  }
  return data;
}

// Copies (or XORs) len patch bytes straight into SDRAM.
static void rd_to_sdram(t_preader *r, uint32_t off, uint32_t len, bool xor) {
  while (len && !r->err) {
    if (r->pos >= r->cnt && !rd_refill(r))
      return;
    unsigned n = MIN(len, r->cnt - r->pos);
    sdram_write(off, &r->buf[r->pos], n, xor);
    r->pos += n; off += n; len -= n;
  }
}

static const char *const spext[] = { ".ips", ".ups", ".bps" };

// Parses the patch header, returns the target size (0 on error).
static uint32_t parse_header(FIL *fd, unsigned type, uint32_t romfs, uint32_t *hdrlen) {
  uint8_t hdr[32];
  UINT rdbytes;
  if (FR_OK != f_read(fd, hdr, sizeof(hdr), &rdbytes) || rdbytes < 8)
    return 0;

  if (type == SPatchIPS)
    return memcmp(hdr, "PATCH", 5) ? 0 : romfs;   // Real size known at load time.

  // UPS and BPS share the same header layout (magic + VLQs).
  if (memcmp(hdr, type == SPatchUPS ? "UPS1" : "BPS1", 4))
    return 0;
  unsigned p = 4;
  uint32_t v[3];
  for (unsigned i = 0; i < 3; i++) {
    uint32_t data = 0, shift = 1;
    while (p < rdbytes) {
      uint8_t x = hdr[p++];
      data += (x & 0x7f) * shift;
      if (x & 0x80)
        break;
      shift <<= 7;
      data += shift;
    }
    v[i] = data;
  }
  if (v[0] != romfs)
    return 0;   // Patch made for a different ROM.
  if (type == SPatchUPS) {
    // UPS has no metadata size: rewind to the end of the second VLQ.
    p = 4;
    for (unsigned i = 0; i < 2; i++)
      while (p < rdbytes && !(hdr[p++] & 0x80));
  }
  *hdrlen = p;
  return v[1];
}

// Walks the IPS records (skipping their data) to find the patched size.
static uint32_t ips_scan(FIL *fd, uint32_t romfs) {
  uint32_t fsz = f_size(fd), tsize = romfs, pos = 5;
  while (pos + 3 <= fsz) {
    uint8_t rec[8];
    UINT rdbytes;
    if (FR_OK != f_lseek(fd, pos) || FR_OK != f_read(fd, rec, 8, &rdbytes) || rdbytes < 3)
      return 0;
    uint32_t off = (rec[0] << 16) | (rec[1] << 8) | rec[2];
    if (off == 0x454F46)            // "EOF" (a truncation size might follow, ignored)
      return tsize;
    if (rdbytes < 5)
      return 0;
    uint32_t sz = (rec[3] << 8) | rec[4];
    if (!sz) {
      if (rdbytes < 8)
        return 0;
      sz = (rec[5] << 8) | rec[6];
      pos += 8;
    } else
      pos += 5 + sz;
    tsize = MAX(tsize, off + sz);
  }
  return 0;   // No EOF marker
}

// Enables FatFs fast seek (cluster link map) on an open file, if it fits.
static void enable_fastseek(FIL *fd, DWORD *clmt, unsigned entries) {
  clmt[0] = entries;
  fd->cltbl = clmt;
  if (FR_OK != f_lseek(fd, CREATE_LINKMAP))
    fd->cltbl = NULL;
}

uint32_t softpatch_target_size(const t_softpatch *sp, uint32_t romfs) {
  if (!sp->valid)
    return 0;
  if (sp->type != SPatchIPS)
    return sp->tsize;

  FIL fd;
  DWORD clmt[64];
  if (FR_OK != f_open(&fd, sp->fn, FA_READ))
    return 0;
  enable_fastseek(&fd, clmt, 64);
  uint32_t ret = ips_scan(&fd, romfs);
  f_close(&fd);
  return ret <= MAX_GBA_ROM_SIZE ? ret : 0;
}

bool softpatch_find(const char *romfn, uint32_t romfs, t_softpatch *sp) {
  sp->type = SPatchNone;
  sp->valid = false;

  unsigned bl = strlen(romfn);
  const char *dot = strrchr(file_basename(romfn), '.');
  if (dot)
    bl = dot - romfn;
  if (bl + 5 > sizeof(sp->fn))
    return false;

  for (unsigned t = 0; t < 3; t++) {
    memcpy(sp->fn, romfn, bl);
    strcpy(&sp->fn[bl], spext[t]);
    FIL fd;
    if (FR_OK != f_open(&fd, sp->fn, FA_READ))
      continue;

    uint32_t hl = 0;
    sp->type = SPatchIPS + t;
    sp->tsize = parse_header(&fd, sp->type, romfs, &hl);
    sp->valid = sp->tsize && sp->tsize <= MAX_GBA_ROM_SIZE;
    f_close(&fd);
    return true;
  }
  return false;
}

static bool apply_ips(t_preader *r, uint32_t tsize) {
  r->pos += 5;   // "PATCH"
  while (!r->err) {
    uint32_t off = rd_be(r, 3);
    if (off == 0x454F46)
      return true;
    uint32_t sz = rd_be(r, 2);
    if (sz) {
      if (off + sz > tsize)
        return false;
      rd_to_sdram(r, off, sz, false);
    } else {
      uint32_t cnt = rd_be(r, 2);
      uint8_t val = rd_u8(r);
      if (off + cnt > tsize)
        return false;
      uint8_t tmp[64];
      memset(tmp, val, sizeof(tmp));
      while (cnt) {
        unsigned n = MIN(cnt, sizeof(tmp));
        sdram_write(off, tmp, n, false);
        off += n; cnt -= n;
      }
    }
  }
  return false;
}

static bool apply_ups(t_preader *r, uint32_t hl, uint32_t tsize) {
  r->pos = hl;
  uint32_t off = 0;
  while (rd_left(r) > 12 && !r->err) {
    off += rd_vlq(r);
    // XOR bytes until a zero terminator.
    while (!r->err) {
      if (r->pos >= r->cnt && !rd_refill(r))
        return false;
      // Find the run of non-zero bytes in the buffer
      unsigned s = r->pos;
      const uint8_t *z = memchr(&r->buf[s], 0, r->cnt - s);
      unsigned e = z ? (unsigned)(z - r->buf) : r->cnt;
      if (off + (e - s) > tsize)
        return false;
      sdram_write(off, &r->buf[s], e - s, true);
      off += e - s;
      r->pos = e;
      if (e < r->cnt) {     // Found the terminator
        r->pos++;
        off++;
        break;
      }
    }
  }
  return !r->err;
}

static bool apply_bps(t_preader *r, uint32_t hl, uint32_t tsize, const char *romfn, uint32_t romfs) {
  r->pos = hl;
  uint32_t msize = rd_vlq(r);   // Skip metadata
  while (msize--)
    rd_u8(r);

  FIL src;
  DWORD clmt[64];
  bool src_open = false;
  uint32_t outoff = 0, srcrel = 0, tgtrel = 0;
  uint8_t tmp[CPBUF_SZ];

  while (outoff < tsize && rd_left(r) > 12 && !r->err) {
    uint32_t data = rd_vlq(r);
    uint32_t len = (data >> 2) + 1;
    unsigned cmd = data & 3;
    if (outoff + len > tsize)
      break;

    if (cmd == 0) {
      // SourceRead: same offset, ROM data is already there.
    }
    else if (cmd == 1) {
      rd_to_sdram(r, outoff, len, false);
    }
    else {
      uint32_t d = rd_vlq(r);
      int32_t delta = (d & 1) ? -(int32_t)(d >> 1) : (int32_t)(d >> 1);
      if (cmd == 2) {
        srcrel += delta;
        if (srcrel + len > romfs)
          break;
        if (srcrel >= outoff) {
          // Source bytes at or after the output cursor are still untouched.
          for (uint32_t c = 0; c < len; ) {
            unsigned n = MIN(len - c, CPBUF_SZ);
            sdram_read(srcrel + c, tmp, n);
            sdram_write(outoff + c, tmp, n, false);
            c += n;
          }
        } else {
          // Already overwritten, read the original bytes from the ROM file.
          if (!src_open) {
            sd_on();
            src_open = (FR_OK == f_open(&src, romfn, FA_READ));
            if (src_open)
              enable_fastseek(&src, clmt, 64);
            sd_off();
            if (!src_open)
              return false;
          }
          for (uint32_t c = 0; c < len; ) {
            unsigned n = MIN(len - c, CPBUF_SZ);
            UINT rdbytes;
            sd_on();
            bool ok = FR_OK == f_lseek(&src, srcrel + c) &&
                      FR_OK == f_read(&src, tmp, n, &rdbytes) && rdbytes == n;
            sd_off();
            if (!ok)
              return false;
            sdram_write(outoff + c, tmp, n, false);
            c += n;
          }
        }
        srcrel += len;
      } else {
        tgtrel += delta;
        if (tgtrel >= outoff)
          break;
        // Byte-wise semantics: chunks never read what they write.
        for (uint32_t c = 0; c < len; ) {
          unsigned n = MIN(MIN(len - c, CPBUF_SZ), outoff - tgtrel);
          sdram_read(tgtrel + c, tmp, n);
          sdram_write(outoff + c, tmp, n, false);
          c += n;
        }
        tgtrel += len;
      }
    }
    outoff += len;
  }

  if (src_open) {
    sd_on();
    f_close(&src);
    sd_off();
  }
  return !r->err && outoff == tsize;
}

unsigned softpatch_apply(const t_softpatch *sp, const char *romfn, uint32_t romfs, uint32_t tsize) {
  if (!sp->valid || !tsize)
    return ERR_LOAD_BADROM;

  t_preader r;
  memset(&r, 0, sizeof(r) - RDBUF_SZ);
  sd_on();
  FRESULT res = f_open(&r.fd, sp->fn, FA_READ);
  r.remain = f_size(&r.fd);
  sd_off();
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  // Bytes beyond the original ROM read as zero (UPS relies on it).
  if (tsize > romfs)
    sdram_zero(romfs, tsize);

  bool ok = false;
  if (rd_refill(&r)) {
    uint32_t hl = 0;
    if (sp->type == SPatchIPS)
      ok = apply_ips(&r, tsize);
    else {
      // Header length: magic + two or three VLQs.
      unsigned p = 4;
      for (unsigned i = 0; i < 2; i++)
        while (p < r.cnt && !(r.buf[p++] & 0x80));
      hl = p;
      ok = (sp->type == SPatchUPS) ? apply_ups(&r, hl, tsize)
                                   : apply_bps(&r, hl, tsize, romfn, romfs);
    }
  }

  sd_on();
  f_close(&r.fd);
  sd_off();
  return ok ? 0 : ERR_LOAD_BADROM;
}
