/* Version sources of common/sd for components/registry.json. No firmware includes this
 * file, so the versions never change an image. Named sd_version.h because common/sd is
 * on the include path of its consumers, next to their own version.h.
 *
 * SD_DRIVER_VERSION: disk layer ro_sd.{h,c} and vendored FatFs R0.16 with patches 1 and 2.
 *   0.1.0 read-only (Nebulae 0.2.0); 0.2.0 disk_write, real CTRL_SYNC.
 * SD_FS_READONLY_VERSION: ffconf.h, the read-only profile with its symbol policy.
 * SD_FS_WRITABLE_VERSION: rw/ffconf.h, the writable profile with its symbol policy. */
#pragma once
#define SD_DRIVER_VERSION "0.2.0"
#define SD_FS_READONLY_VERSION "0.1.0"
#define SD_FS_WRITABLE_VERSION "0.1.0"
