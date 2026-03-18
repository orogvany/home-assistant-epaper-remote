#pragma once

#include <cstddef>
#include <cstdint>

// Buttons configuration
constexpr uint8_t BUTTON_BORDER_SIZE = 4;
constexpr uint8_t BUTTON_SIZE = 100;
constexpr uint8_t BUTTON_ICON_SIZE = 64;
constexpr uint8_t SLIDER_OFFSET = 100;    // The zero is a bit on the right
constexpr uint8_t TOUCH_AREA_MARGIN = 15; // A touch within 15px of the target is OK

// Home assistant configuration
constexpr uint16_t HASS_MAX_JSON_BUFFER = 1024 * 20; // 20k, home assistant talks a lot
constexpr uint32_t HASS_RECONNECT_DELAY_MS = 10000;

// When sending commands too fast (on a slider), this can flood
// the zigbee network and make the commands fail. Increase this delay
// if you see errors when using sliders.
constexpr uint32_t HASS_TASK_SEND_DELAY_MS = 500;

// When sending commands, we'll receive the updates from the server
// with a delay. This causes jittering in the slider and unnecessary
// commands sent to the server. We ignore updates from the server
// during this delay after a command was sent on an entity.
// FIXME: We can lose updates, we should have an authoritative value
// and a target value in the store at some point.
constexpr uint32_t HASS_IGNORE_UPDATE_DELAY_MS = 1000;

// Battery monitoring
constexpr uint32_t BATTERY_READ_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes
constexpr uint8_t BATTERY_ADC_PIN = 3;
constexpr uint8_t BATTERY_CHARGE_PIN = 4;
constexpr float BATTERY_ADC_DIVIDER_RATIO = 2.0f;

// Touch polling intervals
constexpr uint32_t TOUCH_POLL_ACTIVE_MS = 25;
constexpr uint32_t TOUCH_POLL_IDLE_MS = 500;

// Light sleep wake interval for servicing WebSocket when idle
constexpr uint32_t SLEEP_WAKE_INTERVAL_MS = 5000;

// Idle WiFi disconnect timeout (disconnect WiFi after no touch for this long)
constexpr uint32_t IDLE_WIFI_DISCONNECT_MS = 5 * 60 * 1000; // 5 minutes

// Touch feedback
constexpr bool BUZZER_FEEDBACK_ENABLED = true;
constexpr uint16_t BUZZER_FREQ_HZ = 3000;
constexpr uint8_t BUZZER_DURATION_MS = 15;

// Other constants
constexpr size_t MAX_ENTITIES = 16;
constexpr size_t MAX_WIDGETS_PER_SCREEN = 8;
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 50;
constexpr uint32_t DISPLAY_FULL_REDRAW_TIMEOUT_MS = 30000;
