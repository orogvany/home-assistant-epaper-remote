#include "managers/touch.h"
#include "boards.h"
#include "constants.h"
#include "draw.h"
#include "managers/config_server.h"
#include "managers/wifi_portal.h"
#include "wake_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>

static const char* TAG = "touch";

static SemaphoreHandle_t touch_semaphore = nullptr;

static void IRAM_ATTR touch_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(touch_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void touch_task(void* arg) {
    TouchTaskArgs* ctx = static_cast<TouchTaskArgs*>(arg);
    BBCapTouch* bbct = ctx->bbct;
    EntityStore* store = ctx->store;
    Screen* screen = ctx->screen;

    uint32_t ui_state_version = 0;
    auto* ui_state = new UIState{};

    TOUCHINFO ti;
    TouchEvent touch_event = TouchEvent{};
    bool touching = false;
    int active_widget = -1;
    uint32_t last_touch_ms = 0;
    uint8_t widget_original_value = 0;
    uint8_t widget_current_value = 0;

    ESP_LOGI(TAG, "Initializing touchscreen...");
    int rc = -1;
    for (int attempt = 0; attempt < 20 && rc <= 0; attempt++) {
        if (attempt > 0) {
            ESP_LOGI(TAG, "Retrying touch init (attempt %d)...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        rc = bbct->init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    }
    ESP_LOGI(TAG, "init() rc = %d", rc);
    int type = bbct->sensorType();
    ESP_LOGI(TAG, "Sensor type = %d", type);

    // Phase 2: interrupt-driven touch instead of polling
    if (FEATURE_LIGHT_SLEEP) {
        touch_semaphore = xSemaphoreCreateBinary();
        attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touch_isr, FALLING);
    }

    while (true) {
        wake_lock_acquire(); // Hold wake lock during I2C touch read
        bool has_touch = bbct->getSamples(&ti);
        if (!has_touch) wake_lock_release();

        if (has_touch) {
            last_touch_ms = millis();

            // Phase 3: track touch time and trigger WiFi reconnect if idle-disconnected
            if (FEATURE_IDLE_WIFI_DISCONNECT) {
                store_set_last_touch(store, last_touch_ms);
                if (store_get_wifi_idle(store)) {
                    store_set_wifi_idle(store, false);
                    xTaskNotifyGive(store->home_assistant_task);
                }
            }

            ui_state_copy(ctx->state, &ui_state_version, ui_state);

            // Handle touches based on current screen mode
            // All non-widget screens: only respond to initial touch (debounce)
            if (ui_state->mode == UiMode::SettingsMenu && !touching) {
                touching = true;
                int item = getSettingsMenuItemTouched(ti.x[0], ti.y[0]);
                if (item >= -1) {
                    if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                        tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                    }
                }
                if (item == 0) {
                    ESP_LOGI(TAG, "Settings: Configure WiFi");
                    wifi_portal_start("HA-Remote");
                    store_set_ui_mode_override(store, UiMode::WifiSetup);
                } else if (item == 1) {
                    ESP_LOGI(TAG, "Settings: Configure");
                    config_server_start(ctx->config_store);
                    store_set_ui_mode_override(store, UiMode::Configure);
                } else if (item == 2) {
                    ESP_LOGI(TAG, "Settings: About");
                    store_set_ui_mode_override(store, UiMode::About);
                } else if (item == -1) {
                    ESP_LOGI(TAG, "Settings: Back to main");
                    store_set_ui_mode_override(store, UiMode::Blank);
                }
                continue;
            }

            if (ui_state->mode == UiMode::WifiSetup && !touching) {
                touching = true;
                if (ti.x[0] < 200 && ti.y[0] < 80) {
                    if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                        tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                    }
                    ESP_LOGI(TAG, "WiFi Setup: Back to settings");
                    wifi_portal_stop();
                    store_set_ui_mode_override(store, UiMode::SettingsMenu);
                }
                continue;
            }

            if (ui_state->mode == UiMode::About && !touching) {
                touching = true;
                if (ti.x[0] < 200 && ti.y[0] < 80) {
                    if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                        tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                    }
                    ESP_LOGI(TAG, "About: Back to settings");
                    store_set_ui_mode_override(store, UiMode::SettingsMenu);
                }
                continue;
            }

            if (ui_state->mode == UiMode::Configure && !touching) {
                touching = true;
                if (ti.x[0] < 200 && ti.y[0] < 80) {
                    if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                        tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                    }
                    ESP_LOGI(TAG, "Configure: Back to settings");
                    config_server_stop();
                    store_set_ui_mode_override(store, UiMode::SettingsMenu);
                }
                continue;
            }

            if (ui_state->mode != UiMode::MainScreen) {
                continue;
            }

            // Check gear icon touch (main screen only, initial touch only)
            if (!touching && isGearIconTouched(ti.x[0], ti.y[0])) {
                touching = true;
                ESP_LOGI(TAG, "Gear icon tapped — opening settings");
                if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                    tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                }
                store_set_ui_mode_override(store, UiMode::SettingsMenu);
                continue;
            }

            if (active_widget != -1) {
                if (touch_event.x == ti.x[0] && touch_event.y == ti.y[0]) {
                } else {
                    touch_event.x = ti.x[0];
                    touch_event.y = ti.y[0];
                    ESP_LOGI(TAG, "Widget %d, Coordinates: %d %d", active_widget, touch_event.x, touch_event.y);

                    widget_current_value = screen->widgets[active_widget]->getValueFromTouch(&touch_event, widget_original_value);
                    store_send_command(store, screen->entity_ids[active_widget], widget_current_value);
                }
            } else if (touching == false) {
                touch_event.x = ti.x[0];
                touch_event.y = ti.y[0];
                touching = true;
                for (size_t widget_idx = 0; widget_idx < screen->widget_count; widget_idx++) {
                    if (screen->widgets[widget_idx]->isTouching(&touch_event)) {
                        if (BUZZER_FEEDBACK_ENABLED && BUZZER_PIN) {
                            tone(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_DURATION_MS);
                        }
                        ESP_LOGI(TAG, "Starting touch on widget %d", widget_idx);
                        active_widget = widget_idx;

                        widget_original_value = ui_state->widget_values[widget_idx];
                        widget_current_value = screen->widgets[widget_idx]->getValueFromTouch(&touch_event, widget_original_value);
                        store_send_command(store, screen->entity_ids[active_widget], widget_current_value);
                        break;
                    }
                }
            }
            wake_lock_release(); // Done with touch processing
        } else {
            if (touching) {
                if (millis() - last_touch_ms > TOUCH_RELEASE_TIMEOUT_MS) {
                    ESP_LOGI(TAG, "End of touch");
                    touching = false;
                    active_widget = -1;
                }
                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_ACTIVE_MS));
            } else {
                // Poll WiFi portal if active (needs frequent processing)
                if (wifi_portal_is_active()) {
                    if (wifi_portal_poll()) {
                        ESP_LOGI(TAG, "WiFi portal: connected, returning to main");
                        store_set_ui_mode_override(store, UiMode::Blank);
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                } else if (config_server_is_active()) {
                    config_server_poll();
                    vTaskDelay(pdMS_TO_TICKS(50));
                } else if (FEATURE_LIGHT_SLEEP && touch_semaphore) {
                    xSemaphoreTake(touch_semaphore, portMAX_DELAY);
                } else {
                    vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_IDLE_MS));
                }
            }
        }
    }
}
