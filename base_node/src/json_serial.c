#include "json_serial.h"
#include "air_quality.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>

LOG_MODULE_REGISTER(json_serial, LOG_LEVEL_INF);

struct node_record_json {
    const char *type;
    const char *node_id;
    uint32_t    timestamp_ms;
    int32_t     temperature;
    uint32_t    humidity;
    uint32_t    eco2;
    uint32_t    tvoc;
};

static const struct json_obj_descr node_record_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct node_record_json, type,         JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, node_id,      JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, timestamp_ms, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, temperature,  JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, humidity,     JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, eco2,         JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct node_record_json, tvoc,         JSON_TOK_NUMBER),
};

struct kalman_json {
    const char *type;
    uint32_t    timestamp_ms;
    int32_t     eco2_estimate;
    int32_t     tvoc_estimate;
    const char *eco2_class;
    const char *tvoc_class;
};

static const struct json_obj_descr kalman_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct kalman_json, type,          JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct kalman_json, timestamp_ms,  JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct kalman_json, eco2_estimate, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct kalman_json, tvoc_estimate, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct kalman_json, eco2_class,    JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct kalman_json, tvoc_class,    JSON_TOK_STRING),
};

static const char *class_to_str(air_quality_t cls)
{
    switch (cls) {
    case AQ_POOR:    return "poor";
    case AQ_WARNING: return "warning";
    default:         return "good";
    }
}

void json_serial_send(const struct env_record *rec)
{
    struct node_record_json json_rec = {
        .type         = "node",
        .node_id      = rec->node_id,
        .timestamp_ms = rec->timestamp_ms,
        .temperature  = rec->temp_centi_c,
        .humidity     = rec->hum_centi_rh,
        .eco2         = rec->eco2_ppm,
        .tvoc         = rec->tvoc_ppb,
    };

    char buf[256];
    int ret = json_obj_encode_buf(node_record_descr,
                                  ARRAY_SIZE(node_record_descr),
                                  &json_rec, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Node JSON encode failed (err %d)", ret);
        return;
    }

    printk("%s\n", buf);
}

void json_serial_send_kalman(void)
{
    struct kalman_json json_kal = {
        .type          = "kalman",
        .timestamp_ms  = (uint32_t)k_uptime_get(),
        .eco2_estimate = (int32_t)air_quality_get_eco2_estimate(),
        .tvoc_estimate = (int32_t)air_quality_get_tvoc_estimate(),
        .eco2_class    = class_to_str(air_quality_get_eco2()),
        .tvoc_class    = class_to_str(air_quality_get_tvoc()),
    };

    char buf[256];
    int ret = json_obj_encode_buf(kalman_descr,
                                  ARRAY_SIZE(kalman_descr),
                                  &json_kal, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Kalman JSON encode failed (err %d)", ret);
        return;
    }

    printk("%s\n", buf);
}