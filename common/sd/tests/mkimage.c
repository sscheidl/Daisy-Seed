/* Host tool: builds small FAT32/exFAT card images for the read-only driver tests.
 * Built with tests/mkfs/ffconf.h (writable FatFs); never part of a firmware build.
 * Usage: mkimage fat32|exfat <out.img>
 */
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BYTE* disk;
static LBA_t sectors;

DSTATUS disk_status(BYTE d) { return d ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE d) { return disk_status(d); }
DRESULT disk_read(BYTE d, BYTE* b, LBA_t s, UINT n) {
    if(d || s + n > sectors) return RES_PARERR;
    memcpy(b, disk + s * 512, n * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE d, const BYTE* b, LBA_t s, UINT n) {
    if(d || s + n > sectors) return RES_PARERR;
    memcpy(disk + s * 512, b, n * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE d, BYTE cmd, void* p) {
    if(d) return RES_PARERR;
    if(cmd == CTRL_SYNC) return RES_OK;
    if(cmd == GET_SECTOR_COUNT) { *(LBA_t*)p = sectors; return RES_OK; }
    if(cmd == GET_SECTOR_SIZE) { *(WORD*)p = 512; return RES_OK; }
    if(cmd == GET_BLOCK_SIZE) { *(DWORD*)p = 1; return RES_OK; }
    return RES_PARERR;
}

/* Same pattern as tests/test_ro_sd.cpp: byte i of a file with seed s. */
static BYTE Pattern(unsigned seed, unsigned i) { return (BYTE)(i * 31u + seed * 7u + (i >> 9)); }

static int Put(const char* path, unsigned seed, unsigned size) {
    FIL f; UINT w;
    if(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 1;
    for(unsigned i = 0; i < size; ++i) {
        const BYTE b = Pattern(seed, i);
        if(f_write(&f, &b, 1, &w) != FR_OK || w != 1) return 1;
    }
    return f_close(&f) != FR_OK;
}

int main(int argc, char** argv) {
    static BYTE work[32768];
    static FATFS fs;
    if(argc != 3) { fprintf(stderr, "usage: mkimage fat32|exfat out.img\n"); return 2; }
    const int exfat = !strcmp(argv[1], "exfat");
    MKFS_PARM opt = {0};
    opt.fmt = exfat ? FM_EXFAT : FM_FAT32;
    opt.au_size = exfat ? 4096 : 512;
    sectors = exfat ? 16384 : 70000; /* FAT32 needs more than 65525 clusters */
    disk = calloc((size_t)sectors, 512);
    if(!disk || f_mkfs("0:", &opt, work, sizeof work) != FR_OK || f_mount(&fs, "0:", 1) != FR_OK) {
        fprintf(stderr, "mkfs/mount failed\n"); return 1;
    }
    int err = 0;
    err |= f_mkdir("NEBULAE") != FR_OK;
    err |= f_mkdir("NEBULAE/SAMPLES") != FR_OK;
    err |= Put("NEBULAE/SAMPLES/Field recording.wav", 1, 3000);
    err |= Put("NEBULAE/SAMPLES/kalimba.WAV", 2, 700);
    err |= Put("NEBULAE/SAMPLES/Caf\x82.wav", 3, 513); /* CP850 0x82 = e acute */
    err |= Put("NEBULAE/SAMPLES/._kalimba.WAV", 4, 64);
    err |= Put("NEBULAE/SAMPLES/notes.txt", 5, 10);
    err |= f_mkdir("NEBULAE/SAMPLES/Drones") != FR_OK;
    err |= Put("NEBULAE/SAMPLES/Drones/2 Low hum.wav", 6, 1);
    err |= Put("NEBULAE/SAMPLES/Drones/10 Air.wav", 7, 70000); /* many clusters */
    err |= f_mkdir("NEBULAE/SAMPLES/Drones/alt") != FR_OK;
    err |= f_mkdir("NEBULAE/SAMPLES/Empty") != FR_OK;
    err |= f_mount(0, "0:", 0) != FR_OK;
    FILE* out = fopen(argv[2], "wb");
    if(err || !out || fwrite(disk, 512, (size_t)sectors, out) != (size_t)sectors || fclose(out)) {
        fprintf(stderr, "image build failed\n"); return 1;
    }
    free(disk);
    return 0;
}
