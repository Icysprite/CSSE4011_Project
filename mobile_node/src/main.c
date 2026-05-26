#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "nus_peripheral.h"
#include "watchdog.h"
#include "ble.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define WDT_FEED_STACK     512
#define WDT_FEED_PERIOD_MS 15000

K_THREAD_STACK_DEFINE(wdt_feed_stack, WDT_FEED_STACK);
static struct k_thread wdt_feed_thread_data;

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

    int err = nus_peripheral_init();
    if (err) {
        LOG_ERR("NUS peripheral init failed (err %d)", err);
        return err;
    }

    sniffer_start();


    return 0;
}