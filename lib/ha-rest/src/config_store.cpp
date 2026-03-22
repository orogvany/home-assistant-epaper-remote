#include "config_store.h"
#include <Preferences.h>
#include <esp_log.h>

static const char* TAG = "config_store";
static const char* NVS_NAMESPACE = "appconfig";
static const char* NVS_KEY = "json";

void ConfigStore::begin() {
    _applyDefaults();
    _loadFromNVS();

    if (_config.configured) {
        ESP_LOGI(TAG, "Config loaded: %s, %d known devices, %d UI devices",
                 _config.ha_url, _config.known_device_count, _config.ui_device_count);
    } else {
        ESP_LOGI(TAG, "No saved config — using defaults");
    }
}

void ConfigStore::_applyDefaults() {
    memset(&_config, 0, sizeof(_config));
    _config.poll_interval_ms = 10000;
    _config.idle_wifi_disconnect_ms = 5 * 60 * 1000;
    _config.pms150g_shutdown_idle_ms = 6UL * 60 * 60 * 1000;
    _config.pms150g_rtc_wake_min = 240;
    _config.configured = false;
}

void ConfigStore::seedDefaults(const char* ssid, const char* password,
                                const char* ha_url, const char* ha_token) {
    // Only seed if no saved config exists
    if (_config.configured) return;

    strlcpy(_config.wifi_ssid, ssid, sizeof(_config.wifi_ssid));
    strlcpy(_config.wifi_password, password, sizeof(_config.wifi_password));

    // Convert ws:// URL to http:// if needed
    if (strncmp(ha_url, "ws://", 5) == 0) {
        snprintf(_config.ha_url, sizeof(_config.ha_url), "http://%s", ha_url + 5);
    } else if (strncmp(ha_url, "wss://", 6) == 0) {
        snprintf(_config.ha_url, sizeof(_config.ha_url), "https://%s", ha_url + 6);
    } else {
        strlcpy(_config.ha_url, ha_url, sizeof(_config.ha_url));
    }

    // Strip /api/websocket path if present
    char* ws_path = strstr(_config.ha_url, "/api/websocket");
    if (ws_path) *ws_path = '\0';

    strlcpy(_config.ha_token, ha_token, sizeof(_config.ha_token));

    ESP_LOGI(TAG, "Seeded defaults: SSID=%s, HA=%s", _config.wifi_ssid, _config.ha_url);
}

void ConfigStore::_loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) { // read-only
        ESP_LOGI(TAG, "NVS namespace not found — first boot");
        return;
    }

    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len == 0) {
        prefs.end();
        return;
    }

    char* buf = (char*)malloc(len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for config", len);
        prefs.end();
        return;
    }

    prefs.getBytes(NVS_KEY, buf, len);
    buf[len] = '\0';
    prefs.end();

    _deserializeFromJson(buf, len);
    free(buf);
}

bool ConfigStore::save() {
    String json;
    if (!_serializeToJson(json)) {
        ESP_LOGE(TAG, "Failed to serialize config");
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) { // read-write
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return false;
    }

    size_t written = prefs.putBytes(NVS_KEY, json.c_str(), json.length());
    prefs.end();

    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write config to NVS");
        return false;
    }

    ESP_LOGI(TAG, "Config saved to NVS (%d bytes)", json.length());
    return true;
}

void ConfigStore::resetToDefaults() {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }
    _applyDefaults();
    ESP_LOGI(TAG, "Config reset to defaults");
}

bool ConfigStore::_serializeToJson(String& output) {
    JsonDocument doc;

    doc["configured"] = _config.configured;

    // WiFi
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = _config.wifi_ssid;
    wifi["password"] = _config.wifi_password;

    // HA
    JsonObject ha = doc["ha"].to<JsonObject>();
    ha["url"] = _config.ha_url;
    ha["token"] = _config.ha_token;

    // Settings
    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["poll_interval_ms"] = _config.poll_interval_ms;
    settings["idle_wifi_disconnect_ms"] = _config.idle_wifi_disconnect_ms;
    settings["pms150g_shutdown_idle_ms"] = _config.pms150g_shutdown_idle_ms;
    settings["pms150g_rtc_wake_min"] = _config.pms150g_rtc_wake_min;

    // Known devices
    JsonArray known = doc["known_devices"].to<JsonArray>();
    for (int i = 0; i < _config.known_device_count; i++) {
        JsonObject d = known.add<JsonObject>();
        d["entity_id"] = _config.known_devices[i].entity_id;
        d["friendly_name"] = _config.known_devices[i].friendly_name;
        d["domain"] = _config.known_devices[i].domain;
        d["online"] = _config.known_devices[i].online;
    }

    // UI devices
    JsonArray ui = doc["ui_devices"].to<JsonArray>();
    for (int i = 0; i < _config.ui_device_count; i++) {
        JsonObject d = ui.add<JsonObject>();
        d["entity_id"] = _config.ui_devices[i].entity_id;
        d["label"] = _config.ui_devices[i].label;
        d["widget_type"] = _config.ui_devices[i].widget_type;
        d["icon_on"] = _config.ui_devices[i].icon_on;
        d["icon_off"] = _config.ui_devices[i].icon_off;
        d["sort_order"] = _config.ui_devices[i].sort_order;
        d["pos_x"] = _config.ui_devices[i].pos_x;
        d["pos_y"] = _config.ui_devices[i].pos_y;
        d["width"] = _config.ui_devices[i].width;
        d["height"] = _config.ui_devices[i].height;
    }

    serializeJson(doc, output);
    return true;
}

bool ConfigStore::_deserializeFromJson(const char* json, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        ESP_LOGE(TAG, "Config JSON parse error: %s", err.c_str());
        return false;
    }

    _config.configured = doc["configured"] | false;

    // WiFi
    strlcpy(_config.wifi_ssid, doc["wifi"]["ssid"] | "", sizeof(_config.wifi_ssid));
    strlcpy(_config.wifi_password, doc["wifi"]["password"] | "", sizeof(_config.wifi_password));

    // HA
    strlcpy(_config.ha_url, doc["ha"]["url"] | "", sizeof(_config.ha_url));
    strlcpy(_config.ha_token, doc["ha"]["token"] | "", sizeof(_config.ha_token));

    // Settings
    _config.poll_interval_ms = doc["settings"]["poll_interval_ms"] | 10000;
    _config.idle_wifi_disconnect_ms = doc["settings"]["idle_wifi_disconnect_ms"] | (5 * 60 * 1000);
    _config.pms150g_shutdown_idle_ms = doc["settings"]["pms150g_shutdown_idle_ms"] | (6UL * 60 * 60 * 1000);
    _config.pms150g_rtc_wake_min = doc["settings"]["pms150g_rtc_wake_min"] | 240;

    // Known devices
    JsonArray known = doc["known_devices"];
    _config.known_device_count = 0;
    for (JsonObject d : known) {
        if (_config.known_device_count >= MAX_KNOWN_DEVICES) break;
        KnownDevice& dev = _config.known_devices[_config.known_device_count++];
        strlcpy(dev.entity_id, d["entity_id"] | "", sizeof(dev.entity_id));
        strlcpy(dev.friendly_name, d["friendly_name"] | "", sizeof(dev.friendly_name));
        strlcpy(dev.domain, d["domain"] | "", sizeof(dev.domain));
        dev.online = d["online"] | true;
    }

    // UI devices
    JsonArray ui = doc["ui_devices"];
    _config.ui_device_count = 0;
    for (JsonObject d : ui) {
        if (_config.ui_device_count >= MAX_UI_DEVICES) break;
        UIDevice& dev = _config.ui_devices[_config.ui_device_count++];
        strlcpy(dev.entity_id, d["entity_id"] | "", sizeof(dev.entity_id));
        strlcpy(dev.label, d["label"] | "", sizeof(dev.label));
        strlcpy(dev.widget_type, d["widget_type"] | "button", sizeof(dev.widget_type));
        strlcpy(dev.icon_on, d["icon_on"] | "lightbulb_outline", sizeof(dev.icon_on));
        strlcpy(dev.icon_off, d["icon_off"] | "lightbulb_off_outline", sizeof(dev.icon_off));
        dev.sort_order = d["sort_order"] | 0;
        dev.pos_x = d["pos_x"] | 30;
        dev.pos_y = d["pos_y"] | 30;
        dev.width = d["width"] | 0;
        dev.height = d["height"] | 0;
    }

    return true;
}

void ConfigStore::updateKnownDevices(const KnownDevice* devices, int count) {
    if (count > MAX_KNOWN_DEVICES) count = MAX_KNOWN_DEVICES;
    memcpy(_config.known_devices, devices, count * sizeof(KnownDevice));
    _config.known_device_count = count;
    save();
}
