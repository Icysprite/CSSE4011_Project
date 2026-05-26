#ifndef ENV_PACKET_H
#define ENV_PACKET_H

#include <stdint.h>

#define ENV_NODE_NAME_LEN 9

struct env_record {
    char     node_id[ENV_NODE_NAME_LEN];  
    uint32_t timestamp_ms;                 
    int16_t  temp_centi_c;                
    uint16_t hum_centi_rh;                 
    uint16_t eco2_ppm;                     
    uint16_t tvoc_ppb;                    
};

#endif /* ENV_PACKET_H */