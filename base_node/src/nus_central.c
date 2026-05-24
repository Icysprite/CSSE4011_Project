/*
 * Black-Valetudo - Base Node
 * NUS Central implementation
 */

#include "nus_central.h"
#include "env_packet.h"
#include "json_serial.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/data/json.h>

LOG_MODULE_REGISTER(nus_central, LOG_LEVEL_INF);

#define MOBILE_NODE_NAME "mobile_node"

/* ==========================================================================
 * State
 * ========================================================================== */

static struct bt_conn *default_conn;

static struct bt_uuid_128 discover_uuid;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static uint16_t nus_rx_handle;
static bool is_connected;

static struct k_work_delayable scan_restart_work;

/* ==========================================================================
 * Public API
 * ========================================================================== */

bool nus_central_is_connected(void)
{
    return is_connected;
}

/* ==========================================================================
 * Forward declarations
 * ========================================================================== */

static void start_scan(void);

/* ==========================================================================
 * Scan restart work
 * ========================================================================== */

static void scan_restart_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    start_scan();
}

/* ==========================================================================
 * GATT Notification Callback
 * ========================================================================== */

static uint8_t notify_func(struct bt_conn *conn,
                           struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    if (!data) {
        LOG_WRN("Unsubscribed");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    if (length != sizeof(struct env_record)) {
        LOG_WRN("Unexpected packet size: %u (expected %u)",
                length, sizeof(struct env_record));
        return BT_GATT_ITER_CONTINUE;
    }

    struct env_record rec;
    memcpy(&rec, data, sizeof(struct env_record));

    json_serial_send(&rec);

    return BT_GATT_ITER_CONTINUE;
}

/* ==========================================================================
 * GATT Discovery
 * ========================================================================== */

static uint8_t discover_func(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    int err;

    if (!attr) {
        LOG_WRN("Discovery complete");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    if (!bt_uuid_cmp(discover_params.uuid,
                     BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL))) {

        LOG_INF("NUS Service found");

        memcpy(&discover_uuid,
               BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL),
               sizeof(discover_uuid));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("TX char discover failed (err %d)", err);
        }

    } else if (!bt_uuid_cmp(discover_params.uuid,
                            BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL))) {

        LOG_INF("NUS TX Characteristic found");

        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        memcpy(&discover_uuid, BT_UUID_GATT_CCC,
               sizeof(struct bt_uuid_16));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type         = BT_GATT_DISCOVER_DESCRIPTOR;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("CCC discover failed (err %d)", err);
        }

    } else {
        LOG_INF("CCC found, subscribing to notifications");

        subscribe_params.notify     = notify_func;
        subscribe_params.value      = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            LOG_ERR("Subscribe failed (err %d)", err);
        } else {
            LOG_INF("Subscribed to NUS TX notifications");
        }
    }

    return BT_GATT_ITER_STOP;
}

/* ==========================================================================
 * NUS RX Handle Discovery
 * ========================================================================== */

static struct bt_uuid_128 rx_discover_uuid;
static struct bt_gatt_discover_params rx_discover_params;

static uint8_t rx_discover_func(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        LOG_WRN("NUS RX characteristic not found");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    nus_rx_handle = bt_gatt_attr_value_handle(attr);
    LOG_INF("NUS RX ready (handle %u)", nus_rx_handle);

    return BT_GATT_ITER_STOP;
}

static void discover_nus_rx(struct bt_conn *conn)
{
    int err;

    memcpy(&rx_discover_uuid,
           BT_UUID_DECLARE_128(BT_UUID_NUS_RX_CHAR_VAL),
           sizeof(rx_discover_uuid));

    rx_discover_params.uuid         = &rx_discover_uuid.uuid;
    rx_discover_params.func         = rx_discover_func;
    rx_discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    rx_discover_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    rx_discover_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

    err = bt_gatt_discover(conn, &rx_discover_params);
    if (err) {
        LOG_ERR("RX discover failed (err %d)", err);
    }
}

/* ==========================================================================
 * Sending Commands
 * ========================================================================== */

static void write_func(struct bt_conn *conn, uint8_t err,
                       struct bt_gatt_write_params *params)
{
    if (err) {
        LOG_ERR("Write failed (err %u)", err);
    } else {
        LOG_INF("Command sent successfully");
    }
}

static struct bt_gatt_write_params write_params;

int nus_send_command(uint8_t cmd)
{
    if (!is_connected) {
        LOG_WRN("Not connected to mobile node");
        return -ENOTCONN;
    }

    if (nus_rx_handle == 0) {
        LOG_ERR("NUS RX handle not discovered yet");
        return -EINVAL;
    }

    static uint8_t cmd_buf;
    cmd_buf = cmd;

    write_params.func   = write_func;
    write_params.handle = nus_rx_handle;
    write_params.offset = 0;
    write_params.data   = &cmd_buf;
    write_params.length = sizeof(cmd_buf);

    return bt_gatt_write(default_conn, &write_params);
}

/* ==========================================================================
 * Scanning
 * ========================================================================== */

static bool parse_ad_for_name(struct bt_data *data, void *user_data)
{
    bool *found = user_data;

    if (data->type == BT_DATA_NAME_COMPLETE ||
        data->type == BT_DATA_NAME_SHORTENED) {
        if (data->data_len == strlen(MOBILE_NODE_NAME) &&
            memcmp(data->data, MOBILE_NODE_NAME,
                   data->data_len) == 0) {
            *found = true;
            return false;
        }
    }

    return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                         struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    int err;

    if (default_conn) {
        return;
    }

    if (type != BT_GAP_ADV_TYPE_ADV_IND &&
        type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_SCAN_RSP) {
        return;
    }

    bool found = false;
    bt_data_parse(ad, parse_ad_for_name, &found);
    if (!found) {
        return;
    }

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    LOG_INF("Found mobile_node: %s (RSSI %d)", addr_str, rssi);

    bt_le_scan_stop();

    struct bt_conn_le_create_param create_param =
        BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
                                     BT_GAP_SCAN_FAST_INTERVAL,
                                     BT_GAP_SCAN_FAST_INTERVAL);

    struct bt_le_conn_param conn_param =
        BT_LE_CONN_PARAM_INIT(BT_GAP_INIT_CONN_INT_MIN,
                              BT_GAP_INIT_CONN_INT_MAX, 0, 100);

    err = bt_conn_le_create(addr, &create_param, &conn_param, &default_conn);
    if (err) {
        LOG_ERR("Connection failed (err %d)", err);
        default_conn = NULL;
        start_scan();
    }
}

static void start_scan(void)
{
    int err;

    struct bt_le_scan_param scan_param = {
        .type     = BT_LE_SCAN_TYPE_ACTIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window   = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, device_found);
    if (err) {
        LOG_ERR("Scan failed (err %d)", err);
        return;
    }

    printk("Scanning for mobile_node...\n");
}

/* ==========================================================================
 * MTU and Data Length
 * ========================================================================== */

static void update_data_length(struct bt_conn *conn)
{
    int err;
    struct bt_conn_le_data_len_param dl_param = {
        .tx_max_len  = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };

    err = bt_conn_le_data_len_update(conn, &dl_param);
    if (err) {
        LOG_ERR("Data length update failed (err %d)", err);
    }
}

static void on_le_data_len_updated(struct bt_conn *conn,
                                   struct bt_conn_le_data_len_info *info)
{
    LOG_INF("Data length updated: TX %u bytes, RX %u bytes",
            info->tx_max_len, info->rx_max_len);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params)
{
    if (err) {
        LOG_ERR("MTU exchange failed (err %u)", err);
    } else {
        LOG_INF("MTU exchange successful: %u bytes payload",
                bt_gatt_get_mtu(conn) - 3);
    }
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_cb,
};

/* ==========================================================================
 * Connection Callbacks
 * ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
        LOG_ERR("Connection failed (err %u)", conn_err);
        bt_conn_unref(default_conn);
        default_conn = NULL;
        start_scan();
        return;
    }

    if (default_conn == NULL) {
        default_conn = bt_conn_ref(conn);
    }

    is_connected = true;
    printk("Connected to mobile_node (%s)\n", addr);

    update_data_length(conn);

    err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
    if (err) {
        LOG_WRN("MTU exchange failed (err %d)", err);
    }

    memcpy(&discover_uuid,
           BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL),
           sizeof(discover_uuid));
    discover_params.uuid         = &discover_uuid.uuid;
    discover_params.func         = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type         = BT_GATT_DISCOVER_PRIMARY;

    err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        LOG_ERR("Service discover failed (err %d)", err);
        return;
    }

    discover_nus_rx(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (default_conn != conn) {
        return;
    }

    is_connected  = false;
    nus_rx_handle = 0;

    memset(&subscribe_params, 0, sizeof(subscribe_params));
    memset(&discover_params,  0, sizeof(discover_params));

    bt_conn_unref(default_conn);
    default_conn = NULL;

    printk("Disconnected from mobile_node (%s) reason 0x%02x\n", addr, reason);

    k_work_reschedule(&scan_restart_work, K_MSEC(200));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected           = connected,
    .disconnected        = disconnected,
    .le_data_len_updated = on_le_data_len_updated,
};

/* ==========================================================================
 * Init
 * ========================================================================== */

int nus_central_init(void)
{
    k_work_init_delayable(&scan_restart_work, scan_restart_fn);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    LOG_INF("Bluetooth initialised");
    start_scan();

    return 0;
}