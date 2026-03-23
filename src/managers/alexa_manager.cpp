#include "alexa_manager.h"
#include "constants.h"
#include "entity_value.h"
#include "esp_log.h"
#include "wake_lock.h"
#include <alexa_api.h>
#include <alexa_auth.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* TAG = "alexa";

static AlexaAuth auth;
static AlexaAPI api;

static void parse_capability_state(const char* json_str, uint8_t entity_idx, EntityStore* store) {
    JsonDocument doc;
    if (deserializeJson(doc, json_str)) return;

    const char* ns = doc["namespace"];
    const char* name = doc["name"];
    if (!ns || !name) return;

    if (strcmp(ns, "Alexa.PowerController") == 0 && strcmp(name, "powerState") == 0) {
        const char* val = doc["value"];
        if (val) {
            store_update_value(store, entity_idx, strcmp(val, "ON") == 0 ? 1 : 0);
        }
    } else if (strcmp(ns, "Alexa.BrightnessController") == 0 && strcmp(name, "brightness") == 0) {
        int val = doc["value"] | -1;
        if (val >= 0) {
            store_update_value(store, entity_idx, (uint8_t)val);
        }
    }
}

static void poll_alexa_states(EntityStore* store) {
    JsonDocument req_doc;
    JsonArray requests = req_doc["stateRequests"].to<JsonArray>();
    uint8_t alexa_indices[MAX_ENTITIES];
    uint8_t alexa_count = 0;

    for (uint8_t i = 0; i < store->entity_count; i++) {
        if (store->entities[i].source != EntitySource::Alexa) continue;
        JsonObject r = requests.add<JsonObject>();
        r["entityId"] = store->entities[i].entity_id;
        r["entityType"] = "ENTITY";
        alexa_indices[alexa_count++] = i;
    }

    if (alexa_count == 0) return;

    String body;
    serializeJson(req_doc, body);

    wake_lock_acquire();
    String response = api.pollDeviceState(body);
    wake_lock_release();

    if (response.isEmpty()) return;

    JsonDocument resp_doc;
    if (deserializeJson(resp_doc, response, DeserializationOption::NestingLimit(20))) return;

    JsonArray device_states = resp_doc["deviceStates"];
    for (JsonObject ds : device_states) {
        const char* entity_id = ds["entity"]["entityId"];
        if (!entity_id) continue;

        uint8_t entity_idx = 0xFF;
        for (uint8_t i = 0; i < alexa_count; i++) {
            if (strcmp(store->entities[alexa_indices[i]].entity_id, entity_id) == 0) {
                entity_idx = alexa_indices[i];
                break;
            }
        }
        if (entity_idx == 0xFF) continue;

        JsonArray cap_states = ds["capabilityStates"];
        for (const char* cap_json : cap_states) {
            parse_capability_state(cap_json, entity_idx, store);
        }
    }
}

static bool send_alexa_command(Command* cmd) {
    bool power_on = (cmd->value > 0);
    int brightness = -1;

    if (cmd->type == CommandType::AlexaSetBrightness) {
        brightness = cmd->value;
    }

    wake_lock_acquire();
    bool ok = api.setLightState(cmd->entity_id, power_on, brightness);
    wake_lock_release();

    return ok;
}

void alexa_manager_task(void* arg) {
    AlexaManagerArgs* ctx = static_cast<AlexaManagerArgs*>(arg);
    EntityStore* store = ctx->store;
    const AppConfig& cfg = ctx->config_store->config();

    if (!cfg.alexa_enabled) {
        ESP_LOGI(TAG, "Alexa not enabled, task idle");
        vTaskDelete(nullptr);
        return;
    }

    store->alexa_enabled = true;

    ESP_LOGI(TAG, "Waiting for WiFi...");
    store_wait_for_wifi_up(store);
    ESP_LOGI(TAG, "WiFi is up");

    wake_lock_acquire();
    bool loaded = auth.loadFromNVS();
    wake_lock_release();

    if (!loaded || !auth.hasValidTokens()) {
        ESP_LOGW(TAG, "No valid Alexa credentials - authorize via web config");
        store_set_alexa_state(store, ConnState::ConnectionError);
        while (1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }

    wake_lock_acquire();
    bool refreshed = auth.refreshCredentials();
    wake_lock_release();

    if (!refreshed) {
        ESP_LOGE(TAG, "Token refresh failed - re-authorize via web config");
        store_set_alexa_state(store, ConnState::ConnectionError);
        while (1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }

    api.begin(auth.getRefreshToken().c_str(),
              auth.getAccessToken().c_str(),
              auth.getCookies().c_str(),
              auth.getCustomerID().c_str(),
              auth.getDomain().c_str());

    wake_lock_acquire();
    poll_alexa_states(store);
    wake_lock_release();

    store_set_alexa_state(store, ConnState::Up);
    ESP_LOGI(TAG, "Alexa connected, initial state sync complete");

    const uint32_t poll_interval = 60000;
    Command command;

    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(poll_interval));

        while (store_get_pending_command(store, &command)) {
            if (command.type != CommandType::AlexaSetPower &&
                command.type != CommandType::AlexaSetBrightness) {
                store_ack_pending_command(store, &command);
                wake_lock_release();
                continue;
            }

            if (send_alexa_command(&command)) {
                store_ack_pending_command(store, &command);
            }
            wake_lock_release();
        }

        wake_lock_acquire();
        poll_alexa_states(store);
        wake_lock_release();
    }
}
