@ Start-up code for the SuperFW video player.
  .section .text.start, "ax"
  .arm
  .global _start
_start:
  b rom_start
  .fill 0xBC, 1, 0        @ Header, filled in by fixhdr.py

rom_start:
  mov r0, #0x12           @ IRQ mode stack
  msr cpsr_c, r0
  ldr sp, =0x03007FA0
  mov r0, #0x1F           @ System mode stack
  msr cpsr_c, r0
  ldr sp, =0x03007F00

  @ Copy IWRAM code/data, clear .bss and the EWRAM buffers
  ldr r0, =__iwram_lma
  ldr r1, =__iwram_start
  ldr r2, =__iwram_end
1:cmp r1, r2
  ldrlo r3, [r0], #4
  strlo r3, [r1], #4
  blo 1b
  mov r3, #0
  ldr r1, =__bss_start
  ldr r2, =__bss_end
2:cmp r1, r2
  strlo r3, [r1], #4
  blo 2b
  ldr r1, =__ewram_start
  ldr r2, =__ewram_end
3:cmp r1, r2
  strlo r3, [r1], #4
  blo 3b

  ldr r0, =main
  mov lr, pc
  bx r0
  b .

@ Returns to the SuperFW firmware: map the flash back and reset. It must run
@ from IWRAM since the cart mapping changes under our feet.
  .section .iwram_code, "ax"
  .arm
  .global exit_to_firmware
exit_to_firmware:
  mov r0, #0x04000000
  mov r1, #0
  str r1, [r0, #0x208]    @ IME = 0
  mov r1, #0x0A000000
  ldr r2, =0xA55A
#ifdef SUPERCHIS_IO
  mov r0, #0x100          @ Reset the SuperChis NOR mapping
  .rept 8
  strh r2, [r1, #-2]
  strh r2, [r1, #-2]
  strh r0, [r1, #-2]
  strh r0, [r1, #-2]
  .endr
#endif
  mov r0, #0              @ Firmware (flash) mapped, read-only
  strh r2, [r1, #-2]
  strh r2, [r1, #-2]
  strh r0, [r1, #-2]
  strh r0, [r1, #-2]
  mov r0, #0x04000000
  mov r1, #0
  strb r1, [r0, #-6]      @ Soft reset returns to the ROM (0x08000000)
  swi 0
  .pool
