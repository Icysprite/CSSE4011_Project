#ifndef BLE_H
#define BLE_H

#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

void ble_broadcast_command(uint8_t cmd);
void sniffer_start(void);
void sniffer_device_found(const bt_addr_le_t *addr, int8_t rssi,
                           uint8_t type, struct net_buf_simple *ad);

#endif /* BLE_H */