/* Supplements the unchanged BOOT_SRAM script (v0.5): the SD-sharing boot intent lives at
   a fixed backup SRAM address above libDaisy's boot_info (common/sdshare/boot_intent.h). */
ASSERT(_ebackup_sram <= 0x38800F00, "backup SRAM data reaches the boot intent")
