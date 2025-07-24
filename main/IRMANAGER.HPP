// IRMANAGER.HPP

#ifndef IR_MANAGER_HPP
#define IR_MANAGER_HPP

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class IRManager {
private:
    static IRManager* instance;
    static const gpio_num_t IR_PIN = GPIO_NUM_4;  
    IRManager();

public:
    static IRManager& getInstance();
    esp_err_t initialize();
    static void IRInterruptHandler(void* arg);
};

#endif // IR_MANAGER_HPP
