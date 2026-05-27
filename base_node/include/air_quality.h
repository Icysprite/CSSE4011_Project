#ifndef AIR_QUALITY_H
#define AIR_QUALITY_H

#include <stdint.h>

typedef enum {
    AQ_GOOD,
    AQ_WARNING,
    AQ_POOR
} air_quality_t;


void air_quality_init(void);
void air_quality_update(uint16_t eco2_ppm, uint16_t tvoc_ppb);
void air_quality_reset(void);
air_quality_t air_quality_get_eco2(void);
air_quality_t air_quality_get_tvoc(void);
float air_quality_get_eco2_estimate(void);
float air_quality_get_tvoc_estimate(void);

#endif /* AIR_QUALITY_H */