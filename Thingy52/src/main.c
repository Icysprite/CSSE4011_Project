#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <limits.h>
#include "node_helpers.h"

K_SEM_DEFINE(bt_ready_sem, 0, 3);
K_MSGQ_DEFINE(flood_msgq, sizeof(struct flood_packet), FLOOD_QUEUE_SIZE, 4);

static void sensor_thread(void *p1, void *p2, void *p3)
{
    uint8_t seq = 0;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_sem_take(&bt_ready_sem, K_FOREVER);

    printk("Sensor thread started\n");
    printk("Waiting for CCS811 warm-up...\n");
    k_sleep(K_SECONDS(5));

    while (1) {
        struct flood_packet packet;

        int16_t temp_centi_c = 0;
        uint16_t hum_centi_percent = 0;
        uint16_t eco2_ppm = 0;
        uint16_t tvoc_ppb = 0;

        int hts_ret;
        int ccs_ret;

        if (get_node_mode() != MODE_NORMAL) {
            k_sleep(K_MSEC(500));
            continue;
        }

        resume_sensors();

        hts_ret = read_hts221(&temp_centi_c, &hum_centi_percent);
        ccs_ret = read_ccs811(&eco2_ppm, &tvoc_ppb);

        if (hts_ret != 0) {
            temp_centi_c = INT16_MIN;
            hum_centi_percent = UINT16_MAX;
        }

        if (ccs_ret != 0) {
            eco2_ppm = UINT16_MAX;
            tvoc_ppb = UINT16_MAX;
        }

        seq++;

        encode_sensor_packet(&packet,
                             NODE_ID,
                             seq,
                             temp_centi_c,
                             hum_centi_percent,
                             eco2_ppm,
                             tvoc_ppb);

        seen_sensor_check_and_add(NODE_ID, seq);

        if (k_msgq_put(&flood_msgq, &packet, K_NO_WAIT) != 0) {
            printk("Flood queue full, dropping own sensor packet seq=%u\n", seq);
        } else {
            print_sensor_packet("OWN NEW", &packet, 0);
        }

        k_sleep(K_MSEC(SENSOR_SAMPLE_INTERVAL_MS));
    }
}

static void flood_thread(void *p1, void *p2, void *p3)
{
    struct flood_packet packet;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_sem_take(&bt_ready_sem, K_FOREVER);

    printk("Flood advertising thread started as %s\n", DEVICE_NAME);

    while (1) {
        if (k_msgq_get(&flood_msgq, &packet, K_MSEC(500)) != 0) {
            continue;
        }

        if (packet.type == FLOOD_PACKET_SENSOR) {
            if (get_node_mode() != MODE_NORMAL) {
                printk("LOW_POWER: dropping queued sensor advertisement\n");
                continue;
            }

            if (packet.data[2] == NODE_ID) {
                printk("TX own sensor: node_%u seq=%u\n",
                       packet.data[2], packet.data[3]);
            } else {
                printk("TX relay sensor: origin=node_%u seq=%u\n",
                       packet.data[2], packet.data[3]);
            }

            advertise_sensor_packet(&packet);
            k_sleep(K_MSEC(FLOOD_ADVERTISE_TIME_MS));
        } else if (packet.type == FLOOD_PACKET_COMMAND) {
            printk("TX relay command: seq=%u cmd=0x%02X\n",
                   packet.data[2], packet.data[3]);

            advertise_command_packet(&packet);
            k_sleep(K_MSEC(FLOOD_ADVERTISE_TIME_MS));
        }
    }
}

static void scan_thread(void *p1, void *p2, void *p3)
{
    int ret;
    bool scan_running = false;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_sem_take(&bt_ready_sem, K_FOREVER);

    printk("Scan thread started\n");

    while (1) {
        if (get_node_mode() == MODE_NORMAL) {
            if (!scan_running) {
                ret = start_fast_scan();
                if (ret < 0) {
                    printk("BLE scan start failed: %d\n", ret);
                } else {
                    scan_running = true;
                    printk("NORMAL: scanning started\n");
                }
            }

            k_sleep(K_SECONDS(1));
        } else {
            if (scan_running) {
                ret = stop_scan();
                if (ret < 0) {
                    printk("LOW_POWER: scan stop failed: %d\n", ret);
                } else {
                    scan_running = false;
                    printk("LOW_POWER: scanning stopped\n");
                }
            }

            stop_advertising_if_running();
            suspend_sensors();

            printk("LOW_POWER: sleeping for %d ms\n", LOW_POWER_SLEEP_MS);
            k_sleep(K_MSEC(LOW_POWER_SLEEP_MS));

            if (get_node_mode() == MODE_NORMAL) {
                resume_sensors();
                continue;
            }

            printk("LOW_POWER: listening for wake command for %d ms\n",
                   LOW_POWER_LISTEN_MS);

            ret = start_fast_scan();
            if (ret < 0) {
                printk("LOW_POWER: scan start failed: %d\n", ret);
            } else {
                scan_running = true;
            }

            k_sleep(K_MSEC(LOW_POWER_LISTEN_MS));

            if (get_node_mode() == MODE_LOW_POWER && scan_running) {
                ret = stop_scan();
                if (ret < 0) {
                    printk("LOW_POWER: scan stop failed: %d\n", ret);
                } else {
                    scan_running = false;
                }
            }
        }
    }
}

K_THREAD_DEFINE(sensor_thread_id,
                SENSOR_THREAD_STACK_SIZE,
                sensor_thread,
                NULL, NULL, NULL,
                SENSOR_THREAD_PRIORITY,
                0,
                0);

K_THREAD_DEFINE(scan_thread_id,
                SCAN_THREAD_STACK_SIZE,
                scan_thread,
                NULL, NULL, NULL,
                SCAN_THREAD_PRIORITY,
                0,
                0);

K_THREAD_DEFINE(flood_thread_id,
                FLOOD_THREAD_STACK_SIZE,
                flood_thread,
                NULL, NULL, NULL,
                FLOOD_THREAD_PRIORITY,
                0,
                0);

K_THREAD_DEFINE(watchdog_thread_id,
                WATCHDOG_THREAD_STACK_SIZE,
                watchdog_thread,
                NULL, NULL, NULL,
                WATCHDOG_THREAD_PRIORITY,
                0,
                0);

int main(void)
{
    int ret;

    printk("\nThingy:52 BLE flooding node with command sleep/wake started\n");
    printk("This device is node_%u\n", NODE_ID);

    if (!sensors_are_ready()) {
        return 0;
    }

    ret = bt_enable(NULL);
    if (ret < 0) {
        printk("Bluetooth init failed: %d\n", ret);
        return 0;
    }

    printk("Bluetooth initialized\n");

    k_sem_give(&bt_ready_sem);
    k_sem_give(&bt_ready_sem);
    k_sem_give(&bt_ready_sem);

    return 0;
}
