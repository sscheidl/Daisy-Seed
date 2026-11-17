/* Option A of the texture load spike: supplements the unchanged official BOOT_SRAM script.
 * The buffer itself lives in SDRAM (DSY_SDRAM_BSS); only the reverb buffer needs a section. */
SECTIONS
{
    .spike_d3 (NOLOAD) : ALIGN(32) { *(.spike_d3) } > RAM_D3
}
/* Main stack: DTCM above .bss. */
ASSERT(_ebss <= 0x20020000 - 16K, "DTCM stack reserve below 16 KiB")
