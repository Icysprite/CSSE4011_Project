#include "kalman.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kalman, LOG_LEVEL_INF);

void kalman_init(struct kalman_filter *kf, float q, float r, float initial_x)
{
    // State estimate
    kf->x = initial_x;
    // error covariance, it is the uncertainty in the current state estimation.
    // It shrinks when measurements confirms state estimation
    kf->p = 1.0f;
    kf->q = q;
    kf->r = r;
}

float kalman_update(struct kalman_filter *kf, float measurement)
{
    /* Predict */
    kf->p = kf->p + kf->q;

    /* Update */

    /* Kalman gain. If gain is 1, trust measurement more */
    float k = kf->p / (kf->p + kf->r);
    kf->x = kf->x + k * (measurement - kf->x);
    kf->p = (1.0f - k) * kf->p;

    return kf->x;
}