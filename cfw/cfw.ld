/* Custom firmware code cave for Launchkey Mini MK4 fw 1563.
 *
 * FLASH: free space after the stock image (ends 0x080372B3) up to the end of the
 *        0x38000-byte app slot accepted by the updater.
 * RAM:   just above the stock zero-init region (ends 0x2000F518), far below the
 *        stack (initial SP 0x2000FFF8). The stock startup code does not know this
 *        region: cfw_init() zeroes .bss itself, and initialised .data is forbidden.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x080372B4, LENGTH = 0x08040000 - 0x080372B4
    RAM   (rw)  : ORIGIN = 0x2000F520, LENGTH = 0x340
}

INCLUDE fw_api.ld

SECTIONS
{
    .text : {
        KEEP(*(.text.cfw_entry*))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .data : { *(.data*) } > RAM
    .bss (NOLOAD) : {
        __cfw_bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __cfw_bss_end = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}

ASSERT(SIZEOF(.data) == 0, "cfw: initialised .data is not supported (no startup copy), use const or .bss")
