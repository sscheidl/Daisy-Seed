/* Supplement the unchanged official BOOT_SRAM script. */
SECTIONS
{
    /* The engine, constructed at run time by placement new: about 158 KiB, too much for
       the 128 KB DTCM that holds .bss. */
    .audrey_axi (NOLOAD) : ALIGN(32) { *(.audrey_axi) } > SRAM
}
/* Main stack: DTCM above .bss. */
ASSERT(_ebss <= 0x20020000 - 12K, "DTCM stack reserve below 12 KiB")
