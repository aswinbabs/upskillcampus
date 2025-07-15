//RTCMANAGER.CPP

#include "RTCMANAGER.HPP"
#include "MQTTCLIENT.HPP"
#include "LIGHTCONTROLLER.HPP"
#include "esp_sntp.h"
#include "esp_log.h"
#include <math.h>
#include <cstdio>


static const char *TAG = "RTCManager";

RTCManager* RTCManager::instance = nullptr;

RTCManager::RTCManager() :
    i2c_initialized(false),
    rtc_monitor_task_handle(nullptr),
    last_time_sent(0),
    last_date_sent(0),
    i2c_mutex(xSemaphoreCreateMutex()),  // ✅ initialize mutex
    schedule_enabled(false)
{
    // Initialize schedule to 00:00
    memset(&schedule_start_time, 0, sizeof(schedule_start_time));
    memset(&schedule_end_time, 0, sizeof(schedule_end_time));
    ESP_LOGI(TAG, "⏰ Schedule times initialized to 00:00");

}


RTCManager::~RTCManager() {
    stopMonitoring();
    if (i2c_initialized)
        i2c_driver_delete(I2C_MASTER_NUM);
    if (i2c_mutex)
        vSemaphoreDelete(i2c_mutex);  // ✅ delete mutex
}

RTCManager& RTCManager::getInstance() {
    if (!instance)
        instance = new RTCManager();
    return *instance;
}

esp_err_t RTCManager::i2c_master_init() {
    if (i2c_initialized) return ESP_OK;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { .clk_speed = I2C_MASTER_FREQ_HZ },
        .clk_flags = 0
    };

    if (i2c_param_config(I2C_MASTER_NUM, &conf) != ESP_OK) return ESP_FAIL;
    if (i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0) == ESP_OK)
        i2c_initialized = true;
    return i2c_initialized ? ESP_OK : ESP_FAIL;
}

esp_err_t RTCManager::ds3231_write_reg(uint8_t reg, uint8_t data) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2c_mutex);
    return ret;
}

esp_err_t RTCManager::ds3231_read_reg(uint8_t reg, uint8_t* data) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2c_mutex);
    return ret;
}

esp_err_t RTCManager::ds3231_read_time_regs(uint8_t* time_data) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, DS3231_REG_SECONDS, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_READ, true);

    for (int i = 0; i < 6; ++i)
        i2c_master_read_byte(cmd, &time_data[i], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &time_data[6], I2C_MASTER_NACK);

    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    xSemaphoreGive(i2c_mutex);
    return ret;
}

uint8_t RTCManager::bcd_to_dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t RTCManager::dec_to_bcd(uint8_t dec) {
    if (dec > 99) return 0;  // ✅ Prevent overflow
    return ((dec / 10) << 4) + (dec % 10);
}

esp_err_t RTCManager::initialize() {
    if (i2c_master_init() != ESP_OK) return ESP_FAIL;

    uint8_t dummy;
    if (ds3231_read_reg(DS3231_REG_SECONDS, &dummy) != ESP_OK) return ESP_FAIL;  // ✅ Fixed memory leak

    if (ds3231_write_reg(DS3231_REG_CONTROL, 0x1C) != ESP_OK) return ESP_FAIL;
    if (ds3231_write_reg(DS3231_REG_STATUS, 0x00) != ESP_OK) return ESP_FAIL;

    setenv("TZ", "IST-5:30", 1);  // 💡 Consider making this configurable
    tzset();

    // ✅ Set system time from RTC during boot
    struct tm rtc_time = getCurrentTime();
    time_t t = mktime(&rtc_time);
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, nullptr);
    ESP_LOGI(TAG, "🕒 System time synced from RTC on boot");

    return ESP_OK;
}

esp_err_t RTCManager::setDateTime(const struct tm* timeinfo) {
    if (!timeinfo) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ESP_OK;
    err |= ds3231_write_reg(DS3231_REG_SECONDS, dec_to_bcd(timeinfo->tm_sec));
    err |= ds3231_write_reg(DS3231_REG_MINUTES, dec_to_bcd(timeinfo->tm_min));
    err |= ds3231_write_reg(DS3231_REG_HOURS, dec_to_bcd(timeinfo->tm_hour));
    err |= ds3231_write_reg(DS3231_REG_DAY, dec_to_bcd(timeinfo->tm_wday + 1));
    err |= ds3231_write_reg(DS3231_REG_DATE, dec_to_bcd(timeinfo->tm_mday));
    err |= ds3231_write_reg(DS3231_REG_MONTH, dec_to_bcd(timeinfo->tm_mon + 1));
    err |= ds3231_write_reg(DS3231_REG_YEAR, dec_to_bcd(timeinfo->tm_year - 100));

    if (err == ESP_OK) {
        // ✅ Set system time as well
        struct timeval tv;
        tv.tv_sec = mktime((struct tm*)timeinfo);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        ESP_LOGI(TAG, "🕒 System time set from RTCManager::setDateTime()");
    }

    return err;
}


struct tm RTCManager::getCurrentTime() {
    struct tm timeinfo = {};
    uint8_t data[7];
    if (ds3231_read_time_regs(data) != ESP_OK) return timeinfo;

    timeinfo.tm_sec = bcd_to_dec(data[0] & 0x7F);
    timeinfo.tm_min = bcd_to_dec(data[1]);
    timeinfo.tm_hour = bcd_to_dec(data[2] & 0x3F);
    timeinfo.tm_wday = bcd_to_dec(data[3]) - 1;
    timeinfo.tm_mday = bcd_to_dec(data[4]);
    timeinfo.tm_mon = bcd_to_dec(data[5] & 0x1F) - 1;
    timeinfo.tm_year = bcd_to_dec(data[6]) + 100;

    return timeinfo;
}

esp_err_t RTCManager::syncWithNTP() {
    ESP_LOGI("RTCManager", "🔄 Starting SNTP sync...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_init();

    int retry = 0;
    const int max_retries = 10;
    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ++retry < max_retries) {
        ESP_LOGI("RTCManager", "⏳ Waiting for SNTP sync... (%d/%d)", retry, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGE("RTCManager", "❌ SNTP sync failed after %d attempts", retry);
        esp_sntp_stop();
        return ESP_FAIL;
    }

    // Fetch time after sync
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", &timeinfo);
    ESP_LOGI("RTCManager", "✅ SNTP time fetched: %s", time_buf);

    esp_sntp_stop();

    // Now apply to RTC and system
    esp_err_t err = setDateTime(&timeinfo);
    if (err == ESP_OK) resetMonitorTimestamps();
    return err;
}

float RTCManager::getTemperature() {
    uint8_t msb, lsb;
    if (ds3231_read_reg(DS3231_REG_TEMP_MSB, &msb) != ESP_OK ||
        ds3231_read_reg(DS3231_REG_TEMP_LSB, &lsb) != ESP_OK)
        return -999.0f;
    return msb + ((lsb >> 6) * 0.25f);
}

void RTCManager::rtc_monitor_task(void* param) {
    RTCManager* rtc = static_cast<RTCManager*>(param);
    float last_temp = -999.0f;
    int last_min = -1;
    int last_day = -1;

    static bool isLightOnFromSchedule = false;

    while (true) {
        float temp = rtc->getTemperature();
        if (fabs(temp - last_temp) >= 0.1f) {
            last_temp = temp;
            MQTTClient::getInstance().publishTemperature(temp);
        }

        struct tm now = rtc->getCurrentTime();

        if (now.tm_sec == 0 && now.tm_min != last_min) {
            char t[16];
            strftime(t, sizeof(t), "%H:%M", &now);
            MQTTClient::getInstance().publishTime(t);
            last_min = now.tm_min;
        }

        if (now.tm_hour == 0 && now.tm_min == 0 && now.tm_sec == 0 && now.tm_mday != last_day) {
            char d[16];
            strftime(d, sizeof(d), "%d/%m/%Y", &now);
            MQTTClient::getInstance().publishDate(d);
            last_day = now.tm_mday;
        }

        // ✅ Schedule logic without enum
        if (rtc->isScheduleEnabled()) {
            struct tm start, end;
            rtc->getSchedule(start, end);

            int now_minutes = now.tm_hour * 60 + now.tm_min;
            int start_minutes = start.tm_hour * 60 + start.tm_min;
            int end_minutes = end.tm_hour * 60 + end.tm_min;

            bool isInSchedule = (start_minutes <= end_minutes)
                                 ? (now_minutes >= start_minutes && now_minutes < end_minutes)
                                 : (now_minutes >= start_minutes || now_minutes < end_minutes);  // overnight

            bool lightState = LightController::getInstance().getState();

            if (isInSchedule) {
                if (!lightState) {
                    //LightController::getInstance().turnOn();
                    // Wherever RTC is calling:
                    if (LightController::getInstance().requestTurnOn(LightController::RTC) == ESP_OK) {
                        ESP_LOGI(TAG, "🟢 Light turned ON (scheduled)");
                    } else {
                        ESP_LOGW(TAG, "❌ RTC schedule ON ignored (lower priority)");
                    }

                    isLightOnFromSchedule = true;
                   // ESP_LOGI(TAG, "🟢 Light turned ON (scheduled)");
                }
            } else {
                if (lightState && isLightOnFromSchedule) {
                    //LightController::getInstance().turnOff();
                    if (LightController::getInstance().requestTurnOff(LightController::RTC) == ESP_OK) {
                        ESP_LOGI(TAG, "🔴 Light turned OFF (scheduled)");
                    } else {
                        ESP_LOGW(TAG, "❌ Scheduled OFF ignored due to higher-priority control");
                    }

                    isLightOnFromSchedule = false;
                    //ESP_LOGI(TAG, "⚫ Light turned OFF (scheduled)");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



esp_err_t RTCManager::startMonitoring() {
    if (rtc_monitor_task_handle) return ESP_OK;
    return xTaskCreate(rtc_monitor_task, "rtc_monitor", 4096, this, 4, &rtc_monitor_task_handle) == pdPASS ? ESP_OK : ESP_FAIL;
}

void RTCManager::stopMonitoring() {
    if (rtc_monitor_task_handle) {
        vTaskDelete(rtc_monitor_task_handle);
        rtc_monitor_task_handle = nullptr;
    }
}

bool RTCManager::isInitialized() const {
    return i2c_initialized;
}

void RTCManager::logStatus() {
    struct tm t = getCurrentTime();
    float temp = getTemperature();
    char buf[64];
    strftime(buf, sizeof(buf), "%c", &t);
    ESP_LOGI(TAG, "Time: %s, Temp: %.2fC", buf, temp);
}

void RTCManager::resetMonitorTimestamps() {
    last_time_sent = 0;
    last_date_sent = 0;
    ESP_LOGI(TAG, "\u23F1\uFE0F RTC monitor timestamps reset");
}


///////////////////////

void RTCManager::setSchedule(const struct tm& start, const struct tm& end) {
    schedule_start_time = start;
    schedule_end_time = end;
    ESP_LOGI(TAG, "✅ Schedule set: Start - %02d:%02d, End - %02d:%02d",
             start.tm_hour, start.tm_min, end.tm_hour, end.tm_min);
}

void RTCManager::getSchedule(struct tm& start_out, struct tm& end_out) const {
    start_out = schedule_start_time;
    end_out = schedule_end_time;
}

void RTCManager::setScheduleEnabled(bool enabled) {
    schedule_enabled = enabled;
    ESP_LOGI(TAG, "📶 Schedule control is now: %s", enabled ? "ENABLED" : "DISABLED");
}

bool RTCManager::isScheduleEnabled() const {
    return schedule_enabled;
}


