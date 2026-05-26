/*
 * Black-Valetudo - Mobile Node
 * General BLE implementation
 *
 * Threading model:
 * - BLE callback: pushes raw packets into msgq, returns immediately
 * - NUS send thread (high priority): sends node_records over NUS every SNIFF_WINDOW_MS
 * - Packet processing thread (lower priority): drains msgq, validates seq, updates node_records
 */

#include "ble.h"
#include "nus_peripheral.h"
#include "env_packet.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

#define COMPANY_ID_DATA           0xFFFF
#define COMPANY_ID_CMD            0x0001
#define MFG_DATA_LEN              12

#define SNIFFER_MAX_NODES         8
#define SNIFF_WINDOW_MS           5000
#define NODE_TIMEOUT_MS           30000

#define CMD_BROADCAST_COUNT       3
#define CMD_BROADCAST_DURATION_MS 200
#define CMD_BROADCAST_GAP_MS      50

#define ADV_MSGQ_SIZE             32

#define NUS_SEND_THREAD_STACK     2048
#define NUS_SEND_THREAD_PRIORITY  3

#define PROC_THREAD_STACK         2048
#define PROC_THREAD_PRIORITY      5

#define EOW_NODE_ID "EOW"

extern struct bt_conn *current_conn;
extern bool nus_notify_enabled;
static struct env_record node_records[SNIFFER_MAX_NODES];
static uint8_t node_count = 0;
static struct k_mutex node_records_mutex;

struct raw_adv_record {
    char     node_id[ENV_NODE_NAME_LEN];
    uint8_t  seq;
    int16_t  temp_centi_c;
    uint16_t hum_centi_rh;
    uint16_t eco2_ppm;
    uint16_t tvoc_ppb;
    uint32_t timestamp_ms;
};

K_MSGQ_DEFINE(adv_msgq, sizeof(struct raw_adv_record), ADV_MSGQ_SIZE, 4);
K_MSGQ_DEFINE(cmd_msgq, sizeof(uint8_t), 4, 1);

struct node_seq_entry {
    char     node_id[ENV_NODE_NAME_LEN];
    uint8_t  last_seq;
    uint32_t last_seen_ms;
    bool     valid;
};

static struct node_seq_entry seq_table[SNIFFER_MAX_NODES];

static struct node_seq_entry *find_or_alloc_seq(const char *node_id)
{
    for (int i = 0; i < SNIFFER_MAX_NODES; i++) {
        if (seq_table[i].valid &&
            memcmp(seq_table[i].node_id, node_id, ENV_NODE_NAME_LEN) == 0) {
            return &seq_table[i];
        }
    }

    for (int i = 0; i < SNIFFER_MAX_NODES; i++) {
        if (!seq_table[i].valid) {
            memcpy(seq_table[i].node_id, node_id, ENV_NODE_NAME_LEN);
            seq_table[i].last_seq     = 0;
            seq_table[i].last_seen_ms = 0;
            seq_table[i].valid        = true;
            return &seq_table[i];
        }
    }

    return NULL;
}

static bool seq_is_valid(struct node_seq_entry *entry, uint8_t seq)
{
    uint32_t now = (uint32_t)k_uptime_get();

    if ((now - entry->last_seen_ms) > NODE_TIMEOUT_MS) {
        return true;
    }

    return seq > entry->last_seq;
}

static struct env_record *find_or_alloc_node(const char *name)
{
    for (int i = 0; i < node_count; i++) {
        if (memcmp(node_records[i].node_id, name, ENV_NODE_NAME_LEN) == 0) {
            return &node_records[i];
        }
    }

    if (node_count < SNIFFER_MAX_NODES) {
        return &node_records[node_count++];
    }

    return NULL;
}

struct parse_ctx {
    struct raw_adv_record *rec;
    bool has_name;
    bool has_data;
    bool is_cmd;
};

static bool parse_sniffer_ad(struct bt_data *data, void *user_data)
{
    struct parse_ctx *ctx = user_data;

    if (data->type == BT_DATA_NAME_COMPLETE ||
        data->type == BT_DATA_NAME_SHORTENED) {

        /* Just use name to confirm it's a sensor node */
        if (data->data_len >= 5 &&
            memcmp(data->data, "node_", 5) == 0) {
            ctx->has_name = true;
        }
        return true;
    }

    if (data->type == BT_DATA_MANUFACTURER_DATA) {

        if (data->data_len < 2) {
            return true;
        }

        uint16_t company_id = sys_get_le16(&data->data[0]);

        if (company_id == COMPANY_ID_CMD) {
            ctx->is_cmd = true;
            return true;
        }

        if (company_id == COMPANY_ID_DATA &&
            data->data_len == MFG_DATA_LEN) {

            /* Build node_id string from origin node ID byte */
            uint8_t origin_id = data->data[2];
            snprintf(ctx->rec->node_id, ENV_NODE_NAME_LEN,
                     "node_%u", origin_id);

            ctx->rec->seq          = data->data[3];
            ctx->rec->temp_centi_c = (int16_t)sys_get_le16(&data->data[4]);
            ctx->rec->hum_centi_rh = sys_get_le16(&data->data[6]);
            ctx->rec->eco2_ppm     = sys_get_le16(&data->data[8]);
            ctx->rec->tvoc_ppb     = sys_get_le16(&data->data[10]);
            ctx->has_data          = true;
        }
    }

    return true;
}

void sniffer_device_found(const bt_addr_le_t *addr, int8_t rssi,
                                  uint8_t type, struct net_buf_simple *ad)
{
    struct raw_adv_record raw = {0};
    struct parse_ctx ctx = { .rec = &raw };

    bt_data_parse(ad, parse_sniffer_ad, &ctx);

    if (ctx.is_cmd || !ctx.has_name || !ctx.has_data) {
        return;
    }

    raw.timestamp_ms = (uint32_t)k_uptime_get();

    if (k_msgq_put(&adv_msgq, &raw, K_NO_WAIT) != 0) {
        LOG_WRN("adv_msgq full, dropping packet from %s", raw.node_id);
    }
}

static void do_broadcast_command(uint8_t cmd)
{
    static uint8_t seq = 0;
    static uint8_t cmd_payload[4];

    for (int i = 0; i < CMD_BROADCAST_COUNT; i++) {
        sys_put_le16(COMPANY_ID_CMD, &cmd_payload[0]);
        cmd_payload[2] = seq++;
        cmd_payload[3] = cmd;

        struct bt_data ad[] = {
            BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
            BT_DATA(BT_DATA_MANUFACTURER_DATA,
                    cmd_payload, sizeof(cmd_payload)),
        };

        int err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad),
                                  NULL, 0);
        if (err) {
            printk("Command broadcast failed (err %d)\n", err);
            return;
        }

        k_sleep(K_MSEC(CMD_BROADCAST_DURATION_MS));
        bt_le_adv_stop();
        k_sleep(K_MSEC(CMD_BROADCAST_GAP_MS));
    }

    printk("Command 0x%02X broadcast into mesh (%d times)\n",
           cmd, CMD_BROADCAST_COUNT);
    
    printk("Restarting mobile_node advertisement...\n");

    k_work_reschedule(&adv_restart_work, K_MSEC(100));
}

void ble_broadcast_command(uint8_t cmd)
{
    if (k_msgq_put(&cmd_msgq, &cmd, K_NO_WAIT) != 0) {
        printk("cmd_msgq full, dropping command 0x%02X\n", cmd);
    }
}

K_THREAD_STACK_DEFINE(nus_send_stack, NUS_SEND_THREAD_STACK);
static struct k_thread nus_send_thread_data;

static void nus_send_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_sleep(K_MSEC(SNIFF_WINDOW_MS));

        k_mutex_lock(&node_records_mutex, K_FOREVER);

        for (int i = 0; i < node_count; i++) {
            if (current_conn == NULL || !nus_notify_enabled) {
                printk("Cannot send — not connected or notify disabled\n");
                break;
            }

            int ret = bt_nus_send(current_conn,
                                  (uint8_t *)&node_records[i],
                                  sizeof(struct env_record));
            if (ret) {
                printk("NUS send failed for %s (err %d)\n",
                       node_records[i].node_id, ret);
            } else {
                printk("Sent record for %.6s\n",
                       node_records[i].node_id);
            }
        }

        /* Send EOW marker, indicates transmission of node telemetry data is over */
        if (current_conn != NULL && nus_notify_enabled) {
            struct env_record eow = {0};
            memcpy(eow.node_id, EOW_NODE_ID, strlen(EOW_NODE_ID));
            int ret = bt_nus_send(current_conn,
                                  (uint8_t *)&eow,
                                  sizeof(struct env_record));
            if (ret) {
                printk("EOW send failed (err %d)\n", ret);
            } else {
                printk("EOW sent\n");
            }
        }

        memset(node_records, 0, sizeof(node_records));
        node_count = 0;

        k_mutex_unlock(&node_records_mutex);
    }
}

K_THREAD_STACK_DEFINE(proc_stack, PROC_THREAD_STACK);
static struct k_thread proc_thread_data;

static void proc_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct raw_adv_record raw;
    uint8_t cmd;

    while (1) {
        while (k_msgq_get(&adv_msgq, &raw, K_NO_WAIT) == 0) {

            struct node_seq_entry *seq_entry =
                find_or_alloc_seq(raw.node_id);

            if (!seq_entry) {
                continue;
            }

            if (!seq_is_valid(seq_entry, raw.seq)) {
                continue;
            }

            seq_entry->last_seq     = raw.seq;
            seq_entry->last_seen_ms = raw.timestamp_ms;

            k_mutex_lock(&node_records_mutex, K_FOREVER);
            struct env_record *slot = find_or_alloc_node(raw.node_id);
            if (slot) {
                memcpy(slot->node_id, raw.node_id, ENV_NODE_NAME_LEN);
                slot->timestamp_ms  = raw.timestamp_ms;
                slot->temp_centi_c  = raw.temp_centi_c;
                slot->hum_centi_rh  = raw.hum_centi_rh;
                slot->eco2_ppm      = raw.eco2_ppm;
                slot->tvoc_ppb      = raw.tvoc_ppb;
            }
            k_mutex_unlock(&node_records_mutex);
        }

        while (k_msgq_get(&cmd_msgq, &cmd, K_NO_WAIT) == 0) {
            do_broadcast_command(cmd);
        }

        k_sleep(K_MSEC(10));
    }
}

void sniffer_start(void)
{
    k_mutex_init(&node_records_mutex);
    memset(seq_table, 0, sizeof(seq_table));

    static const struct bt_le_scan_param scan_param = {
        .type     = BT_LE_SCAN_TYPE_ACTIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window   = BT_GAP_SCAN_FAST_WINDOW,
    };

    int err = bt_le_scan_start(&scan_param, sniffer_device_found);
    if (err) {
        printk("Sniffer scan start failed (err %d)\n", err);
        return;
    }

    printk("Sniffer started, listening for sensor nodes...\n");

    k_thread_create(&nus_send_thread_data, nus_send_stack,
                    NUS_SEND_THREAD_STACK,
                    nus_send_thread, NULL, NULL, NULL,
                    NUS_SEND_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&nus_send_thread_data, "nus_send");

    /* Send environmental data off to base node */
    k_thread_create(&proc_thread_data, proc_stack,
                    PROC_THREAD_STACK,
                    proc_thread, NULL, NULL, NULL,
                    PROC_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&proc_thread_data, "proc");
}