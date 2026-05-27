#ifndef NODE_HELPERS_H
#define NODE_HELPERS_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>


#define NODE_ID                         3
#define DEVICE_NAME                     "node_3"

//Company ID to distinguish sensors packet from sensor and mobile command
#define SENSOR_COMPANY_ID               0xFFFF
#define MOBILE_COMPANY_ID               0x0001

//command from mobile node
#define CMD_MEASURE                     0x01
#define CMD_SLEEP                       0x02

//sampling, advertising and sleep intervals
#define SENSOR_SAMPLE_INTERVAL_MS       5000
#define FLOOD_ADVERTISE_TIME_MS         300
#define LOW_POWER_SLEEP_MS              10000
#define LOW_POWER_LISTEN_MS             3000

#define SENSOR_THREAD_STACK_SIZE        2048
#define SCAN_THREAD_STACK_SIZE          2048
#define FLOOD_THREAD_STACK_SIZE         2048

#define SENSOR_THREAD_PRIORITY          5
#define SCAN_THREAD_PRIORITY            5
#define FLOOD_THREAD_PRIORITY           5

//watchdog parameters
#define WATCHDOG_THREAD_STACK_SIZE      1024
#define WATCHDOG_THREAD_PRIORITY        7
#define WATCHDOG_TIMEOUT_MS             10000
#define WATCHDOG_FEED_INTERVAL_MS       2000

//prevent overflooding the messages
#define SEEN_SENSOR_CACHE_SIZE          32
#define SEEN_COMMAND_CACHE_SIZE         32
#define FLOOD_QUEUE_SIZE                12

/* Sensor packet:
 * Byte 0-1   Company ID 0xFFFF
 * Byte 2     Origin node ID
 * Byte 3     Origin sequence
 * Byte 4-5   Temperature, centi-degree C
 * Byte 6-7   Humidity, centi-%RH
 * Byte 8-9   eCO2, ppm
 * Byte 10-11 TVOC, ppb
 */
#define SENSOR_PACKET_LEN               12

/* Mobile command packet:
 * Byte 0-1 Company ID 0x0001
 * Byte 2   Command sequence
 * Byte 3   Command
 */
#define COMMAND_PACKET_LEN              4

enum node_mode {
    MODE_NORMAL,
    MODE_LOW_POWER,
};

enum flood_packet_type {
    FLOOD_PACKET_SENSOR,
    FLOOD_PACKET_COMMAND,
};

struct flood_packet {
    enum flood_packet_type type;
    uint8_t len;
    uint8_t data[SENSOR_PACKET_LEN];
};

struct seen_sensor_entry {
    uint8_t origin_node_id;
    uint8_t seq;
    bool valid;
};

struct seen_command_entry {
    uint8_t seq;
    uint8_t cmd;
    bool valid;
};

// Shared Zephyr objects defined in main.c.
extern struct k_sem bt_ready_sem;
extern struct k_msgq flood_msgq;

//Node mode
enum node_mode get_node_mode(void);
void set_node_mode(enum node_mode mode);

//sensor related functions
bool sensors_are_ready(void);
void suspend_sensors(void);
void resume_sensors(void);
int read_hts221(int16_t *temp_centi_c, uint16_t *hum_centi_percent);
int read_ccs811(uint16_t *eco2_ppm, uint16_t *tvoc_ppb);

//encode data into packet to be advertised
void encode_sensor_packet(struct flood_packet *packet,
                          uint8_t origin_node_id,
                          uint8_t seq,
                          int16_t temp_centi_c,
                          uint16_t hum_centi_percent,
                          uint16_t eco2_ppm,
                          uint16_t tvoc_ppb);
void encode_command_packet(struct flood_packet *packet, uint8_t seq, uint8_t cmd);
void print_sensor_packet(const char *prefix, const struct flood_packet *packet, int8_t rssi);
void print_command_packet(const char *prefix, const struct flood_packet *packet, int8_t rssi);

//add to store of seen packets, return true if already seen
bool seen_sensor_check_and_add(uint8_t origin_node_id, uint8_t seq);
bool seen_command_check_and_add(uint8_t seq, uint8_t cmd);

/* BLE advertising and scanning. */
int advertise_sensor_packet(const struct flood_packet *packet);
int advertise_command_packet(const struct flood_packet *packet);

//sleep and wake related functions
void stop_advertising_if_running(void);
int start_fast_scan(void);
int stop_scan(void);

//watchdog
void handle_command(uint8_t seq, uint8_t cmd);
void watchdog_thread(void *p1, void *p2, void *p3);

#endif
