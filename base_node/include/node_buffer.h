#ifndef NODE_BUFFER_H
#define NODE_BUFFER_H

#include "env_packet.h"
#include <stdbool.h>

#define NODE_BUFFER_MAX 8

void node_buffer_init(void);
void node_buffer_add(const struct env_record *rec);
int node_buffer_count(void);
const struct env_record *node_buffer_get(int index);
void node_buffer_clear(void);

#endif /* NODE_BUFFER_H */