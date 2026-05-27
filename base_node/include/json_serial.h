#ifndef JSON_SERIAL_H
#define JSON_SERIAL_H

#include "env_packet.h"

void json_serial_send(const struct env_record *rec);
void json_serial_send_kalman(void);

#endif /* JSON_SERIAL_H */