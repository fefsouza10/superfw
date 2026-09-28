/*
 * Soft-patching (IPS/UPS/BPS) applied while loading a ROM to SDRAM.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#ifndef _SOFTPATCH_H_
#define _SOFTPATCH_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  SPatchNone = 0,
  SPatchIPS  = 1,
  SPatchUPS  = 2,
  SPatchBPS  = 3,
} t_spatch_type;

typedef struct struct_t_softpatch {
  char fn[256];         // Patch file path (next to the ROM)
  uint32_t tsize;       // Size of the patched ROM (IPS: ROM size until scanned)
  uint8_t type;         // t_spatch_type
  bool valid;           // Header parsed OK and matches the ROM size.
} t_softpatch;

// Looks for <rom basename>.ips/.ups/.bps next to the ROM. Returns true if a
// patch file exists (it might still be invalid, check sp->valid).
bool softpatch_find(const char *romfn, uint32_t romfs, t_softpatch *sp);

// Size of the patched ROM (IPS patches are scanned, which can take a moment).
// Returns zero if the patch is not valid.
uint32_t softpatch_target_size(const t_softpatch *sp, uint32_t romfs);

// Applies the patch to the ROM already loaded at 0x08000000 (romfs bytes).
// Must be called with the SD card interface disabled and SDRAM writable.
// Returns zero on success.
unsigned softpatch_apply(const t_softpatch *sp, const char *romfn, uint32_t romfs, uint32_t tsize);

#endif
