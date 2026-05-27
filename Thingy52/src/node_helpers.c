#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include "node_helpers.h"



static enum node_mode current_mode = MODE_NORMAL;


//define sensor devicess
static const struct device *hts221_dev = DEVICE_DT_GET_ONE(st_hts221);
static const struct device *ccs811_dev = DEVICE_DT_GET_ONE(ams_ccs811);
static bool sensors_suspended = false;

//sensor and command data to be advertised
static uint8_t current_sensor_adv_data[SENSOR_PACKET_LEN];
static uint8_t current_command_adv_data[COMMAND_PACKET_LEN];
static bool adv_running = false;

static struct seen_sensor_entry seen_sensor_cache[SEEN_SENSOR_CACHE_SIZE];
static uint8_t seen_sensor_write_index = 0;

static struct seen_command_entry seen_command_cache[SEEN_COMMAND_CACHE_SIZE];
static uint8_t seen_command_write_index = 0;

//mutexes to prevent racing
K_MUTEX_DEFINE(mode_mutex);
K_MUTEX_DEFINE(adv_mutex);
K_MUTEX_DEFINE(seen_sensor_mutex);
K_MUTEX_DEFINE(seen_command_mutex);

#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
#define WDT_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_nrf_wdt)
static const struct device *wdt_dev = DEVICE_DT_GET(WDT_NODE);
#else
#error "No watchdog device found in devicetree"
#endif

static int wdt_channel_id = -1;

/*Defining structs for BLE advertising
  Using company ID to distinguish sensor and command
  set scanning and advertising speed fast*/
static const struct bt_data sensor_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA,
            current_sensor_adv_data,
            sizeof(current_sensor_adv_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            DEVICE_NAME,
            sizeof(DEVICE_NAME) - 1),
};

static const struct bt_data command_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA,
            current_command_adv_data,
            sizeof(current_command_adv_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            DEVICE_NAME,
            sizeof(DEVICE_NAME) - 1),
};

static const struct bt_le_adv_param fast_adv_param = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = BT_LE_ADV_OPT_USE_IDENTITY,
    .interval_min = 0x0020,
    .interval_max = 0x0040,
    .peer = NULL,
};

static const struct bt_le_scan_param fast_scan_param = {
    .type = BT_LE_SCAN_TYPE_PASSIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = 0x0010,
    .window = 0x0010,
};


//read and change node mode with mutex protection
enum node_mode get_node_mode(void)
{
    enum node_mode mode;

    k_mutex_lock(&mode_mutex, K_FOREVER);
    mode = current_mode;
    k_mutex_unlock(&mode_mutex);

    return mode;
}

void set_node_mode(enum node_mode mode)
{
    k_mutex_lock(&mode_mutex, K_FOREVER);

    if (current_mode != mode) {
        current_mode = mode;

        if (mode == MODE_NORMAL) {
            printk("MODE CHANGE: NORMAL\n");
        } else {
            printk("MODE CHANGE: LOW_POWER\n");
        }
    }

    k_mutex_unlock(&mode_mutex);
}


//modify sensor's behaviour according to 
bool sensors_are_ready(void)
{
    if (!device_is_ready(hts221_dev)) {
        printk("HTS221 device is not ready\n");
        return false;
    }

    if (!device_is_ready(ccs811_dev)) {
        printk("CCS811 device is not ready\n");
        return false;
    }

    return true;
}

void suspend_sensors(void)
{
    int ret;

    if (sensors_suspended) {
        return;
    }

    ret = pm_device_action_run(hts221_dev, PM_DEVICE_ACTION_SUSPEND);
    printk("HTS221 suspend ret=%d\n", ret);

    ret = pm_device_action_run(ccs811_dev, PM_DEVICE_ACTION_SUSPEND);
    printk("CCS811 suspend ret=%d\n", ret);

    sensors_suspended = true;
}

void resume_sensors(void)
{
    int ret;

    if (!sensors_suspended) {
        return;
    }

    ret = pm_device_action_run(hts221_dev, PM_DEVICE_ACTION_RESUME);
    printk("HTS221 resume ret=%d\n", ret);

    ret = pm_device_action_run(ccs811_dev, PM_DEVICE_ACTION_RESUME);
    printk("CCS811 resume ret=%d\n", ret);

    sensors_suspended = false;
    k_sleep(K_SECONDS(2));
}

int read_hts221(int16_t *temp_centi_c, uint16_t *hum_centi_percent)
{
    struct sensor_value temp;
    struct sensor_value hum;
    int ret;

    ret = sensor_sample_fetch(hts221_dev);
    if (ret < 0) {
        printk("HTS221: sample fetch failed: %d\n", ret);
        return ret;
    }

    ret = sensor_channel_get(hts221_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    if (ret < 0) {
        printk("HTS221: temperature read failed: %d\n", ret);
        return ret;
    }

    ret = sensor_channel_get(hts221_dev, SENSOR_CHAN_HUMIDITY, &hum);
    if (ret < 0) {
        printk("HTS221: humidity read failed: %d\n", ret);
        return ret;
    }

    *temp_centi_c = (int16_t)((temp.val1 * 100) + (temp.val2 / 10000));
    *hum_centi_percent = (uint16_t)((hum.val1 * 100) + (hum.val2 / 10000));

    return 0;
}

int read_ccs811(uint16_t *eco2_ppm, uint16_t *tvoc_ppb)
{
    struct sensor_value co2;
    struct sensor_value voc;
    int ret;

    ret = sensor_sample_fetch(ccs811_dev);
    if (ret < 0) {
        printk("CCS811: sample fetch failed: %d\n", ret);
        return ret;
    }

    ret = sensor_channel_get(ccs811_dev, SENSOR_CHAN_CO2, &co2);
    if (ret < 0) {
        printk("CCS811: eCO2 read failed: %d\n", ret);
        return ret;
    }

    ret = sensor_channel_get(ccs811_dev, SENSOR_CHAN_VOC, &voc);
    if (ret < 0) {
        printk("CCS811: TVOC read failed: %d\n", ret);
        return ret;
    }

    *eco2_ppm = (uint16_t)co2.val1;
    *tvoc_ppb = (uint16_t)voc.val1;

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Packet handling                                                            */
/* -------------------------------------------------------------------------- */

void encode_sensor_packet(struct flood_packet *packet,
                          uint8_t origin_node_id,
                          uint8_t seq,
                          int16_t temp_centi_c,
                          uint16_t hum_centi_percent,
                          uint16_t eco2_ppm,
                          uint16_t tvoc_ppb)
{
    packet->type = FLOOD_PACKET_SENSOR;
    packet->len = SENSOR_PACKET_LEN;

    sys_put_le16(SENSOR_COMPANY_ID, &packet->data[0]);

    packet->data[2] = origin_node_id;
    packet->data[3] = seq;

    sys_put_le16((uint16_t)temp_centi_c, &packet->data[4]);
    sys_put_le16(hum_centi_percent, &packet->data[6]);
    sys_put_le16(eco2_ppm, &packet->data[8]);
    sys_put_le16(tvoc_ppb, &packet->data[10]);
}

void encode_command_packet(struct flood_packet *packet, uint8_t seq, uint8_t cmd)
{
    packet->type = FLOOD_PACKET_COMMAND;
    packet->len = COMMAND_PACKET_LEN;

    sys_put_le16(MOBILE_COMPANY_ID, &packet->data[0]);
    packet->data[2] = seq;
    packet->data[3] = cmd;
}

void print_sensor_packet(const char *prefix, const struct flood_packet *packet, int8_t rssi)
{
    uint8_t origin_node_id = packet->data[2];
    uint8_t seq = packet->data[3];

    int16_t temp_centi_c = (int16_t)sys_get_le16(&packet->data[4]);
    uint16_t hum_centi_percent = sys_get_le16(&packet->data[6]);
    uint16_t eco2_ppm = sys_get_le16(&packet->data[8]);
    uint16_t tvoc_ppb = sys_get_le16(&packet->data[10]);

    printk("%s SENSOR origin=node_%u seq=%u rssi=%d "
           "T=%d.%02dC H=%u.%02u%% eCO2=%u TVOC=%u\n",
           prefix,
           origin_node_id,
           seq,
           rssi,
           temp_centi_c / 100,
           temp_centi_c < 0 ? -(temp_centi_c % 100) : temp_centi_c % 100,
           hum_centi_percent / 100,
           hum_centi_percent % 100,
           eco2_ppm,
           tvoc_ppb);
}

void print_command_packet(const char *prefix, const struct flood_packet *packet, int8_t rssi)
{
    uint8_t seq = packet->data[2];
    uint8_t cmd = packet->data[3];

    if (cmd == CMD_SLEEP) {
        printk("%s COMMAND CMD_SLEEP seq=%u rssi=%d\n", prefix, seq, rssi);
    } else if (cmd == CMD_MEASURE) {
        printk("%s COMMAND CMD_MEASURE seq=%u rssi=%d\n", prefix, seq, rssi);
    } else {
        printk("%s COMMAND UNKNOWN cmd=0x%02X seq=%u rssi=%d\n",
               prefix, cmd, seq, rssi);
    }
}

/* -------------------------------------------------------------------------- */
/* Seen caches                                                                */
/* -------------------------------------------------------------------------- */

static bool seen_sensor_contains(uint8_t origin_node_id, uint8_t seq)
{
    for (int i = 0; i < SEEN_SENSOR_CACHE_SIZE; i++) {
        if (seen_sensor_cache[i].valid &&
            seen_sensor_cache[i].origin_node_id == origin_node_id &&
            seen_sensor_cache[i].seq == seq) {
            return true;
        }
    }

    return false;
}

static void seen_sensor_add(uint8_t origin_node_id, uint8_t seq)
{
    seen_sensor_cache[seen_sensor_write_index].origin_node_id = origin_node_id;
    seen_sensor_cache[seen_sensor_write_index].seq = seq;
    seen_sensor_cache[seen_sensor_write_index].valid = true;

    seen_sensor_write_index++;

    if (seen_sensor_write_index >= SEEN_SENSOR_CACHE_SIZE) {
        seen_sensor_write_index = 0;
    }
}

bool seen_sensor_check_and_add(uint8_t origin_node_id, uint8_t seq)
{
    bool already_seen;

    k_mutex_lock(&seen_sensor_mutex, K_FOREVER);

    already_seen = seen_sensor_contains(origin_node_id, seq);

    if (!already_seen) {
        seen_sensor_add(origin_node_id, seq);
    }

    k_mutex_unlock(&seen_sensor_mutex);

    return already_seen;
}

static bool seen_command_contains(uint8_t seq, uint8_t cmd)
{
    for (int i = 0; i < SEEN_COMMAND_CACHE_SIZE; i++) {
        if (seen_command_cache[i].valid &&
            seen_command_cache[i].seq == seq &&
            seen_command_cache[i].cmd == cmd) {
            return true;
        }
    }

    return false;
}

static void seen_command_add(uint8_t seq, uint8_t cmd)
{
    seen_command_cache[seen_command_write_index].seq = seq;
    seen_command_cache[seen_command_write_index].cmd = cmd;
    seen_command_cache[seen_command_write_index].valid = true;

    seen_command_write_index++;

    if (seen_command_write_index >= SEEN_COMMAND_CACHE_SIZE) {
        seen_command_write_index = 0;
    }
}

bool seen_command_check_and_add(uint8_t seq, uint8_t cmd)
{
    bool already_seen;

    k_mutex_lock(&seen_command_mutex, K_FOREVER);

    already_seen = seen_command_contains(seq, cmd);

    if (!already_seen) {
        seen_command_add(seq, cmd);
    }

    k_mutex_unlock(&seen_command_mutex);

    return already_seen;
}

/* -------------------------------------------------------------------------- */
/* Advertising                                                                */
/* -------------------------------------------------------------------------- */

static int start_or_update_advertising(const struct bt_data *ad, size_t ad_len)
{
    int ret;

    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_running) {
        ret = bt_le_adv_update_data(ad, ad_len, NULL, 0);
        if (ret < 0) {
            printk("Advertising update failed: %d\n", ret);
        }
    } else {
        ret = bt_le_adv_start(&fast_adv_param, ad, ad_len, NULL, 0);
        if (ret < 0) {
            printk("Advertising start failed: %d\n", ret);
        } else {
            adv_running = true;
        }
    }

    k_mutex_unlock(&adv_mutex);

    return ret;
}

int advertise_sensor_packet(const struct flood_packet *packet)
{
    memcpy(current_sensor_adv_data, packet->data, SENSOR_PACKET_LEN);
    return start_or_update_advertising(sensor_ad, ARRAY_SIZE(sensor_ad));
}

int advertise_command_packet(const struct flood_packet *packet)
{
    memcpy(current_command_adv_data, packet->data, COMMAND_PACKET_LEN);
    return start_or_update_advertising(command_ad, ARRAY_SIZE(command_ad));
}

void stop_advertising_if_running(void)
{
    int ret;

    k_mutex_lock(&adv_mutex, K_FOREVER);

    if (adv_running) {
        ret = bt_le_adv_stop();
        if (ret < 0) {
            printk("Advertising stop failed: %d\n", ret);
        } else {
            adv_running = false;
        }
    }

    k_mutex_unlock(&adv_mutex);
}

/* -------------------------------------------------------------------------- */
/* Commands and scanning                                                      */
/* -------------------------------------------------------------------------- */

void handle_command(uint8_t seq, uint8_t cmd)
{
    switch (cmd) {
    case CMD_SLEEP:
        printk("Handling CMD_SLEEP seq=%u -> enter low-power mode\n", seq);
        set_node_mode(MODE_LOW_POWER);
        break;

    case CMD_MEASURE:
        printk("Handling CMD_MEASURE seq=%u -> resume normal mode\n", seq);
        set_node_mode(MODE_NORMAL);
        resume_sensors();
        break;

    default:
        printk("Ignoring unknown command 0x%02X seq=%u\n", cmd, seq);
        break;
    }
}

struct parse_result {
    bool found;
    struct flood_packet packet;
};

static bool parse_manufacturer_data(struct bt_data *data, void *user_data)
{
    struct parse_result *result = user_data;
    uint16_t company_id;

    if (data->type != BT_DATA_MANUFACTURER_DATA) {
        return true;
    }

    if (data->data_len < 2) {
        return true;
    }

    company_id = sys_get_le16(&data->data[0]);

    if (company_id == SENSOR_COMPANY_ID && data->data_len == SENSOR_PACKET_LEN) {
        result->packet.type = FLOOD_PACKET_SENSOR;
        result->packet.len = SENSOR_PACKET_LEN;
        memcpy(result->packet.data, data->data, SENSOR_PACKET_LEN);
        result->found = true;
        return false;
    }

    if (company_id == MOBILE_COMPANY_ID && data->data_len == COMMAND_PACKET_LEN) {
        result->packet.type = FLOOD_PACKET_COMMAND;
        result->packet.len = COMMAND_PACKET_LEN;
        memcpy(result->packet.data, data->data, COMMAND_PACKET_LEN);
        result->found = true;
        return false;
    }

    return true;
}

static void device_found(const bt_addr_le_t *addr,
                         int8_t rssi,
                         uint8_t type,
                         struct net_buf_simple *adv_buf)
{
    struct parse_result result = {
        .found = false,
    };

    ARG_UNUSED(addr);
    ARG_UNUSED(type);

    bt_data_parse(adv_buf, parse_manufacturer_data, &result);

    if (!result.found) {
        return;
    }

    if (result.packet.type == FLOOD_PACKET_SENSOR) {
        uint8_t origin_node_id = result.packet.data[2];
        uint8_t seq = result.packet.data[3];
        bool already_seen;

        if (origin_node_id == NODE_ID) {
            return;
        }

        already_seen = seen_sensor_check_and_add(origin_node_id, seq);

        if (already_seen) {
            return;
        }

        print_sensor_packet("RX NEW", &result.packet, rssi);

        if (get_node_mode() != MODE_NORMAL) {
            printk("LOW_POWER: received sensor packet but not relaying it\n");
            return;
        }

        if (k_msgq_put(&flood_msgq, &result.packet, K_NO_WAIT) != 0) {
            printk("Flood queue full, dropping sensor packet node_%u seq=%u\n",
                   origin_node_id, seq);
            return;
        }

        printk("Queued sensor relay: origin=node_%u seq=%u\n",
               origin_node_id, seq);
    } else if (result.packet.type == FLOOD_PACKET_COMMAND) {
        uint8_t seq = result.packet.data[2];
        uint8_t cmd = result.packet.data[3];
        bool already_seen;

        already_seen = seen_command_check_and_add(seq, cmd);

        if (already_seen) {
            return;
        }

        print_command_packet("RX NEW", &result.packet, rssi);

        if (k_msgq_put(&flood_msgq, &result.packet, K_NO_WAIT) != 0) {
            printk("Flood queue full, dropping command seq=%u cmd=0x%02X\n",
                   seq, cmd);
        } else {
            printk("Queued command relay: seq=%u cmd=0x%02X\n", seq, cmd);
        }

        handle_command(seq, cmd);
    }
}

int start_fast_scan(void)
{
    return bt_le_scan_start(&fast_scan_param, device_found);
}

int stop_scan(void)
{
    return bt_le_scan_stop();
}

/* -------------------------------------------------------------------------- */
/* Watchdog                                                                   */
/* -------------------------------------------------------------------------- */

void watchdog_thread(void *p1, void *p2, void *p3)
{
    int ret;
    struct wdt_timeout_cfg wdt_config = {
        .window = {
            .min = 0,
            .max = WATCHDOG_TIMEOUT_MS,
        },
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!device_is_ready(wdt_dev)) {
        printk("Watchdog device is not ready\n");
        return;
    }

    wdt_channel_id = wdt_install_timeout(wdt_dev, &wdt_config);
    if (wdt_channel_id < 0) {
        printk("Watchdog timeout install failed: %d\n", wdt_channel_id);
        return;
    }

    ret = wdt_setup(wdt_dev, 0);
    if (ret < 0) {
        printk("Watchdog setup failed: %d\n", ret);
        return;
    }

    printk("Watchdog started: timeout=%d ms, feed interval=%d ms\n",
           WATCHDOG_TIMEOUT_MS,
           WATCHDOG_FEED_INTERVAL_MS);

    while (1) {
        ret = wdt_feed(wdt_dev, wdt_channel_id);
        if (ret < 0) {
            printk("Watchdog feed failed: %d\n", ret);
        }

        k_sleep(K_MSEC(WATCHDOG_FEED_INTERVAL_MS));
    }
}
