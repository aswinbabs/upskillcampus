//WIFIMANAGER.CPP

#include "WIFIMANAGER.HPP"
#include "esp_log.h"
#include "esp_netif.h"
#include <string.h>

static const char *TAG = "WiFiManager";

WiFiManager* WiFiManager::instance = nullptr;

WiFiManager::WiFiManager() : is_connected(false), retry_count(0) {
    wifi_event_group = xEventGroupCreate();
}

WiFiManager& WiFiManager::getInstance() {
    if (instance == nullptr) {
        instance = new WiFiManager();
    }
    return *instance;
}

void WiFiManager::event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    WiFiManager* manager = static_cast<WiFiManager*>(arg);
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (manager->retry_count < MAXIMUM_RETRY) {
            esp_wifi_connect();
            manager->retry_count++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(manager->wifi_event_group, WIFI_FAIL_BIT);
        }
        manager->is_connected = false;
        ESP_LOGI(TAG, "Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        manager->retry_count = 0;
        manager->is_connected = true;
        xEventGroupSetBits(manager->wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t WiFiManager::initialize() {
    ESP_LOGI(TAG, "Initializing WiFi Manager");
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        this,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        this,
                                                        &instance_got_ip));

    // Configure WiFi
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");
    return ESP_OK;
}

bool WiFiManager::isConnected() const {
    return is_connected;
}

void WiFiManager::waitForConnection() {
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

esp_err_t WiFiManager::reconnect() {
    if (!is_connected) {
        retry_count = 0;
        return esp_wifi_connect();
    }
    return ESP_OK;
}

WiFiManager::~WiFiManager() {
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
    }
}