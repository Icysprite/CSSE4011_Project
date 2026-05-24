#ifndef ENV_PACKET_H
#define ENV_PACKET_H

#include <stdint.h>

#define ENV_NODE_NAME_LEN 8

/*
 * Environmental record sent over NUS from mobile node to base node.
 */
struct env_record {
    char     node_id[ENV_NODE_NAME_LEN];  /* e.g. "node_1" to "node_5" */
    uint32_t timestamp_ms;                 /* k_uptime_get() on mobile node */
    int16_t  temp_centi_c;                 /* centi-degrees C */
    uint16_t hum_centi_rh;                 /* centi-%RH */
    uint16_t eco2_ppm;                     /* ppm */
    uint16_t tvoc_ppb;                     /* ppb */
};

#endif /* ENV_PACKET_H */