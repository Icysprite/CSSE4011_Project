#ifndef NUS_CENTRAL_H
#define NUS_CENTRAL_H

#include <stdint.h>
#include <stdbool.h>


int nus_central_init(void);
int nus_send_command(uint8_t cmd);
bool nus_central_is_connected(void);
// Resets air_quality classification
void nus_central_reset_classification(void);

#endif /* NUS_CENTRAL_H */