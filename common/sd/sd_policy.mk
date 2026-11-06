# Symbol policy of the profiles of common/sd: one definition for the images (sd.mk,
# make sd-check) and for the object check (Makefile, make arm-check). Names are removed from
# the ban list of scripts/check-sd-symbols.py; *_PROBES are injected and must be rejected.

# Writable profile: IDMA block writes and the FatFs file API.
SD_WRITE_ALLOW = disk_write,f_write,f_unlink,f_rename,f_mkdir,f_truncate,f_sync,BSP_SD_WriteBlocks_DMA,HAL_SD_WriteBlocks_DMA,SDMMC_CmdWriteSingleBlock,SDMMC_CmdWriteMultiBlock
SD_WRITE_PROBES = f_mkfs,f_fdisk,f_setlabel,f_chmod,HAL_SD_WriteBlocks,HAL_SD_WriteBlocks_IT,HAL_SD_Erase,BSP_SD_Erase,SDMMC_CmdLockUnlock,SDMMC_CmdErase,FATFS_LinkDriver,SD_Driver

# Read-only profile.
SD_READONLY_PROBES = disk_write,f_write,f_unlink,f_rename,f_mkdir,f_mkfs,BSP_SD_WriteBlocks_DMA,HAL_SD_WriteBlocks,HAL_SD_WriteBlocks_DMA,SDMMC_CmdWriteMultiBlock,HAL_SD_Erase

# Read-only profile next to the card reader (Nebulae): the reader's raw IDMA writes only.
SD_READER_ALLOW = SDMMC_CmdWriteSingleBlock,SDMMC_CmdWriteMultiBlock,HAL_SD_WriteBlocks_DMA
SD_READER_PROBES = disk_write,f_write,f_unlink,f_rename,f_mkdir,f_mkfs,BSP_SD_WriteBlocks_DMA,BSP_SD_WriteBlocks,HAL_SD_WriteBlocks,HAL_SD_WriteBlocks_IT,HAL_SD_Erase,SDMMC_CmdLockUnlock
