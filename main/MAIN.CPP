//MAIN.CPP

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "WIFIMANAGER.HPP"
#include "MQTTCLIENT.HPP"
#include "LIGHTCONTROLLER.HPP"
#include "RTCMANAGER.HPP"
#include "IRMANAGER.HPP"

static const char *TAG = "MAIN";

// Enhanced system status task with RTC timestamp
void system_status_task(void* parameter) {
    while(1) {
        // Get system status
        bool wifi_connected = WiFiManager::getInstance().isConnected();
        bool mqtt_connected = MQTTClient::getInstance().isConnected();
        bool rtc_initialized = RTCManager::getInstance().isInitialized();
        
        // Get current time for status logging (if RTC is available)
        char time_buffer[32] = "N/A";
        if (rtc_initialized) {
            struct tm current_time = RTCManager::getInstance().getCurrentTime();
            strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &current_time);
        }
        
        // System status check with timestamp
        if (wifi_connected && mqtt_connected && rtc_initialized) {
            ESP_LOGI(TAG, "[%s] System running normally - WiFi: ✓ MQTT: ✓ RTC: ✓", time_buffer);
        } else {
            ESP_LOGW(TAG, "[%s] System status - WiFi: %s MQTT: %s RTC: %s", 
                    time_buffer,
                    wifi_connected ? "✓" : "✗",
                    mqtt_connected ? "✓" : "✗",
                    rtc_initialized ? "✓" : "✗");
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // Status check every 30 seconds
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 Home Automation System");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize Light Controller
    LightController::getInstance().initialize();
    ESP_LOGI(TAG, "Light Controller initialized");
    
    // Initialize RTC Manager
    if(RTCManager::getInstance().initialize() == ESP_OK) {
        ESP_LOGI(TAG, "RTC initialized successfully");
        
        // Show initial RTC status
        RTCManager::getInstance().logStatus();
        
        // Start RTC monitoring
        RTCManager::getInstance().startMonitoring();
        
    } else {
        ESP_LOGE(TAG, "RTC initialization failed - check DS3231 connections");
        ESP_LOGE(TAG, "Expected connections: SDA=GPIO21, SCL=GPIO22, VCC=3.3V, GND=GND");
    }
    
    // Initialize WiFi Manager
    WiFiManager::getInstance().initialize();
    ESP_LOGI(TAG, "WiFi Manager initialized");
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    WiFiManager::getInstance().waitForConnection();
    ESP_LOGI(TAG, "WiFi connected successfully");
    
    // Initialize MQTT Client
    if (MQTTClient::getInstance().initialize() != ESP_OK) {
    ESP_LOGE(TAG, "MQTT initialization failed");
    } else {
        ESP_LOGI(TAG, "MQTT Client initialized");
    }

    //Initialize IR Manager
    IRManager::getInstance().initialize();
    ESP_LOGI(TAG, "IR Manager initialized");

    
    ESP_LOGI(TAG, "System initialization complete");
    
    // Create system status monitoring task
    xTaskCreate(system_status_task, "system_status", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
    
    // Get the updated time on startup
        struct tm updated_time = RTCManager::getInstance().getCurrentTime();
        
        // Extract date and time as separate strings
        char dateStr[32], timeStr[32];
        snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d", 
                updated_time.tm_mday , updated_time.tm_mon + 1, updated_time.tm_year + 1900);
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", 
                 updated_time.tm_hour, updated_time.tm_min);
        
        // Publish the updated date and time
        MQTTClient::getInstance().publishDate(dateStr);
        MQTTClient::getInstance().publishTime(timeStr);
        
        ESP_LOGI(TAG, "Published date: %s, time: %s after start-up", dateStr, timeStr);

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // Sleep for 1 minute
    }
}