#pragma once
#include <cstdint>
using BYTE=uint8_t;using DWORD=uint32_t;using WORD=uint16_t;using DSTATUS=unsigned;
#ifndef UINT
using UINT=unsigned;
#endif
enum DRESULT {RES_OK,RES_ERROR,RES_WRPRT,RES_NOTRDY,RES_PARERR};
constexpr unsigned STA_NOINIT=1,CTRL_SYNC=0,GET_SECTOR_COUNT=1,GET_SECTOR_SIZE=2,GET_BLOCK_SIZE=3;
struct Diskio_drvTypeDef {
    DSTATUS(*initialize)(BYTE);DSTATUS(*status)(BYTE);
    DRESULT(*read)(BYTE,BYTE*,DWORD,UINT);DRESULT(*write)(BYTE,const BYTE*,DWORD,UINT);DRESULT(*ioctl)(BYTE,BYTE,void*);
};
