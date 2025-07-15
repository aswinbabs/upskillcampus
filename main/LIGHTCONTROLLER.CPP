//LIGHTCONTROLLER.CPP

#include "LIGHTCONTROLLER.HPP"
#include "esp_log.h"

static const char *TAG = "LightController";

LightController* LightController::instance = nullptr;

LightController::LightController() 
    : light_state(false), current_owner(RTC) {}  // Default lowest priority

LightController& LightController::getInstance() {
    if (instance == nullptr) {
        instance = new LightController();
    }
    return *instance;
}

esp_err_t LightController::initialize() {
    ESP_LOGI(TAG, "Initializing Light Controller");

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LIGHT_RELAY_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << STATUS_LED_PIN);
    gpio_config(&io_conf);

    gpio_set_level(LIGHT_RELAY_PIN, 0);
    gpio_set_level(STATUS_LED_PIN, 0);

    light_state = false;
    current_owner = RTC;

    ESP_LOGI(TAG, "Light Controller initialized - Relay: GPIO%d, Status LED: GPIO%d",
             LIGHT_RELAY_PIN, STATUS_LED_PIN);

    return ESP_OK;
}

esp_err_t LightController::requestTurnOn(ControlSource source) {
    if (source >= current_owner || !light_state) {
        if (!light_state) {
            gpio_set_level(LIGHT_RELAY_PIN, 1);
            gpio_set_level(STATUS_LED_PIN, 1);
            light_state = true;
        }

        if (source != current_owner) {
            ESP_LOGI(TAG, "🔁 Owner changed: %s → %s",
                     controlSourceToString(current_owner),
                     controlSourceToString(source));
            current_owner = source;
        }

        ESP_LOGI(TAG, "✅ Light turned ON by %s", controlSourceToString(source));
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "⚠️ Light ON request by lower-priority source %s ignored",
                 controlSourceToString(source));
        return ESP_FAIL;
    }
}

esp_err_t LightController::requestTurnOff(ControlSource source) {
    if (source >= current_owner || light_state) {
        if (light_state) {
            gpio_set_level(LIGHT_RELAY_PIN, 0);
            gpio_set_level(STATUS_LED_PIN, 0);
            light_state = false;
        }

        if (source != current_owner) {
            ESP_LOGI(TAG, "🔁 Owner changed: %s → %s",
                     controlSourceToString(current_owner),
                     controlSourceToString(source));
            current_owner = source;
        }

        ESP_LOGI(TAG, "🛑 Light turned OFF by %s", controlSourceToString(source));
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "⚠️ Light OFF request by lower-priority source %s ignored",
                 controlSourceToString(source));
        return ESP_FAIL;
    }
}

bool LightController::getState() const {
    return light_state;
}

LightController::~LightController() {
    turnOff();  // Clean shutdown
}

void LightController::turnOn() {
    requestTurnOn(current_owner);
}

void LightController::turnOff() {
    requestTurnOff(current_owner);
}

LightController::ControlSource LightController::getCurrentOwner() const {
    return current_owner;
}

// 🔹 Static helper to convert enum to string for logs
const char* LightController::controlSourceToString(ControlSource src) {
    switch (src) {
        case MANUAL: return "MANUAL";
        case IR:     return "IR";
        case RTC:    return "RTC";
        default:     return "UNKNOWN";
    }
}
