#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include "nus_central.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define CMD_MEASURE 0x01
#define CMD_SLEEP   0x02

static bool is_awake = false;

static int cmd_wake(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!nus_central_is_connected()) {
        shell_error(sh, "Not connected to mobile_node");
        return -ENOTCONN;
    }

    if (is_awake) {
        shell_print(sh, "Mesh is already awake");
        return 0;
    }

    int err = nus_send_command(CMD_MEASURE);
    if (err) {
        shell_error(sh, "Failed to send CMD_MEASURE (err %d)", err);
        return err;
    }

    is_awake = true;
    shell_print(sh, "CMD_MEASURE sent to mobile_node");
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

    if (!is_awake) {
        shell_print(sh, "Mesh is already asleep");
        return 0;
    }

    int err = nus_send_command(CMD_SLEEP);
    if (err) {
        shell_error(sh, "Failed to send CMD_SLEEP (err %d)", err);
        return err;
    }

    is_awake = false;
    shell_print(sh, "CMD_SLEEP sent to mobile_node");
    return 0;
}

SHELL_CMD_REGISTER(wake,  NULL, "Send CMD_MEASURE to sensor mesh", cmd_wake);
SHELL_CMD_REGISTER(sleep, NULL, "Send CMD_SLEEP to sensor mesh",   cmd_sleep);

int main(void)
{
    int err = nus_central_init();
    if (err) {
        LOG_ERR("NUS central init failed (err %d)", err);
        return err;
    }

    return 0;
}