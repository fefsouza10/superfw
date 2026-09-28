/*
 * Cover-art / title-screen preview for the ROM browser.
 *
 * Reads EZ-Flash-Omega style thumbnails directly from the SD card:
 *   /IMGS/{c0}/{c1}/{CODE}.bmp  (120x80, 16bpp X1R5G5B5 BMP, keyed by GBA game code)
 * Each pixel is mapped on the fly to a fixed 6x6x6 (216 color) palette cube that
 * lives in the free BG palette indices 20..235, and blitted into a bottom-right
 * pane of the Mode-4 menu framebuffer (EZ-Flash-Omega style).
 */
#ifndef __COVERART_H__
#define __COVERART_H__

#include <stdint.h>
#include <stdbool.h>

// Maximum source thumbnail size (matches the EZ-Omega .bmp pack).
#define COVER_SRC_W      120
#define COVER_SRC_H      80

// Displayed size (even width, the framebuffer is written 16 bits at a time).
#define COVER_W          76
#define COVER_H          50

// Fixed 6x6x6 color cube, placed in the free BG palette range 20..235
// (theme=16..19, logo=1..15, IGM=240..244, selector=255 are left untouched).
#define CUBE_PAL_BASE    20
#define CUBE_NCOLORS     216

// Bottom-right pane, just above the y=144 footer bar.
#define COVER_PANE_X     (240 - COVER_W - 4)    // 160
#define COVER_PANE_Y     (144 - COVER_H - 4)    // 90

// Number of consecutive frames the selection must stay on the same entry, with
// no key held, before its cover is read from SD (cached covers show at once).
#define COVER_LOAD_DELAY     4
// Idle frames before the covers of the neighbouring entries are prefetched.
#define COVER_PREFETCH_DELAY 20

// Size of one cover image and of the cache buffer the caller must provide.
#define COVER_BUF_SIZE     (COVER_W * COVER_H)
#define COVER_CACHE_SLOTS  40
#define COVER_CACHE_SIZE   (1024 * 16 + 64 * 4 + \
                            COVER_CACHE_SLOTS * (COVER_BUF_SIZE + 12 + CUBE_NCOLORS * 2))

// Sets the (COVER_CACHE_SIZE bytes, word aligned, cart SDRAM) cache buffer and
// clears the state. Must be called before any other function.
void coverart_init(void *cachemem);

// Hide the cover (no entry selected, or the feature is off).
void coverart_invalidate(void);

// Call once per frame with the selected entry. Cached covers show at once;
// otherwise the SD card is only read once the selection settles on the entry
// (COVER_LOAD_DELAY idle frames), so it is cheap to call per frame.
// Pass is_gba=false for entries that have no cover (dirs, other files).
void coverart_update(const char *rom_fullpath, uint32_t filesize, bool is_gba);

// Like coverart_update but keyed directly by a stored 4-char game code (no ROM
// header read) -- used for NOR/flash games.
void coverart_update_gcode(const uint8_t gcode[4]);

// Prefetching: after coverart_update*, if coverart_prefetch_ready() the menu
// offers neighbouring entries (nearest first) to coverart_prefetch*, stopping
// at the first call that returns true (it read the SD card; one read per
// frame). If none did, it calls coverart_prefetch_finished().
bool coverart_prefetch_ready(void);
bool coverart_prefetch(const char *rom_fullpath, uint32_t filesize);
bool coverart_prefetch_gcode(const uint8_t gcode[4]);
void coverart_prefetch_finished(void);

// Whether a cover is currently loaded and should be drawn.
bool coverart_available(void);

// Blit the loaded cover into the bottom-right pane of `frame`.
void coverart_draw(volatile uint8_t *frame);

#endif
