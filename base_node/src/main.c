#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "nus_central.h"
#include "watchdog.h"
#include "air_quality.h"
#include "buzzer.h"
#include "node_buffer.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define CMD_MEASURE 0x01
#define CMD_SLEEP   0x02

#define WDT_FEED_STACK  512
#define WDT_FEED_PERIOD_MS 2000  

K_THREAD_STACK_DEFINE(wdt_feed_stack, WDT_FEED_STACK);
static struct k_thread wdt_feed_thread_data;

static int cmd_wake(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!nus_central_is_connected()) {
        shell_error(sh, "Not connected to mobile_node");
        return -ENOTCONN;
    }

    int err = nus_send_command(CMD_MEASURE);
    if (err) {
        shell_error(sh, "Failed to send CMD_MEASURE (err %d)", err);
        return err;
    }

    return 0;
}

static int cmd_sleep(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!nus_central_is_connected()) {
        shell_error(sh, "Not connected to mobile_node");
        return -ENOTCONN;
    }

    int err = nus_send_command(CMD_SLEEP);
    if (err) {
        shell_error(sh, "Failed to send CMD_SLEEP (err %d)", err);
        return err;
    }

    shell_print(sh, "CMD_SLEEP sent to mobile_node");
    buzzer_stop();
    air_quality_reset();
    nus_central_reset_classification();

    return 0;
}

SHELL_CMD_REGISTER(wake,  NULL, "Send CMD_MEASURE to sensor mesh", cmd_wake);
SHELL_CMD_REGISTER(sleep, NULL, "Send CMD_SLEEP to sensor mesh",   cmd_sleep);

static void wdt_feed_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        watchdog_feed();
        k_sleep(K_MSEC(WDT_FEED_PERIOD_MS));
    }
}

int main(void)
{
    watchdog_init();

    k_thread_create(&wdt_feed_thread_data, wdt_feed_stack,
                    WDT_FEED_STACK,
                    wdt_feed_thread, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&wdt_feed_thread_data, "wdt_feed");

    node_buffer_init();
    air_quality_init();
    buzzer_init();

    int err = nus_central_init();
    if (err) {
        LOG_ERR("NUS central init failed (err %d)", err);
        return err;
    }

    return 0;
}