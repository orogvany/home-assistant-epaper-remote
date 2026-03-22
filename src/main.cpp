#include "boards.h"
#include "config_remote.h"
#include "constants.h"
#include "draw.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include <Wire.h>
#include "managers/battery.h"
#include "managers/power.h"
#include "managers/home_assistant.h"
#include "managers/touch.h"
#include "managers/ui.h"
#include "managers/wifi.h"
#include "screen.h"
#include "store.h"
#include "ui_state.h"
#include "widgets/Slider.h"
#include <FastEPD.h>

static Configuration config;

static FASTEPD epaper;
static Screen screen;
static BBCapTouch bbct;
static EntityStore store;
static SharedUIState shared_ui_state;

static UITaskArgs ui_task_args;
static TouchTaskArgs touch_task_args;
static HomeAssistantTaskArgs hass_task_args;
static BatteryTaskArgs battery_task_args;

static void init_display(FASTEPD* ep) {
    ep->initPanel(DISPLAY_PANEL);
    ep->setPanelSize(DISPLAY_HEIGHT, DISPLAY_WIDTH);
    ep->setRotation(90);
    ep->einkPower(true);
}

static bool usb_connected = false;

static bool idle_hook() {
    // Don't sleep during boot (let all tasks fully initialize)
    if (millis() < LIGHT_SLEEP_BOOT_DELAY_MS) return false;

    // Don't sleep if USB is connected (preserves serial debug)
    if (usb_connected) return false;

    // Don't sleep again too quickly — give tasks time to run after wake
    static int64_t last_wake = 0;
    int64_t now = esp_timer_get_time();
    if (last_wake > 0 && (now - last_wake) < LIGHT_SLEEP_MIN_WAKE_US) return false;

    esp_light_sleep_start();
    last_wake = esp_timer_get_time();
    return true;
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { delay(10); } // Wait for USB CDC, max 3 seconds
    Serial.println("=== M5Paper S3 HA Remote - Booting ===");
    Serial.flush();

    // Only init I2C early if we need it for BMI270 or Phase 4 RTC check
    // (touch task handles its own I2C init via bbct->init())
    if (FEATURE_BMI270_SUSPEND || (FEATURE_PMS150G_SHUTDOWN && HAS_PMS150G)) {
        Wire.begin(TOUCH_SDA, TOUCH_SCL);
    }

    // Phase 3.5: Put BMI270 gyroscope into suspend mode (~3.5µA vs ~950µA)
    if (FEATURE_BMI270_SUSPEND) {
        Wire.beginTransmission(0x68);
        Wire.write(0x7D); // PWR_CTRL register
        Wire.write(0x00); // Disable accelerometer and gyroscope
        Wire.endTransmission();
    }

    // Phase 4: Check if this is an RTC wake from PMS150G power-off
    if (FEATURE_PMS150G_SHUTDOWN && HAS_PMS150G && power_was_rtc_wake()) {
        power_clear_rtc_flag();
        init_display(&epaper);

        // Read RTC seconds + minutes (BCD-encoded) for position randomization
        Wire.beginTransmission(BM8563_ADDR);
        Wire.write(0x02); // Seconds register (minutes follows at 0x03)
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)2);
        uint8_t raw_sec = Wire.available() ? Wire.read() : 0;
        uint8_t raw_min = Wire.available() ? Wire.read() : 0;
        uint8_t rtc_sec = ((raw_sec & 0x70) >> 4) * 10 + (raw_sec & 0x0F);
        uint8_t rtc_min = ((raw_min & 0x70) >> 4) * 10 + (raw_min & 0x0F);
        uint16_t seed = rtc_sec + rtc_min * 60;
        int16_t offset_x = (int16_t)((seed * 7) % 400) - 200;  // ±200px horizontal
        int16_t offset_y = (int16_t)((seed * 13) % 300) - 150;  // ±150px vertical

        drawIdleScreen(&epaper, offset_x, offset_y);

        power_setup_rtc_timer(PMS150G_RTC_WAKE_INTERVAL_MIN);
        power_off_pms150g();
    }

    // Detect USB connection (GPIO 5 USB_DET, >0.2V = USB present)
    // Light sleep is disabled when USB is connected to preserve serial debug
    pinMode(5, INPUT);
    usb_connected = (analogReadMilliVolts(5) > 200);

    // Configure light sleep wake sources
    if (FEATURE_LIGHT_SLEEP) {
        esp_sleep_enable_timer_wakeup(SLEEP_WAKE_INTERVAL_MS * 1000);
        gpio_wakeup_enable((gpio_num_t)TOUCH_INT, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        // Register on core 0 only (esp_light_sleep_start is system-wide)
        esp_register_freertos_idle_hook_for_cpu(idle_hook, 0);
    }

    // Initialize objects
    store_init(&store);
    store_set_last_touch(&store, millis()); // Start idle timer from boot

    // Read battery immediately so first screen draw has a real value
    if (FEATURE_BATTERY_INDICATOR && HAS_BATTERY_ADC) {
        pinMode(BATTERY_CHARGE_PIN, INPUT);
        uint16_t raw_mv = analogReadMilliVolts(BATTERY_ADC_PIN);
        uint16_t voltage_mv = (uint16_t)(raw_mv * BATTERY_ADC_DIVIDER_RATIO);
        // Simple voltage-to-percentage (rough, battery task has full lookup table)
        uint8_t pct = voltage_mv >= 4200 ? 100 : voltage_mv <= 3300 ? 0 : (voltage_mv - 3300) * 100 / 900;
        bool charging = (digitalRead(BATTERY_CHARGE_PIN) == LOW);
        store_set_battery(&store, voltage_mv, pct, charging);
        Serial.printf("Battery initial: %d mV (%d%%)\n", voltage_mv, pct);
    }

    ui_state_init(&shared_ui_state);
    configure_remote(&config, &store, &screen);
    initialize_slider_sprites();

    init_display(&epaper);

    // Launch UI task
    ui_task_args.epaper = &epaper;
    ui_task_args.screen = &screen;
    ui_task_args.store = &store;
    ui_task_args.shared_state = &shared_ui_state;
    xTaskCreate(ui_task, "ui", 4096, &ui_task_args, 1, &store.ui_task);

    // Connect to wifi and launch watcher
    launch_wifi(&config, &store);

    // Connect to home assistant
    hass_task_args.config = &config;
    hass_task_args.store = &store;
    hass_task_args.epaper = &epaper;
    xTaskCreate(home_assistant_task, "home_assistant", 8192, &hass_task_args, 1, &store.home_assistant_task);

    // Launch touch task
    touch_task_args.bbct = &bbct;
    touch_task_args.screen = &screen;
    touch_task_args.state = &shared_ui_state;
    touch_task_args.store = &store;
    xTaskCreate(touch_task, "touch", 4096, &touch_task_args, 1, nullptr);

    // Launch battery monitoring task
    battery_task_args.store = &store;
    xTaskCreate(battery_task, "battery", 2048, &battery_task_args, 1, nullptr);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
