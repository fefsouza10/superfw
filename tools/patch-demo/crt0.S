@ Minimal GBA start-up code for the soft-patch demo ROM.
  .section .text.start, "ax"
  .arm
  .global _start
_start:
  b rom_start
  .fill 0xBC, 1, 0        @ Header, filled in by mkdemo.py
rom_start:
  mov r0, #0x12           @ IRQ mode stack
  msr cpsr_c, r0
  ldr sp, =0x03007FA0
  mov r0, #0x1F           @ System mode stack
  msr cpsr_c, r0
  ldr sp, =0x03007F00
  ldr r0, =main
  bx r0
  .pool
