//WIFIMANAGER.HPP

#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP

#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class WiFiManager {
private:
    static WiFiManager* instance;
    EventGroupHandle_t wifi_event_group;
    bool is_connected;
    int retry_count;
    static const int WIFI_CONNECTED_BIT = BIT0;
    static const int WIFI_FAIL_BIT = BIT1;
    static const int MAXIMUM_RETRY = 10;
    
    // WiFi Configuration
    static constexpr const char* WIFI_SSID = "C-Net Sreedharan";
    static constexpr const char* WIFI_PASSWORD = "sreedharan@32";
    
    WiFiManager();
    static void event_handler(void* arg, esp_event_base_t event_base, 
                             int32_t event_id, void* event_data);

public:
    static WiFiManager& getInstance();
    esp_err_t initialize();
    bool isConnected() const;
    void waitForConnection();
    esp_err_t reconnect();
    ~WiFiManager();
};

#endif // WIFI_MANAGER_HPP