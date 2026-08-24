/* App A linker script - sectors 5-7, starts at 0x08020000 */
ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{
  CCMRAM  (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  RAM     (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  FLASH   (rx)  : ORIGIN = 0x08020000, LENGTH = 384K
}

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >FLASH

  .text :
  {
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
  } >FLASH

  .rodata :
  {
    *(.rodata)
    *(.rodata*)
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    _ebss = .;
    __bss_end__ = _ebss;
    end = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
