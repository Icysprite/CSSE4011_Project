#ifndef NUS_PERIPHERAL_H
#define NUS_PERIPHERAL_H

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <stdbool.h>

extern struct bt_conn *current_conn;
extern bool nus_notify_enabled;
extern struct k_work_delayable adv_restart_work;

int nus_peripheral_init(void);

#endif /* NUS_PERIPHERAL_H */