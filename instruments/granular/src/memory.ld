/* Supplement the unchanged official BOOT_SRAM script. */
SECTIONS
{
    .neb_sd (NOLOAD) : ALIGN(32) { *(.neb_sd) . = ALIGN(32); } > SRAM
    .neb_heap (NOLOAD) : ALIGN(32)
    {
        __neb_heap_start = .;
        . += 64K;
        __neb_heap_end = .;
    } > RAM_D2
    .neb_file (NOLOAD) : ALIGN(32)
    {
        __neb_file_start = .;
        . = ORIGIN(SDRAM) + LENGTH(SDRAM) - 8M;
        __neb_file_end = .;
    } > SDRAM
}
ASSERT(__neb_file_end > __neb_file_start + 1M, "Insufficient file SDRAM")
ASSERT(__neb_file_end <= 0xc4000000 - 8M, "SDRAM reserve exhausted")
ASSERT(_ebss <= 0x20020000 - 16K, "DTCM stack reserve below 16 KiB")
/* Boot intent (src/boot_intent.h) lives at a fixed backup SRAM address above
   libDaisy's boot_info, which the external bootloader reads. */
ASSERT(_ebackup_sram <= 0x38800F00, "backup SRAM data reaches the Nebulae boot intent")
