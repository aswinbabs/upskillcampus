// IRMANAGER.CPP

#include "IRMANAGER.HPP"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "LIGHTCONTROLLER.HPP"

static const char* TAG = "IRManager";

IRManager* IRManager::instance = nullptr;
static QueueHandle_t ir_evt_queue = nullptr;
static TimerHandle_t ir_auto_off_timer = nullptr;

IRManager::IRManager() {}

IRManager& IRManager::getInstance() {
    if (!instance) {
        instance = new IRManager();
    }
    return *instance;
}

// Timer callback: turns off light only if IR still owns it
static void IRManagerAutoOffCallback(TimerHandle_t xTimer) {
    LightController& controller = LightController::getInstance();
    if (controller.getState()) {
        // Only turn off if IR still owns control
        if (controller.getCurrentOwner() == LightController::IR) {
            controller.requestTurnOff(LightController::IR);
            ESP_LOGI(TAG, "⏲️ Auto-off: IR still owns light. Turning off.");
        } else {
            ESP_LOGI(TAG, "⏲️ Auto-off skipped: light now owned by another source.");
        }
    }
}

// Task to handle IR events
static void IRManagerTask(void* arg) {
    int evt;
    while (true) {
        if (xQueueReceive(ir_evt_queue, &evt, portMAX_DELAY)) {
            LightController& controller = LightController::getInstance();
            if (controller.getState()) {
                controller.requestTurnOff(LightController::IR);
            } else {
                controller.requestTurnOn(LightController::IR);

                // Start or reset 5-second auto-off timer
                if (ir_auto_off_timer == nullptr) {
                    ir_auto_off_timer = xTimerCreate(
                        "IRAutoOffTimer",
                        pdMS_TO_TICKS(5000),
                        pdFALSE,
                        NULL,
                        IRManagerAutoOffCallback
                    );
                }
                xTimerStop(ir_auto_off_timer, 0);  // Just in case
                xTimerStart(ir_auto_off_timer, 0);
            }
        }
    }
}

// ISR handler
void IRAM_ATTR IRManager::IRInterruptHandler(void* arg) {
    static uint32_t last_trigger_tick = 0;
    uint32_t now = xTaskGetTickCountFromISR();

    if ((now - last_trigger_tick) > pdMS_TO_TICKS(500)) {
        int signal = 1;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ir_evt_queue, &signal, &xHigherPriorityTaskWoken);
        last_trigger_tick = now;

        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

// Initialization function
esp_err_t IRManager::initialize() {
    ESP_LOGI(TAG, "Initializing IR sensor on GPIO %d", IR_PIN);

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << IR_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(IR_PIN, IRInterruptHandler, NULL);

    ir_evt_queue = xQueueCreate(5, sizeof(int));
    if (!ir_evt_queue) {
        ESP_LOGE(TAG, "Failed to create IR event queue");
        return ESP_FAIL;
    }

    xTaskCreate(IRManagerTask, "ir_task", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "IR Manager initialized");
    return ESP_OK;
}
