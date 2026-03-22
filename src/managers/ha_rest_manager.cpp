#include "ha_rest_manager.h"
#include "boards.h"
#include "constants.h"
#include "draw.h"
#include "esp_log.h"
#include "managers/power.h"
#include "wake_lock.h"
#include <WiFi.h>
#include <ha_rest_client.h>

static const char* TAG = "ha_rest";

// Parse HA state into a uint8_t value for the store
static uint8_t parse_ha_value(const HAEntityState& state) {
    // Check named states
    if (strcmp(state.state, "on") == 0 || strcmp(state.state, "locked") == 0 ||
        strcmp(state.state, "playing") == 0 || strcmp(state.state, "open") == 0) {

        // Check for analog value in attributes
        if (state.brightness >= 0) return state.brightness * 100 / 255;
        if (state.percentage >= 0) return state.percentage;
        if (state.current_position >= 0) return state.current_position;
        if (state.volume_level >= 0) return (uint8_t)(state.volume_level * 100);
        return 1; // Binary on
    }

    if (strcmp(state.state, "off") == 0 || strcmp(state.state, "unlocked") == 0 ||
        strcmp(state.state, "paused") == 0 || strcmp(state.state, "idle") == 0 ||
        strcmp(state.state, "closed") == 0) {
        return 0;
    }

    // Try numeric state (input_number: "52.0")
    char* endptr;
    double numeric = strtod(state.state, &endptr);
    if (endptr != state.state && *endptr == '\0') {
        return (uint8_t)numeric;
    }

    return 0;
}

// Map CommandType to HA domain + service + extra JSON
static bool send_ha_command(HARestClient* client, Command* cmd) {
    char extra[64] = {};

    switch (cmd->type) {
    case CommandType::SetLightBrightnessPercentage:
        if (cmd->value == 0)
            return client->callService("light", "turn_off", cmd->entity_id);
        snprintf(extra, sizeof(extra), "\"brightness_pct\":%d", cmd->value);
        return client->callService("light", "turn_on", cmd->entity_id, extra);

    case CommandType::SetFanSpeedPercentage:
        if (cmd->value == 0)
            return client->callService("fan", "turn_off", cmd->entity_id);
        snprintf(extra, sizeof(extra), "\"percentage\":%d", cmd->value);
        return client->callService("fan", "set_percentage", cmd->entity_id, extra);

    case CommandType::SwitchOnOff:
        return client->callService("switch",
            cmd->value == 0 ? "turn_off" : "turn_on", cmd->entity_id);

    case CommandType::AutomationOnOff:
        return client->callService("automation",
            cmd->value == 0 ? "turn_off" : "turn_on", cmd->entity_id);

    case CommandType::SetCoverPosition:
        snprintf(extra, sizeof(extra), "\"position\":%d", cmd->value);
        return client->callService("cover", "set_cover_position", cmd->entity_id, extra);

    case CommandType::ActivateScene:
        return client->callService("scene", "turn_on", cmd->entity_id);

    case CommandType::RunScript:
        return client->callService("script", "turn_on", cmd->entity_id);

    case CommandType::LockUnlock:
        return client->callService("lock",
            cmd->value == 0 ? "unlock" : "lock", cmd->entity_id);

    case CommandType::SetMediaPlayerVolume:
        snprintf(extra, sizeof(extra), "\"volume_level\":%.2f", cmd->value / 100.0);
        return client->callService("media_player", "volume_set", cmd->entity_id, extra);

    case CommandType::MediaPlayerPlayPause:
        return client->callService("media_player",
            cmd->value == 0 ? "media_pause" : "media_play", cmd->entity_id);

    case CommandType::SetInputNumber:
        snprintf(extra, sizeof(extra), "\"value\":%d", cmd->value);
        return client->callService("input_number", "set_value", cmd->entity_id, extra);

    case CommandType::InputBooleanToggle:
        return client->callService("input_boolean",
            cmd->value == 0 ? "turn_off" : "turn_on", cmd->entity_id);

    case CommandType::VacuumCommand:
        if (cmd->value == 0) return client->callService("vacuum", "stop", cmd->entity_id);
        if (cmd->value == 1) return client->callService("vacuum", "start", cmd->entity_id);
        return client->callService("vacuum", "return_to_base", cmd->entity_id);

    default:
        ESP_LOGE(TAG, "Unknown command type: %d", (int)cmd->type);
        return false;
    }
}

// Poll all configured entity states from HA
static void poll_entity_states(HARestClient* client, EntityStore* store) {
    for (uint8_t i = 0; i < store->entity_count; i++) {
        HAEntityState ha_state = {};
        if (client->getEntityState(store->entities[i].entity_id, &ha_state)) {
            uint8_t value = parse_ha_value(ha_state);
            store_update_value(store, i, value);
        }
    }
}

void ha_rest_manager_task(void* arg) {
    HARestManagerArgs* ctx = static_cast<HARestManagerArgs*>(arg);
    EntityStore* store = ctx->store;

    ESP_LOGI(TAG, "Waiting for WiFi...");
    store_wait_for_wifi_up(store);
    ESP_LOGI(TAG, "WiFi is up");

    // Initialize REST client
    HAConfig ha_config = {
        .base_url = ctx->config->home_assistant_url,
        .token = ctx->config->home_assistant_token,
    };
    // Convert ws:// URL to http:// for REST
    static char http_url[128];
    const char* url = ctx->config->home_assistant_url;
    if (strncmp(url, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http://%s", url + 5);
    } else if (strncmp(url, "wss://", 6) == 0) {
        snprintf(http_url, sizeof(http_url), "https://%s", url + 6);
    } else {
        strlcpy(http_url, url, sizeof(http_url));
    }
    // Strip /api/websocket path if present
    char* ws_path = strstr(http_url, "/api/websocket");
    if (ws_path) *ws_path = '\0';

    ha_config.base_url = http_url;
    ESP_LOGI(TAG, "HA REST base URL: %s", ha_config.base_url);

    HARestClient client;
    client.begin(ha_config);

    // Test connectivity
    wake_lock_acquire();
    int status = client.testConnection();
    wake_lock_release();
    ESP_LOGI(TAG, "HA REST API test: %d", status);

    if (status == 200) {
        // Initial state sync
        wake_lock_acquire();
        poll_entity_states(&client, store);
        wake_lock_release();
        store_set_hass_state(store, ConnState::Up);
        ESP_LOGI(TAG, "Initial state sync complete");
    } else {
        ESP_LOGE(TAG, "HA REST API connection failed: %d", status);
        store_set_hass_state(store, ConnState::ConnectionError);
    }

    // Main loop: poll states + send commands
    Command command;
    bool wifi_is_off = false;
    uint32_t last_poll_ms = millis();

    while (1) {
        // Wait for notification (command queued) or poll interval
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HA_REST_POLL_INTERVAL_MS));

        // Phase 3: Handle idle WiFi disconnect
        if (FEATURE_IDLE_WIFI_DISCONNECT) {
            // Check if touch woke us from idle
            if (wifi_is_off && !store_get_wifi_idle(store)) {
                ESP_LOGI(TAG, "Touch detected, reconnecting WiFi...");
                WiFi.mode(WIFI_STA);
                if (FEATURE_WIFI_MODEM_SLEEP) WiFi.setSleep(WIFI_PS_MIN_MODEM);
                WiFi.begin(ctx->config->wifi_ssid, ctx->config->wifi_password);
                wifi_is_off = false;
                store_wait_for_wifi_up(store);

                // Re-sync states after reconnect, then fall through
                // to command processing (don't continue — pending commands
                // need to be sent and their wake locks released)
                wake_lock_acquire();
                poll_entity_states(&client, store);
                wake_lock_release();
                store_set_hass_state(store, ConnState::Up);
                last_poll_ms = millis();
            }

            // Check for idle timeout
            uint32_t last_touch = store_get_last_touch(store);
            if (!wifi_is_off && last_touch > 0) {
                uint32_t idle_ms = millis() - last_touch;
                if (idle_ms > IDLE_WIFI_DISCONNECT_MS) {
                    ESP_LOGI(TAG, "Idle timeout, disconnecting WiFi (screen unchanged)");
                    store_set_wifi_idle(store, true);
                    wifi_is_off = true;
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                    continue;
                }
            }

            // Phase 4: PMS150G shutdown
            if (FEATURE_PMS150G_SHUTDOWN && wifi_is_off && HAS_PMS150G && last_touch > 0) {
                uint32_t idle_ms = millis() - last_touch;
                if (idle_ms > PMS150G_SHUTDOWN_IDLE_MS) {
                    ESP_LOGI(TAG, "Deep idle timeout, entering PMS150G shutdown");
                    drawIdleScreen(ctx->epaper, 0, 0);
                    power_setup_rtc_timer(PMS150G_RTC_WAKE_INTERVAL_MIN);
                    power_off_pms150g();
                }
            }
        }

        if (wifi_is_off) continue;

        // Send any pending commands (optimistic — UI already updated)
        // Wake lock was acquired by store_send_command() to bridge the gap
        // between touch releasing its lock and us processing the command
        bool had_commands = false;
        while (store_get_pending_command(store, &command)) {
            had_commands = true;
            wake_lock_acquire(); // Hold lock during HTTP call
            ESP_LOGI(TAG, "Sending command: %s = %d", command.entity_id, command.value);
            bool ok = send_ha_command(&client, &command);
            if (!ok) {
                ESP_LOGE(TAG, "Command failed for %s", command.entity_id);
            }
            store_ack_pending_command(store, &command);
            wake_lock_release();
        }
        // Release the wake lock that store_send_command() acquired
        if (had_commands) {
            wake_lock_release();
        }

        // Periodic state polling
        uint32_t now = millis();
        if (now - last_poll_ms >= HA_REST_POLL_INTERVAL_MS) {
            wake_lock_acquire();
            poll_entity_states(&client, store);
            wake_lock_release();
            last_poll_ms = now;
        }
    }
}
