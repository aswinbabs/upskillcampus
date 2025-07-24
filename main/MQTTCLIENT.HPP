//MQTTCLIENT.HPP

#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include "mqtt_client.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// MQTT Configuration
#define MQTT_BROKER_URI             "mqtt://192.168.X.X:1883"
#define CLIENT_ID                   "esp32_home_automation"

// Topics
#define LIGHT1_TOPIC                "home/light1/command"           // Subscribe
#define NTP_SYNC_TOPIC              "home/ntp_sync"                 // Subscribe
#define SUB_DATETIME_TOPIC          "home/sub_dateTime"             // Subscribe
#define SUB_SCHEDULE_TOPIC          "home/sub_schedule"             // Subscribe
#define SUB_SCHEDULE_CONTROL_TOPIC  "home/sub_schedule_control"     // Subscribe

#define TEMP_TOPIC                  "home/temperature"              // Publish
#define DATE_TOPIC                  "home/date"                     // Publish
#define TIME_TOPIC                  "home/time"                     // Publish
#define STATUS_TOPIC                "home/light1/status"            // Publish
#define SCHEDULE_TOPIC              "home/schedule"                 // Publish

class MQTTClient {
private:
    static MQTTClient* instance;

    esp_mqtt_client_handle_t client;
    bool is_connected;
    QueueHandle_t command_queue;

    // Private constructor
    MQTTClient();

    // MQTT Event Handler
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                                   int32_t event_id, void* event_data);

    // Command Task
    static void commandProcessTask(void* parameters);
    void processCommand(const char* command);
    void handleScheduleCommand(const char* json); //N
    void handleScheduleControlCommand(const char* payload);//N

public:
    // Singleton Access
    static MQTTClient& getInstance();

    // Initialization
    esp_err_t initialize();

    // Status Check
    bool isConnected() const;

    // Publishing
    esp_err_t publishStatus(const char* status);
    esp_err_t publishTemperature(float temperature);
    esp_err_t publishTime(const char* time_str);
    esp_err_t publishDate(const char* date_str);
    esp_err_t publishSchedule();                        // Publishes current schedule info


    // Subscriptions
    esp_err_t subscribeToNTPSync();
    esp_err_t subscribeToDateTime();
    esp_err_t subscribeToSchedule();
    esp_err_t subscribeToScheduleControl();


    // Destructor
    ~MQTTClient();
};

#endif // MQTT_CLIENT_HPP