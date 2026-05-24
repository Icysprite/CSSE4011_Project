/*
 * Black-Valetudo - Mobile Node
 * NUS Peripheral implementation
 */

#include "nus_peripheral.h"
#include "ble.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/nus.h>
#include <string.h>

LOG_MODULE_REGISTER(nus_peripheral, LOG_LEVEL_INF);

#define CMD_MEASURE 0x01
#define CMD_SLEEP   0x02

/* ==========================================================================
 * State
 * Non-static so ble.c can access via extern
 * ========================================================================== */

struct bt_conn *current_conn;
bool nus_notify_enabled = false;

static struct k_work_delayable adv_restart_work;

static void nus_notif_changed(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    nus_notify_enabled = enabled;
    printk("NUS notify %s\n", enabled ? "enabled" : "disabled");
}

/* ==========================================================================
 * Advertising Data
 * ========================================================================== */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
                  BT_UUID_NUS_SRV_VAL),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ==========================================================================
 * Advertising restart work
 * ========================================================================== */

static void adv_restart_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising restart failed (err %d)", err);
    } else {
        printk("Advertising as mobile_node...\n");
    }
}

/* ==========================================================================
 * NUS Callbacks
 * ========================================================================== */

static void on_receive(struct bt_conn *conn,
                       const void *data, uint16_t len,
                       void *ctx)
{
    ARG_UNUSED(ctx);

    const uint8_t *bytes = data;

    if (len < 1) {
        return;
    }

    switch (bytes[0]) {
    case CMD_MEASURE:
        printk("Command received: CMD_MEASURE\n");
        ble_broadcast_command(CMD_MEASURE);
        break;
    case CMD_SLEEP:
        printk("Command received: CMD_SLEEP\n");
        ble_broadcast_command(CMD_SLEEP);
        break;
    default:
        printk("Command received: unknown (0x%02X)\n", bytes[0]);
        break;
    }
}

static struct bt_nus_cb nus_cb = {
    .received      = on_receive,
    .notif_enabled = nus_notif_changed,
};

/* ==========================================================================
 * Connection Callbacks
 * ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    printk("Connected to base_node (%s)\n", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Disconnected from base_node (%s) reason 0x%02x\n", addr, reason);

    if (current_conn == conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    k_work_reschedule(&adv_restart_work, K_MSEC(200));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

/* ==========================================================================
 * Init
 * ========================================================================== */

int nus_peripheral_init(void)
{
    int err;

    k_work_init_delayable(&adv_restart_work, adv_restart_fn);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    LOG_INF("Bluetooth initialised");

    err = bt_nus_cb_register(&nus_cb, NULL);
    if (err) {
        LOG_ERR("NUS callback register failed (err %d)", err);
        return err;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed (err %d)", err);
        return err;
    }

    printk("Advertising as mobile_node...\n");

    return 0;
}