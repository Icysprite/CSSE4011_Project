/*
 * Black-Valetudo - Base Node
 * JSON serial output
 *
 * Encodes env_record as JSON and sends over USB serial to PC.
 */

#include "json_serial.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>

LOG_MODULE_REGISTER(json_serial, LOG_LEVEL_INF);

struct env_record_json {
    const char *node_id;
    uint32_t    timestamp_ms;
    int32_t     temperature;
    uint32_t    humidity;
    uint32_t    eco2;
    uint32_t    tvoc;
};

static const struct json_obj_descr env_record_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct env_record_json, node_id,      JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct env_record_json, timestamp_ms, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct env_record_json, temperature,  JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct env_record_json, humidity,     JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct env_record_json, eco2,         JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct env_record_json, tvoc,         JSON_TOK_NUMBER),
};

void json_serial_send(const struct env_record *rec)
{
    struct env_record_json json_rec = {
        .node_id      = rec->node_id,
        .timestamp_ms = rec->timestamp_ms,
        .temperature  = rec->temp_centi_c,
        .humidity     = rec->hum_centi_rh,
        .eco2         = rec->eco2_ppm,
        .tvoc         = rec->tvoc_ppb,
    };

    char json_buf[256];
    int ret = json_obj_encode_buf(env_record_descr,
                                  ARRAY_SIZE(env_record_descr),
                                  &json_rec,
                                  json_buf,
                                  sizeof(json_buf));
    if (ret < 0) {
        LOG_ERR("JSON encode failed (err %d)", ret);
        return;
    }

    printk("%s\n", json_buf);
}