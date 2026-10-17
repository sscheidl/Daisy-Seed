#pragma once
#include <cstdint>

// Host stand-in for libDaisy's USB device state. Firmware reads dev_state only.
#define USBD_STATE_ADDRESSED 0x02U
#define USBD_STATE_CONFIGURED 0x03U
struct USBD_HandleTypeDef {
    volatile uint8_t dev_state = 0;
};
