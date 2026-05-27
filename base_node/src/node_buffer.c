#include "node_buffer.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(node_buffer, LOG_LEVEL_INF);

static struct env_record buffer[NODE_BUFFER_MAX];
static int count = 0;
static struct k_mutex buffer_mutex;

void node_buffer_init(void)
{
    k_mutex_init(&buffer_mutex);
    memset(buffer, 0, sizeof(buffer));
    count = 0;
}

void node_buffer_add(const struct env_record *rec)
{
    k_mutex_lock(&buffer_mutex, K_FOREVER);

    /* Check if node already exists — overwrite the existing entry if so */
    for (int i = 0; i < count; i++) {
        if (memcmp(buffer[i].node_id, rec->node_id,
                   ENV_NODE_NAME_LEN) == 0) {
            buffer[i] = *rec;
            k_mutex_unlock(&buffer_mutex);
            return;
        }
    }

    /* New node — add to buffer */
    if (count < NODE_BUFFER_MAX) {
        buffer[count] = *rec;
        count++;
    } else {
        LOG_WRN("Node buffer full, dropping record from %s", rec->node_id);
    }

    k_mutex_unlock(&buffer_mutex);
}

int node_buffer_count(void)
{
    return count;
}

const struct env_record *node_buffer_get(int index)
{
    if (index < 0 || index >= count) {
        return NULL;
    }
    return &buffer[index];
}

void node_buffer_clear(void)
{
    k_mutex_lock(&buffer_mutex, K_FOREVER);
    memset(buffer, 0, sizeof(buffer));
    count = 0;
    k_mutex_unlock(&buffer_mutex);
}