/*
 * lightc — tiny linker script
 * Two segments, minimal alignment waste.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R+X */
    data PT_LOAD FLAGS(6);   /* R+W */
}

SECTIONS
{
    . = 0x400000 + SIZEOF_HEADERS;

    .text   : { *(.text) *(.text.*) } :text
    .rodata : { *(.rodata) *(.rodata.*) } :text

    . = ALIGN(4096);

    .data : { *(.data) *(.data.*) } :data
    .bss  : { *(.bss) *(.bss.*) *(COMMON) } :data

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gnu.hash)
        *(.gnu.property)
    }
}
