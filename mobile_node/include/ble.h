#ifndef BLE_H
#define BLE_H

#include <stdint.h>

void ble_broadcast_command(uint8_t cmd);
void sniffer_start(void);

#endif /* BLE_H */