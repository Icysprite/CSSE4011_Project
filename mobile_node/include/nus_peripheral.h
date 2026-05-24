#ifndef NUS_PERIPHERAL_H
#define NUS_PERIPHERAL_H

#include <stdbool.h>

extern struct bt_conn *current_conn;
extern bool nus_notify_enabled;

/*
 * Initialise BLE and start advertising as mobile_node.
 */
int nus_peripheral_init(void);

#endif /* NUS_PERIPHERAL_H */