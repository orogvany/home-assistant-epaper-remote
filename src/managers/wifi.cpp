#include "wifi.h"
#include "config.h"
#include "constants.h"
#include "store.h"
#include <WiFi.h>

static const char* TAG = "wifi";

void launch_wifi(Configuration* config, EntityStore* store) {
    WiFi.onEvent([store](WiFiEvent_t event, WiFiEventInfo_t info) {
        ESP_LOGI(TAG, "received wifi event: %d", event);

        switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "obtained IP address");
            store_set_wifi_state(store, ConnState::Up);
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            // Don't update UI if this is an intentional idle disconnect (Phase 3)
            if (store_get_wifi_idle(store)) {
                ESP_LOGI(TAG, "disconnected (idle, no UI update)");
            } else {
                ESP_LOGI(TAG, "disconnected");
                store_set_wifi_state(store, ConnState::ConnectionError);
            }
            break;
        }
    });

    WiFi.mode(WIFI_STA);
    if (FEATURE_WIFI_MODEM_SLEEP) {
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
    }
    WiFi.setAutoReconnect(true);

    // If creds are configured, use them. Otherwise try saved creds (from WiFiManager portal)
    if (config->wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Connecting to WiFi: %s", config->wifi_ssid);
        WiFi.begin(config->wifi_ssid, config->wifi_password);
    } else {
        ESP_LOGI(TAG, "No hardcoded WiFi — trying saved credentials");
        WiFi.begin();
    }
}
