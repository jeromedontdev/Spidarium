#include "rtc.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdlib.h>
#include <sys/time.h>

static const char *TAG = "spidarium.rtc";
static i2c_master_dev_handle_t s_device;

static int unbcd(uint8_t value)
{
    return (value & 15) <= 9 && (value >> 4) <= 9 ?
        (value >> 4) * 10 + (value & 15) : -1;
}
static uint8_t bcd(int value) { return (value / 10 << 4) | (value % 10); }

esp_err_t pet_rtc_read(time_t *utc)
{
    if (!s_device || !utc) return ESP_ERR_INVALID_STATE;
    uint8_t reg = 0, control;
    esp_err_t err = i2c_master_transmit_receive(s_device, &reg, 1, &control, 1, 100);
    if (err != ESP_OK) return err;
    if (control & 0x22) return ESP_ERR_INVALID_STATE; /* Stopped or 12-hour mode. */
    reg = 4;
    uint8_t data[7];
    err = i2c_master_transmit_receive(s_device, &reg, 1, data, sizeof data, 100);
    if (err != ESP_OK) return err;
    if (data[0] & 0x80) return ESP_ERR_INVALID_STATE; /* Oscillator-stop flag. */
    struct tm value = {
        .tm_sec = unbcd(data[0] & 0x7f), .tm_min = unbcd(data[1] & 0x7f),
        .tm_hour = unbcd(data[2] & 0x3f), .tm_mday = unbcd(data[3] & 0x3f),
        .tm_mon = unbcd(data[5] & 0x1f) - 1, .tm_year = unbcd(data[6]) + 100,
        .tm_isdst = 0,
    };
    if (value.tm_sec < 0 || value.tm_sec > 59 || value.tm_min < 0 || value.tm_min > 59 ||
        value.tm_hour < 0 || value.tm_hour > 23 || value.tm_mday < 1 || value.tm_mday > 31 ||
        value.tm_mon < 0 || value.tm_mon > 11 || value.tm_year < 100 ||
        value.tm_year > 199) return ESP_ERR_INVALID_RESPONSE;
    struct tm requested = value;
    time_t epoch = mktime(&value);
    if (epoch == (time_t)-1 || value.tm_mday != requested.tm_mday ||
        value.tm_mon != requested.tm_mon || value.tm_year != requested.tm_year)
        return ESP_ERR_INVALID_RESPONSE;
    *utc = epoch;
    return ESP_OK;
}

esp_err_t pet_rtc_write(time_t utc)
{
    if (!s_device) return ESP_ERR_INVALID_STATE;
    struct tm value;
    if (!gmtime_r(&utc, &value) || value.tm_year < 100 || value.tm_year > 199)
        return ESP_ERR_INVALID_ARG;
    uint8_t reg = 0, control;
    esp_err_t err = i2c_master_transmit_receive(s_device, &reg, 1, &control, 1, 100);
    if (err != ESP_OK) return err;
    /* Stop while writing and select 24-hour mode; preserve unrelated bits. */
    uint8_t stop[] = {0, (control & ~0x02) | 0x20};
    err = i2c_master_transmit(s_device, stop, sizeof stop, 100);
    if (err != ESP_OK) return err;
    uint8_t data[] = {4, bcd(value.tm_sec), bcd(value.tm_min), bcd(value.tm_hour),
                      bcd(value.tm_mday), bcd(value.tm_wday), bcd(value.tm_mon + 1),
                      bcd(value.tm_year - 100)};
    err = i2c_master_transmit(s_device, data, sizeof data, 100);
    uint8_t start[] = {0, control & ~0x22};
    esp_err_t restart = i2c_master_transmit(s_device, start, sizeof start, 100);
    if (err != ESP_OK) return err;
    if (restart != ESP_OK) return restart;
    time_t verified;
    err = pet_rtc_read(&verified);
    if (err != ESP_OK) return err;
    if (verified < utc || verified > utc + 1) return ESP_ERR_INVALID_RESPONSE;
    ESP_LOGI(TAG, "RTC updated (UTC)");
    return ESP_OK;
}

esp_err_t pet_rtc_init(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    esp_err_t err = bsp_i2c_init();
    if (err == ESP_OK) {
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x51,
            .scl_speed_hz = 100000,
        };
        err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &s_device);
    }
    time_t utc;
    if (err == ESP_OK) err = pet_rtc_read(&utc);
    if (err == ESP_OK) {
        struct timeval value = { .tv_sec = utc };
        if (settimeofday(&value, NULL) != 0) err = ESP_FAIL;
    }
    if (err == ESP_OK) ESP_LOGI(TAG, "system clock restored from RTC");
    else ESP_LOGW(TAG, "RTC not valid: %s; time sync required", esp_err_to_name(err));
    return err;
}
