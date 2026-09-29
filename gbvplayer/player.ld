/* Video player: runs from the cart (SDRAM), hot code and data in IWRAM. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
  ROM   : ORIGIN = 0x08000000, LENGTH = 1M
  IWRAM : ORIGIN = 0x03000000, LENGTH = 31K
  EWRAM : ORIGIN = 0x02000000, LENGTH = 256K
}

SECTIONS {
  .text : ALIGN(4) {
    KEEP(*(.text.start))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > ROM

  .iwram : ALIGN(4) {
    __iwram_start = .;
    *(.iwram_code .iwram_code.*)
    *(.data .data.*)
    . = ALIGN(4);
    __iwram_end = .;
  } > IWRAM AT > ROM
  __iwram_lma = LOADADDR(.iwram);

  .bss (NOLOAD) : ALIGN(4) {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > IWRAM

  .ewram (NOLOAD) : ALIGN(4) {
    __ewram_start = .;
    *(.ewram_bss .ewram_bss.*)
    . = ALIGN(4);
    __ewram_end = .;
  } > EWRAM

  __rom_end = __iwram_lma + SIZEOF(.iwram);

  /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
