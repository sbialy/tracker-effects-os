ENTRY(entry)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 36M }
SECTIONS
{
    .text : { *(.text .text.*) *(.rodata .rodata.*) *(.ARM.extab* .gnu.linkonce.armextab.*) } > RAM
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > RAM
    . = ALIGN(4);
    .init_array : { __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; } > RAM
    .data : { *(.data .data.*) } > RAM
    . = ALIGN(8);
    .bss : { __bss_start = .; *(.bss .bss.* COMMON) *(.noinit*) __bss_end = .; } > RAM
    . = ALIGN(16);
    _end = .; PROVIDE(end = .);
}
