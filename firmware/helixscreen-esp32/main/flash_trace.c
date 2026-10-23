// SPDX-License-Identifier: GPL-3.0-or-later
// DIAG (not for landing): logs every partition erase and a per-burst summary of
// partition writes, with the time each kept the flash busy.
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

static const char* TAG = "flashtrace";

esp_err_t __real_esp_partition_erase_range(const esp_partition_t* p, size_t off, size_t size);
esp_err_t __real_esp_partition_write(const esp_partition_t* p, size_t off, const void* src,
                                     size_t size);

static uint32_t s_writes, s_write_bytes;
static int64_t s_write_us, s_write_max_us, s_last_write_us;

static void flush_writes(void) {
    if (s_writes) {
        ESP_LOGW(TAG, "write burst: %lu calls %lu B total %lldus max %lldus",
                 (unsigned long)s_writes, (unsigned long)s_write_bytes, s_write_us, s_write_max_us);
        s_writes = s_write_bytes = 0;
        s_write_us = s_write_max_us = 0;
    }
}

esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t* p, size_t off, size_t size) {
    flush_writes();
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = __real_esp_partition_erase_range(p, off, size);
    int64_t dt = esp_timer_get_time() - t0;
    ESP_LOGW(TAG, "erase %s off=0x%x size=%u %lldus", p->label, (unsigned)off, (unsigned)size, dt);
    return err;
}

esp_err_t __wrap_esp_partition_write(const esp_partition_t* p, size_t off, const void* src,
                                     size_t size) {
    int64_t t0 = esp_timer_get_time();
    if (s_writes && t0 - s_last_write_us > 200000) {
        flush_writes();
    }
    esp_err_t err = __real_esp_partition_write(p, off, src, size);
    int64_t dt = esp_timer_get_time() - t0;
    s_writes++;
    s_write_bytes += size;
    s_write_us += dt;
    if (dt > s_write_max_us) {
        s_write_max_us = dt;
    }
    s_last_write_us = esp_timer_get_time();
    return err;
}
