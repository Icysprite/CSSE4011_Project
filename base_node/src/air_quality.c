/*
 * Black-Valetudo - Base Node
 * Air quality classification using Kalman filter
 */

#include "air_quality.h"
#include "kalman.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(air_quality, LOG_LEVEL_INF);

/* ==========================================================================
 * Kalman filter tuning parameters
 *
 * Q — process noise: how much we expect the true value to drift per update
 *     larger Q = trust measurements more, filter responds faster
 *
 * R — measurement noise: how noisy the sensor readings are
 *     larger R = trust measurements less, filter responds slower
 * ========================================================================== */

#define ECO2_Q          5.0f /* increased from 1.0f to 5.0f to track changes faster*/
#define ECO2_R          10.0f
#define ECO2_INITIAL    400.0f   /* typical outdoor CO2 level */

#define TVOC_Q          0.5f
#define TVOC_R          5.0f
#define TVOC_INITIAL    0.0f

#define ECO2_WARNING_PPM   1000
#define ECO2_POOR_PPM      2000

#define TVOC_WARNING_PPB   220
#define TVOC_POOR_PPB      660

/* Filter state */
static struct kalman_filter eco2_filter;
static struct kalman_filter tvoc_filter;

void air_quality_init(void)
{
    kalman_init(&eco2_filter, ECO2_Q, ECO2_R, ECO2_INITIAL);
    kalman_init(&tvoc_filter, TVOC_Q, TVOC_R, TVOC_INITIAL);

    LOG_INF("Air quality Kalman filters initialised");
}

void air_quality_update(uint16_t eco2_ppm, uint16_t tvoc_ppb)
{
    float eco2_est = kalman_update(&eco2_filter, (float)eco2_ppm);
    float tvoc_est = kalman_update(&tvoc_filter, (float)tvoc_ppb);

    LOG_INF("Kalman estimate — eCO2: %.1f ppm  TVOC: %.1f ppb",
            (double)eco2_est, (double)tvoc_est);
}

float air_quality_get_eco2_estimate(void)
{
    return eco2_filter.x;
}

float air_quality_get_tvoc_estimate(void)
{
    return tvoc_filter.x;
}

air_quality_t air_quality_get_eco2(void)
{
    float est = eco2_filter.x;

    if (est >= ECO2_POOR_PPM) {
        return AQ_POOR;
    } else if (est >= ECO2_WARNING_PPM) {
        return AQ_WARNING;
    }
    return AQ_GOOD;
}

air_quality_t air_quality_get_tvoc(void)
{
    float est = tvoc_filter.x;

    if (est >= TVOC_POOR_PPB) {
        return AQ_POOR;
    } else if (est >= TVOC_WARNING_PPB) {
        return AQ_WARNING;
    }
    return AQ_GOOD;
}

void air_quality_reset(void)
{
    kalman_init(&eco2_filter, ECO2_Q, ECO2_R, ECO2_INITIAL);
    kalman_init(&tvoc_filter, TVOC_Q, TVOC_R, TVOC_INITIAL);
    LOG_INF("Air quality filters reset");
}