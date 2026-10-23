/* Shared read-only microSD access for Seed3 Desktop firmware (SDMMC1 + FatFs R0.16).
 * One FatFs volume "0:", used from the main loop only; never from an interrupt or
 * the audio callback. The FatFs build is read-only (FF_FS_READONLY 1), so no write,
 * create, rename, delete or format function exists. See common/sd/README.md.
 */
#pragma once
#include <stdint.h>
#include "ff.h"
#include "diskio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* SDMMC1 IDMA target: AXI SRAM or SDRAM (not DTCM), 32-byte aligned, a multiple
     * of 512 bytes. Each transfer reads at most bounce_bytes/512 sectors. FatFs
     * buffers (FATFS, FIL, caller data) may live anywhere. */
    uint8_t* bounce;
    uint32_t bounce_bytes;
    /* Upper bound for one transfer, including waiting for the card's transfer state. */
    uint32_t transfer_timeout_ms;
    /* Upper bound for everything after ro_sd_mount()/ro_sd_restart_budget(); 0 = none. */
    uint32_t budget_ms;
    /* Card-detect switch; NULL treats the card as always present. */
    int (*card_present)(void);
    /* Called after every successful transfer, e.g. USB MIDI service. It must not call
     * FatFs or this driver. NULL = none. */
    void (*service)(void);
} RoSdConfig;

/* Validates and copies the configuration. Returns 0, or -1 for an invalid one.
 * libDaisy's SdmmcHandler must be initialized before the first mount. */
int ro_sd_init(const RoSdConfig* config);

/* Re-initializes the card interface from scratch (also after a card swap or a failed
 * attempt), restarts the budget and mounts volume "0:" into fs. */
FRESULT ro_sd_mount(FATFS* fs);

/* Unregisters the volume. Open FIL/DIR objects become invalid. */
void ro_sd_unmount(void);

/* Restarts the overall budget, e.g. at the start of a diagnostic test. */
void ro_sd_restart_budget(void);

/* Current disk status bits; STA_PROTECT is always set. */
DSTATUS ro_sd_status(void);

#ifdef __cplusplus
}
#endif
