//MQTTMANAGER.CPP

#include "MQTTClient.HPP"
#include "LIGHTCONTROLLER.HPP"
#include "RTCMANAGER.HPP"  // Required for NTP sync
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>  // For PRId32
#include <ctime>   // for struct tm
#include "cJSON.h"
#include <sys/time.h>


static const char *TAG = "MQTTClient";

MQTTClient* MQTTClient::instance = nullptr;

MQTTClient::MQTTClient() : client(nullptr), is_connected(false) {
    command_queue = xQueueCreate(10, sizeof(char*));
}

MQTTClient::~MQTTClient() {
    if (client) esp_mqtt_client_destroy(client);
    if (command_queue) vQueueDelete(command_queue);
}

MQTTClient& MQTTClient::getInstance() {
    if (!instance) {
        instance = new MQTTClient();
    }
    return *instance;
}

esp_err_t MQTTClient::initialize() {
    ESP_LOGI(TAG, "Initializing MQTT Client");

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.credentials.client_id = CLIENT_ID;
    
    // Add network timeout configuration
    mqtt_cfg.network.timeout_ms = 10000;  // 10 second timeout
    mqtt_cfg.network.refresh_connection_after_ms = 20000;  // Refresh connection every 20 seconds
    
    // Add buffer sizes
    mqtt_cfg.buffer.size = 1024;
    mqtt_cfg.buffer.out_size = 1024;

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, this);
    
    esp_err_t start_result = esp_mqtt_client_start(client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_result));
        return start_result;
    }

    xTaskCreate(commandProcessTask, "mqtt_cmd_task", 4096, this, 5, NULL);
    ESP_LOGI(TAG, "MQTT Client initialized");
    return ESP_OK;
}

bool MQTTClient::isConnected() const {
    return is_connected;
}

esp_err_t MQTTClient::publishStatus(const char* status) {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot publish status - not connected");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(client, STATUS_TOPIC, status, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish status");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Published status: %s (msg_id: %d)", status, msg_id);
    return ESP_OK;
}

esp_err_t MQTTClient::publishTemperature(float temperature) {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot publish temperature - not connected");
        return ESP_FAIL;
    }

    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.2f", temperature);

    int msg_id = esp_mqtt_client_publish(client, TEMP_TOPIC, temp_str, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish temperature");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Published temperature: %.2f°C (msg_id: %d)", temperature, msg_id);
    return ESP_OK;
}

esp_err_t MQTTClient::publishTime(const char* time_str) {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot publish time - not connected");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(client, TIME_TOPIC, time_str, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish time");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Published time: %s (msg_id: %d)", time_str, msg_id);
    return ESP_OK;
}

esp_err_t MQTTClient::publishDate(const char* date_str) {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot publish date - not connected");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(client, DATE_TOPIC, date_str, 0, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish date");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Published date: %s (msg_id: %d)", date_str, msg_id);
    return ESP_OK;
}

esp_err_t MQTTClient::subscribeToNTPSync() {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot subscribe to NTP sync - not connected");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_subscribe(client, NTP_SYNC_TOPIC, 1);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to NTP sync topic");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Subscribed to NTP sync topic (msg_id: %d)", msg_id);
    return ESP_OK;
}

esp_err_t MQTTClient::subscribeToDateTime() {
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot subscribe to date-time updates - not connected");
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_subscribe(client, SUB_DATETIME_TOPIC, 1);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to date-time topic");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to date-time topic (msg_id: %d)", msg_id);
    return ESP_OK;
}


void MQTTClient::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    MQTTClient* mqtt_client = static_cast<MQTTClient*>(handler_args);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT connected to broker");
            mqtt_client->is_connected = true;
            
            // Subscribe to topics
            int sub_result1 = esp_mqtt_client_subscribe(mqtt_client->client, LIGHT1_TOPIC, 1);
            int sub_result2 = esp_mqtt_client_subscribe(mqtt_client->client, NTP_SYNC_TOPIC, 1);
            int sub_result3 = esp_mqtt_client_subscribe(mqtt_client->client, SUB_DATETIME_TOPIC, 1);
            int sub_result4 = esp_mqtt_client_subscribe(mqtt_client->client, SUB_SCHEDULE_TOPIC,1);
            int sub_result5 = esp_mqtt_client_subscribe(mqtt_client->client, SUB_SCHEDULE_CONTROL_TOPIC,1);
            
            if (sub_result1 == -1 || sub_result2 == -1 || sub_result3 == -1 || sub_result4 == -1 || sub_result5 == -1 ) {
                ESP_LOGE(TAG, "Failed to subscribe to topics");
            } else {
                ESP_LOGI(TAG, "Subscribed to command topics");
            }
            
            // Publish initial status
            mqtt_client->publishStatus("OFF");
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            ESP_LOGI(TAG, "MQTT disconnected from broker");
            mqtt_client->is_connected = false;
            break;
        }

        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT data received - Topic length: %d, Data length: %d", 
                    event->topic_len, event->data_len);

            if (event->topic_len <= 0 || event->data_len <= 0) {
                ESP_LOGW(TAG, "Invalid topic or data length");
                break;
            }

            char topic[event->topic_len + 1];
            memcpy(topic, event->topic, event->topic_len);
            topic[event->topic_len] = '\0';

            char* data = (char*)malloc(event->data_len + 1);
            if (data) {
                memcpy(data, event->data, event->data_len);
                data[event->data_len] = '\0';

                ESP_LOGI(TAG, "Received - Topic: %s, Data: %s", topic, data);

                if (strcmp(topic, LIGHT1_TOPIC) == 0 || strcmp(topic, NTP_SYNC_TOPIC) == 0 || strcmp(topic, SUB_DATETIME_TOPIC) == 0) {
                    if (xQueueSend(mqtt_client->command_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Command queue full. Dropping command: %s", data);
                        free(data);
                    }
                } 
                else if(strcmp(topic, SUB_SCHEDULE_TOPIC) == 0){
                    mqtt_client->handleScheduleCommand(data);
                    free(data);
                }
                else if (strcmp(topic, SUB_SCHEDULE_CONTROL_TOPIC) == 0){
                    mqtt_client->handleScheduleControlCommand(data);
                    free(data);
                }else {
                    ESP_LOGW(TAG, "Unknown topic: %s", topic);
                    free(data);
                }
            } else {
                ESP_LOGE(TAG, "Failed to allocate memory for command data");
            }
            break;
        }

        case MQTT_EVENT_ERROR: {
            ESP_LOGE(TAG, "MQTT event error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: %d", event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: %d", event->error_handle->connect_return_code);
            }
            break;
        }

        case MQTT_EVENT_SUBSCRIBED: {
            ESP_LOGI(TAG, "MQTT subscribed, msg_id: %d", event->msg_id);
            break;
        }

        case MQTT_EVENT_UNSUBSCRIBED: {
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id: %d", event->msg_id);
            break;
        }

        case MQTT_EVENT_PUBLISHED: {
            ESP_LOGI(TAG, "MQTT published, msg_id: %d", event->msg_id);
            break;
        }

        case MQTT_EVENT_BEFORE_CONNECT: {
            ESP_LOGI(TAG, "MQTT attempting to connect...");
            break;
        }

        case MQTT_EVENT_DELETED: {
            ESP_LOGI(TAG, "MQTT event deleted, msg_id: %d", event->msg_id);
            break;
        }

        case MQTT_USER_EVENT: {
            ESP_LOGI(TAG, "MQTT user event: %" PRId32, event_id);
            break;
        }

        case MQTT_EVENT_ANY: {
            // This is used for event registration, not actual events
            ESP_LOGD(TAG, "MQTT_EVENT_ANY received");
            break;
        }

        default: {
            ESP_LOGI(TAG, "Unhandled MQTT event: %" PRId32, event_id);
            break;
        }
    }
}

void MQTTClient::commandProcessTask(void* parameters) {
    MQTTClient* mqtt_client = static_cast<MQTTClient*>(parameters);
    char* command;

    ESP_LOGI(TAG, "Command process task started");

    while (1) {
        if (xQueueReceive(mqtt_client->command_queue, &command, portMAX_DELAY)) {
            mqtt_client->processCommand(command);
            free(command);
        }
    }
}

void MQTTClient::processCommand(const char* command) {
    ESP_LOGI(TAG, "Processing command: %s", command);
 
    if (strcmp(command, "ON") == 0) {
    if (!LightController::getInstance().getState()) {
        LightController::getInstance().requestTurnOn(LightController::MANUAL);
        publishStatus("ON");
        ESP_LOGI(TAG, "🖐️ Manual command: Light turned ON");
    } else {
        ESP_LOGI(TAG, "⚠️ Manual command: Light already ON — no action taken");
    }

    // When manual ON, disable schedule if it had turned ON the light
    RTCManager::getInstance().setScheduleEnabled(false);

    } else if (strcmp(command, "OFF") == 0) {
        if (LightController::getInstance().getState()) {
            //LightController::getInstance().turnOff();
            LightController::getInstance().requestTurnOff(LightController::MANUAL);
            publishStatus("OFF");
            ESP_LOGI(TAG, "🖐️ Manual command: Light turned OFF");
        } else {
            ESP_LOGI(TAG, "⚠️ Manual command: Light already OFF — no action taken");
        }

        // When manual OFF, disable schedule if it had turned ON the light
        RTCManager::getInstance().setScheduleEnabled(false);
    }
    else if (strcmp(command, "SYNC") == 0) {
    ESP_LOGI(TAG, "NTP sync command received");

    // Initiate NTP sync
    RTCManager::getInstance().syncWithNTP();

    // Small delay to allow NTP sync to complete (adjust if needed)
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Get the updated system time (from SNTP)
    time_t now = time(NULL);
    struct tm updated_time;
    localtime_r(&now, &updated_time);

    // ✅ Set system time to RTC
    RTCManager::getInstance().setDateTime(&updated_time);

    // 📨 Prepare formatted strings for date and time
    char dateStr[32], timeStr[32];
    snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
             updated_time.tm_mday,
             updated_time.tm_mon + 1,
             updated_time.tm_year + 1900);
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d",
             updated_time.tm_hour,
             updated_time.tm_min);

    // 🔁 Publish the latest synced date and time
    publishDate(dateStr);
    publishTime(timeStr);

    ESP_LOGI(TAG, "✅ NTP sync successful. Published date: %s, time: %s", dateStr, timeStr);

    // 🔄 Reset monitoring timestamps so RTC monitor restarts tracking cleanly
    RTCManager::getInstance().resetMonitorTimestamps();
    } else if (command[0] == '{') {
    
    // Parse JSON (expected from SUB_DATETIME_TOPIC)
    cJSON* root = cJSON_Parse(command);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return;
    }

    const cJSON* dateNode = cJSON_GetObjectItem(root, "date");
    const cJSON* timeNode = cJSON_GetObjectItem(root, "time");

    struct tm timeinfo = {};
    bool valid = true;

    if (cJSON_IsString(dateNode) && dateNode->valuestring) {
        ESP_LOGI(TAG, "📥 Received date: %s", dateNode->valuestring);
        int year, month, day;
        if (sscanf(dateNode->valuestring, "%2d-%2d-%4d", &day, &month, &year) == 3) {
            timeinfo.tm_mday = day;
            timeinfo.tm_mon  = month - 1;
            timeinfo.tm_year = year - 1900;
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to parse date");
            valid = false;
        }
    } else {
        ESP_LOGW(TAG, "⚠️ 'date' field missing or invalid");
        valid = false;
    }

    if (cJSON_IsString(timeNode) && timeNode->valuestring) {
        ESP_LOGI(TAG, "📥 Received time: %s", timeNode->valuestring);
        int hour, min, sec;
        if (sscanf(timeNode->valuestring, "%2d:%2d:%2d", &hour, &min, &sec) >= 2) {
            timeinfo.tm_hour = hour;
            timeinfo.tm_min  = min;
            timeinfo.tm_sec  = sec;
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to parse time");
            valid = false;
        }
    } else {
        ESP_LOGW(TAG, "⚠️ 'time' field missing or invalid");
        valid = false;
    }

    cJSON_Delete(root);

    if (valid) {
        
        // 🕒 Set RTC time
        RTCManager::getInstance().setDateTime(&timeinfo);

        // 🕒 Set system time
        time_t new_time = mktime(&timeinfo);
        struct timeval tv = { .tv_sec = new_time, .tv_usec = 0 };

        if (settimeofday(&tv, nullptr) != 0) {
            ESP_LOGE(TAG, "❌ Failed to set system time");
        } else {
            ESP_LOGI(TAG, "⏰ System time updated successfully with node-red ");
        }

        // 🔁 Confirm updated time
        struct tm updated_time = RTCManager::getInstance().getCurrentTime();

        // 📨 Publish back to Node-RED
        char dateStr[32], timeStr[32];
        snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
                 updated_time.tm_mday,
                 updated_time.tm_mon + 1,
                 updated_time.tm_year + 1900);
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 updated_time.tm_hour,
                 updated_time.tm_min,
                 updated_time.tm_sec);

        publishDate(dateStr);
        publishTime(timeStr);

        ESP_LOGI(TAG, "✅ Applied and published updated date: %s, time: %s", dateStr, timeStr);
    
    
        // 🔁 Ensure RTC task re-publishes after manual sync
        RTCManager::getInstance().resetMonitorTimestamps();  
    }
    }else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
    }
}

esp_err_t MQTTClient::subscribeToSchedule() {
    if (!is_connected) return ESP_FAIL;
    int msg_id = esp_mqtt_client_subscribe(client, SUB_SCHEDULE_TOPIC, 1);
    ESP_LOGI(TAG, "Subscribed to schedule topic (msg_id: %d)", msg_id);
    return msg_id == -1 ? ESP_FAIL : ESP_OK;
}

esp_err_t MQTTClient::subscribeToScheduleControl() {
    if (!is_connected) return ESP_FAIL;
    int msg_id = esp_mqtt_client_subscribe(client, SUB_SCHEDULE_CONTROL_TOPIC, 1);
    ESP_LOGI(TAG, "Subscribed to schedule control topic (msg_id: %d)", msg_id);
    return msg_id == -1 ? ESP_FAIL : ESP_OK;
}

esp_err_t MQTTClient::publishSchedule() {
    if (!is_connected) return ESP_FAIL;

    struct tm start = {}, end = {};
    RTCManager::getInstance().getSchedule(start, end);

    char startBuf[16], endBuf[16];
    strftime(startBuf, sizeof(startBuf), "%H:%M", &start);
    strftime(endBuf, sizeof(endBuf), "%H:%M", &end);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "S_T", startBuf);
    cJSON_AddStringToObject(root, "E_T", endBuf);

    char* msg = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    int msg_id = esp_mqtt_client_publish(client, SCHEDULE_TOPIC, msg, 0, 1, 0);
    ESP_LOGI(TAG, "Published schedule: %s (msg_id: %d)", msg, msg_id);
    free(msg);
    return msg_id == -1 ? ESP_FAIL : ESP_OK;
}

void MQTTClient::handleScheduleCommand(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Invalid schedule JSON received");
        return;
    }

    const cJSON* startNode = cJSON_GetObjectItem(root, "startTimeIST");
    const cJSON* endNode = cJSON_GetObjectItem(root, "endTimeIST");

    if (cJSON_IsString(startNode) && cJSON_IsString(endNode)) {
        struct tm start = {}, end = {};
        sscanf(startNode->valuestring, "%2d:%2d", &start.tm_hour, &start.tm_min);
        sscanf(endNode->valuestring, "%2d:%2d", &end.tm_hour, &end.tm_min);

        RTCManager::getInstance().setSchedule(start, end);
        publishSchedule();
    }

    cJSON_Delete(root);
}

void MQTTClient::handleScheduleControlCommand(const char* data) {
    if (strcasecmp(data, "ON") == 0) {
        RTCManager::getInstance().setScheduleEnabled(true);
        ESP_LOGI(TAG, "📅 Schedule control ENABLED");
    } 
    else if (strcasecmp(data, "OFF") == 0) {
        RTCManager::getInstance().setScheduleEnabled(false);
        ESP_LOGI(TAG, "📅 Schedule control DISABLED");

        // 🧠 Check if light is ON due to schedule, then turn it OFF
        LightController& light = LightController::getInstance();
        if (light.getState() && light.getCurrentOwner() == LightController::RTC) {
            light.requestTurnOff(LightController::RTC);
            publishStatus("OFF");
            ESP_LOGI(TAG, "⚠️ Schedule disabled — light turned OFF");
        }
    } 
    else {
        ESP_LOGW(TAG, "Invalid schedule control: %s", data);
    }

    publishSchedule();  // Reflect current state to MQTT
}
