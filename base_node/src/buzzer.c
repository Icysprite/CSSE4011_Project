/*
 * Black-Valetudo - Base Node
 * PWM buzzer/speaker driver
 *
 * Warning — short chirps at 1000 Hz
 * Poor    — continuous alarm at 2000 Hz until stopped
 */

#include "buzzer.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pwm.h>

LOG_MODULE_REGISTER(buzzer, LOG_LEVEL_INF);

#define WARNING_FREQ_HZ   1000
#define POOR_FREQ_HZ      2000
#define CHIRP_DURATION_MS 200
#define CHIRP_COUNT       3

static const struct pwm_dt_spec buzzer_pwm =
    PWM_DT_SPEC_GET(DT_ALIAS(buzzer));

/* ==========================================================================
 * Buzzer thread
 * ========================================================================== */

#define BUZZER_THREAD_STACK 512
#define BUZZER_THREAD_PRIO  10

K_THREAD_STACK_DEFINE(buzzer_stack, BUZZER_THREAD_STACK);
static struct k_thread buzzer_thread_data;

typedef enum {
    BUZZER_IDLE,
    BUZZER_WARNING,
    BUZZER_POOR,
    BUZZER_STOP,
} buzzer_cmd_t;

K_MSGQ_DEFINE(buzzer_msgq, sizeof(buzzer_cmd_t), 4, 1);

static void buzzer_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    buzzer_cmd_t cmd;
    bool poor_active = false;

    while (1) {
        /* Block until a command arrives */
        k_msgq_get(&buzzer_msgq, &cmd, K_FOREVER);

        switch (cmd) {

        case BUZZER_WARNING:
            /* Short chirps — k_sleep needed for on/off timing */
            for (int i = 0; i < CHIRP_COUNT; i++) {
                pwm_set_dt(&buzzer_pwm,
                           PWM_HZ(WARNING_FREQ_HZ),
                           PWM_HZ(WARNING_FREQ_HZ) / 2);
                k_sleep(K_MSEC(CHIRP_DURATION_MS));
                pwm_set_dt(&buzzer_pwm,
                           PWM_HZ(WARNING_FREQ_HZ), 0);
                k_sleep(K_MSEC(CHIRP_DURATION_MS));
            }
            break;

        case BUZZER_POOR:
            /* Start PWM — hardware runs independently */
            poor_active = true;
            pwm_set_dt(&buzzer_pwm,
                       PWM_HZ(POOR_FREQ_HZ),
                       PWM_HZ(POOR_FREQ_HZ) / 2);

            /* Yield CPU — check msgq for stop command */
            while (poor_active) {
                buzzer_cmd_t inner_cmd;
                if (k_msgq_get(&buzzer_msgq,
                               &inner_cmd, K_NO_WAIT) == 0) {
                    if (inner_cmd == BUZZER_STOP) {
                        poor_active = false;
                    }
                }
                k_sleep(K_MSEC(100));
            }

            /* Stop PWM */
            pwm_set_dt(&buzzer_pwm, PWM_HZ(POOR_FREQ_HZ), 0);
            break;

        case BUZZER_STOP:
            /* Stop if poor alarm is not active */
            pwm_set_dt(&buzzer_pwm, PWM_HZ(POOR_FREQ_HZ), 0);
            break;

        default:
            break;
        }
    }
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

int buzzer_init(void)
{
    if (!pwm_is_ready_dt(&buzzer_pwm)) {
        LOG_ERR("Buzzer PWM not ready");
        return -ENODEV;
    }

    k_thread_create(&buzzer_thread_data, buzzer_stack,
                    BUZZER_THREAD_STACK,
                    buzzer_thread, NULL, NULL, NULL,
                    BUZZER_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&buzzer_thread_data, "buzzer");

    LOG_INF("Buzzer initialised");
    return 0;
}

void buzzer_warning(void)
{
    buzzer_cmd_t cmd = BUZZER_WARNING;
    k_msgq_put(&buzzer_msgq, &cmd, K_NO_WAIT);
}

void buzzer_poor(void)
{
    buzzer_cmd_t cmd = BUZZER_POOR;
    k_msgq_put(&buzzer_msgq, &cmd, K_NO_WAIT);
}

void buzzer_stop(void)
{
    buzzer_cmd_t cmd = BUZZER_STOP;
    k_msgq_put(&buzzer_msgq, &cmd, K_NO_WAIT);
}