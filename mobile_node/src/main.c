#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "nus_peripheral.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err = nus_peripheral_init();
    if (err) {
        LOG_ERR("NUS peripheral init failed (err %d)", err);
        return err;
    }

    sniffer_start();


    return 0;
}