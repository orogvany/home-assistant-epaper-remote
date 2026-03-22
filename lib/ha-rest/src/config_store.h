#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Maximum number of devices we can track
constexpr int MAX_KNOWN_DEVICES = 64;
constexpr int MAX_UI_DEVICES = 8;

// Device info from HA discovery
struct KnownDevice {
    char entity_id[64];
    char friendly_name[48];
    char domain[16];          // "light", "switch", "fan", etc.
    bool online;              // Reachable/available
};

// Device configured for display in our UI
struct UIDevice {
    char entity_id[64];
    char label[32];
    char widget_type[8];      // "slider" or "button"
    char icon_on[32];
    char icon_off[32];
    int sort_order;
    uint16_t pos_x;
    uint16_t pos_y;
    uint16_t width;
    uint16_t height;
};

// Centralized configuration - loaded from NVS as a single JSON blob.
// All values have defaults so the system works even with empty NVS.
struct AppConfig {
    // WiFi
    char wifi_ssid[33];
    char wifi_password[65];

    // Home Assistant
    char ha_url[128];         // e.g., "http://192.168.0.102:8123"
    char ha_token[256];

    // Settings
    uint32_t poll_interval_ms;
    uint32_t idle_wifi_disconnect_ms;
    uint32_t pms150g_shutdown_idle_ms;
    uint8_t pms150g_rtc_wake_min;

    // Known devices (from HA discovery, synced via UI)
    KnownDevice known_devices[MAX_KNOWN_DEVICES];
    int known_device_count;

    // UI devices (what we actually show)
    UIDevice ui_devices[MAX_UI_DEVICES];
    int ui_device_count;

    // Security
    bool pin_enabled;
    char pin_code[5];         // 4-digit PIN + null terminator

    // Flags
    bool configured;          // Has the user gone through setup?
};

class ConfigStore {
public:
    ConfigStore() = default;

    // Initialize - loads from NVS or seeds defaults
    void begin();

    // Get the current config (read-only access)
    const AppConfig& config() const { return _config; }

    // Get mutable config for modification
    AppConfig& mutableConfig() { return _config; }

    // Save current config to NVS
    bool save();

    // Reset to defaults (wipes NVS config)
    void resetToDefaults();

    // Seed defaults from hardcoded values (for backward compat during migration)
    void seedDefaults(const char* ssid, const char* password,
                      const char* ha_url, const char* ha_token);

    // Update known devices from HA discovery results
    void updateKnownDevices(const KnownDevice* devices, int count);

private:
    AppConfig _config = {};

    void _loadFromNVS();
    void _applyDefaults();
    bool _serializeToJson(String& output);
    bool _deserializeFromJson(const char* json, size_t len);
};
