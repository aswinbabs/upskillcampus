//LIGHTCONTROLLER.HPP

#ifndef LIGHT_CONTROLLER_HPP
#define LIGHT_CONTROLLER_HPP

#include "driver/gpio.h"
#include "esp_err.h"

class LightController {
public:
    enum ControlSource {
        MANUAL = 3,
        IR     = 2,
        RTC    = 1
    };

private:
    static LightController* instance;
    bool light_state;
    ControlSource current_owner;

    // GPIO Configuration
    static const gpio_num_t LIGHT_RELAY_PIN = GPIO_NUM_2;
    static const gpio_num_t STATUS_LED_PIN = GPIO_NUM_5;

    LightController();

    // Helper for internal logging
    static const char* controlSourceToString(ControlSource src);

public:
    static LightController& getInstance();
    esp_err_t initialize();

    esp_err_t requestTurnOn(ControlSource source);
    esp_err_t requestTurnOff(ControlSource source);

    void turnOn(); 
    void turnOff(); 

    bool getState() const;
    ControlSource getCurrentOwner() const;

    ~LightController();
};

#endif // LIGHT_CONTROLLER_HPP
