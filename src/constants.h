#pragma once

#include <cstddef>
#include <cstdint>

// ============================================================================
// FEATURE FLAGS + CONFIGURATION
// Each feature's enable/disable and its config values are grouped together.
// ============================================================================

// --- WiFi Modem Sleep ---
// Sleeps the WiFi radio between DTIM beacons while maintaining connection.
constexpr bool FEATURE_WIFI_MODEM_SLEEP = true;

// --- CPU Light Sleep ---
// CPU enters light sleep via idle hook when all tasks are blocked.
// Wakes on touch interrupt (GPIO 48) or timer.
// Disabled when USB is connected (preserves serial debug).
constexpr bool FEATURE_LIGHT_SLEEP = false; // BROKEN: light sleep corrupts e-ink display (SPI interrupted mid-refresh). Needs wake lock during display operations.
constexpr uint32_t SLEEP_WAKE_INTERVAL_MS = 5000;            // Timer wake interval for WebSocket servicing
constexpr uint32_t LIGHT_SLEEP_BOOT_DELAY_MS = 30000;        // Don't sleep during first 30s (let tasks fully init)
constexpr uint32_t LIGHT_SLEEP_MIN_WAKE_US = 100000;         // Min 100ms awake between sleep cycles (microseconds)

// --- Idle WiFi Disconnect ---
// Disconnects WiFi after no touch for a configurable period.
// Touch reconnects WiFi automatically.
constexpr bool FEATURE_IDLE_WIFI_DISCONNECT = true;
constexpr uint32_t IDLE_WIFI_DISCONNECT_MS = 5 * 60 * 1000;  // 5 minutes

// --- PMS150G Deep Power-Off ---
// Powers off the entire device via PMS150G after extended idle.
// RTC alarm wakes device periodically to refresh idle screen.
// Button press wakes device for normal operation.
constexpr bool FEATURE_PMS150G_SHUTDOWN = true;
// How long before it goes into deep sleep since last activity
constexpr uint32_t PMS150G_SHUTDOWN_IDLE_MS = 6UL * 60 * 60 * 1000; // 6 hours
// How long between screen refreshes to avoid any burn in.  Also updates the battery %
constexpr uint8_t PMS150G_RTC_WAKE_INTERVAL_MIN = 240;              // 4 hours (max 255)

// --- Battery Indicator ---
// Shows battery percentage and charging status on screen.
// Reads ADC every BATTERY_READ_INTERVAL_MS.
constexpr bool FEATURE_BATTERY_INDICATOR = true;
constexpr uint32_t BATTERY_READ_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes
constexpr uint8_t BATTERY_ADC_PIN = 3;
constexpr uint8_t BATTERY_CHARGE_PIN = 4;
constexpr float BATTERY_ADC_DIVIDER_RATIO = 2.0f;

// --- BMI270 Gyroscope Suspend ---
// Puts the BMI270 IMU into suspend mode on boot (~3.5µA vs ~950µA).
constexpr bool FEATURE_BMI270_SUSPEND = true;

// --- Buzzer Touch Feedback ---
// Audible click on widget touch via onboard passive buzzer.
constexpr bool BUZZER_FEEDBACK_ENABLED = true;
constexpr uint16_t BUZZER_FREQ_HZ = 3000;
constexpr uint8_t BUZZER_DURATION_MS = 15;

// ============================================================================
// WIDGET / UI CONFIGURATION
// ============================================================================
constexpr uint8_t BUTTON_BORDER_SIZE = 4;
constexpr uint8_t BUTTON_SIZE = 100;
constexpr uint8_t BUTTON_ICON_SIZE = 64;
constexpr uint8_t SLIDER_OFFSET = 100;                       // Slider zero offset from left edge
constexpr uint8_t TOUCH_AREA_MARGIN = 15;                    // Touch hit area expansion (px)
constexpr uint32_t DISPLAY_FULL_REDRAW_TIMEOUT_MS = 30000;   // Full refresh to clear ghosting

// ============================================================================
// HOME ASSISTANT CONFIGURATION
// ============================================================================
constexpr uint16_t HASS_MAX_JSON_BUFFER = 1024 * 20;         // 20KB buffer for HA WebSocket messages
constexpr uint32_t HASS_RECONNECT_DELAY_MS = 10000;          // WebSocket reconnect interval
constexpr uint32_t HASS_TASK_SEND_DELAY_MS = 500;            // Delay between commands (prevents Zigbee flooding)
// FIXME: We can lose updates, we should have an authoritative value
// and a target value in the store at some point.
constexpr uint32_t HASS_IGNORE_UPDATE_DELAY_MS = 1000;       // Ignore server updates after sending a command

// ============================================================================
// OTHER CONSTANTS
// ============================================================================
constexpr size_t MAX_ENTITIES = 16;
constexpr size_t MAX_WIDGETS_PER_SCREEN = 8;
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 50;
constexpr uint32_t TOUCH_POLL_ACTIVE_MS = 25;                // Touch polling while finger is down
constexpr uint32_t TOUCH_POLL_IDLE_MS = 500;                 // Touch polling when idle (no FEATURE_LIGHT_SLEEP)
