//RTCMANAGER.HPP

#ifndef RTC_MANAGER_HPP
#define RTC_MANAGER_HPP

#include <ctime>
#include <string>
#include "esp_err.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define DS3231_I2C_ADDR 0x68

#define DS3231_REG_SECONDS    0x00
#define DS3231_REG_MINUTES    0x01
#define DS3231_REG_HOURS      0x02
#define DS3231_REG_DAY        0x03
#define DS3231_REG_DATE       0x04
#define DS3231_REG_MONTH      0x05
#define DS3231_REG_YEAR       0x06
#define DS3231_REG_CONTROL    0x0E
#define DS3231_REG_STATUS     0x0F
#define DS3231_REG_TEMP_MSB   0x11
#define DS3231_REG_TEMP_LSB   0x12

#define I2C_MASTER_SCL_IO     22
#define I2C_MASTER_SDA_IO     21
#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_MASTER_FREQ_HZ    100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

class RTCManager {
private:
    RTCManager();
    static RTCManager* instance;

    bool i2c_initialized;
    TaskHandle_t rtc_monitor_task_handle;
    time_t last_time_sent;
    time_t last_date_sent;

    SemaphoreHandle_t i2c_mutex;

    // Schedule-related variables
    struct tm schedule_start_time;
    struct tm schedule_end_time;
    bool schedule_enabled;

    esp_err_t i2c_master_init();
    esp_err_t ds3231_write_reg(uint8_t reg, uint8_t data);
    esp_err_t ds3231_read_reg(uint8_t reg, uint8_t* data);
    esp_err_t ds3231_read_time_regs(uint8_t* time_data);
    uint8_t bcd_to_dec(uint8_t bcd);
    uint8_t dec_to_bcd(uint8_t dec);

    static void rtc_monitor_task(void* parameter);

    // Prevent copy/assignment
    RTCManager(const RTCManager&) = delete;
    RTCManager& operator=(const RTCManager&) = delete;

public:
    static RTCManager& getInstance();
    ~RTCManager();

    esp_err_t initialize();
    esp_err_t setDateTime(const struct tm* timeinfo);
    struct tm getCurrentTime();
    esp_err_t syncWithNTP();
    float getTemperature();

    esp_err_t startMonitoring();
    void stopMonitoring();
    bool isInitialized() const;
    void logStatus();
    void resetMonitorTimestamps();

    // Schedule-related methods
    void setSchedule(const struct tm& start, const struct tm& end);
    void getSchedule(struct tm& start_out, struct tm& end_out) const;
    void setScheduleEnabled(bool enabled);
    bool isScheduleEnabled() const;

};

#endif
