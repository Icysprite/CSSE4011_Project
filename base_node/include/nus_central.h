#ifndef NUS_CENTRAL_H
#define NUS_CENTRAL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Initialise BLE and start scanning for mobile_node.
 */
int nus_central_init(void);

/*
 * Send a command byte to the mobile node over NUS.
 * Returns 0 on success, negative errno on failure.
 */
int nus_send_command(uint8_t cmd);

/*
 * Returns true if connected to mobile_node.
 */
bool nus_central_is_connected(void);

#endif /* NUS_CENTRAL_H */