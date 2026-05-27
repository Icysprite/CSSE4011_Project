#include "watchdog.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/watchdog.h>

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

#define WDT_TIMEOUT_MS 5000  

static const struct device *wdt;
static int wdt_channel_id;

static void wdt_callback(const struct device *dev, int channel_id)
{
    LOG_WRN("Watchdog expired — resetting");
}

int watchdog_init(void)
{
    wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));

    if (!device_is_ready(wdt)) {
        LOG_ERR("Watchdog device not ready");
        return -ENODEV;
    }

    struct wdt_timeout_cfg wdt_cfg = {
        .window = {
            .min = 0,
            .max = WDT_TIMEOUT_MS,
        },
        .callback = wdt_callback,
        .flags    = WDT_FLAG_RESET_SOC,
    };

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_cfg);
    if (wdt_channel_id < 0) {
        LOG_ERR("Watchdog install timeout failed (err %d)", wdt_channel_id);
        return wdt_channel_id;
    }

    int err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err) {
        LOG_ERR("Watchdog setup failed (err %d)", err);
        return err;
    }

    LOG_INF("Watchdog initialised (%d ms timeout)", WDT_TIMEOUT_MS);
    return 0;
}

void watchdog_feed(void)
{
    wdt_feed(wdt, wdt_channel_id);
}