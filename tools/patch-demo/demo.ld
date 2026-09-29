ENTRY(_start)
SECTIONS {
  . = 0x08000000;
  .text : { KEEP(*(.text.start)) *(.text*) }
  .rodata : { *(.rodata.cfg) *(.rodata*) }
  /DISCARD/ : { *(.ARM.*) *(.comment) }
}
