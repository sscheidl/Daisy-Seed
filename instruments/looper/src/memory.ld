/* Supplement the unchanged official BOOT_SRAM script (docs/SD_MANAGEMENT.md). */
SECTIONS
{
    /* SDMMC1 IDMA bounce sector of the card reader: AXI SRAM, cache-line aligned. */
    .looper_sd (NOLOAD) : ALIGN(32) { *(.looper_sd) . = ALIGN(32); } > SRAM
    /* Objects constructed at run time (placement new): the loop store and the SD
       management responder. */
    .looper_axi (NOLOAD) : ALIGN(32) { *(.looper_axi) } > SRAM
}
/* Main stack: DTCM above .bss. v0.4.0 left 14.7 KiB; keep at least 12 KiB. */
ASSERT(_ebss <= 0x20020000 - 12K, "DTCM stack reserve below 12 KiB")
/* Boot intent (src/boot_intent.h) lives at a fixed backup SRAM address above
   libDaisy's boot_info, which the external bootloader reads. */
ASSERT(_ebackup_sram <= 0x38800F00, "backup SRAM data reaches the looper boot intent")
