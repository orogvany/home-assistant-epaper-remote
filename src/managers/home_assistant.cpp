#include "config.h"
#include "constants.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "managers/home_assistant.h"
#include "store.h"
#include <WebSocketsClient.h>
#include <cJSON.h>

typedef struct home_assistant_context {
    EntityStore* store;
    Configuration* config;
    WebSocketsClient* client;
    ConnState state;
    SemaphoreHandle_t mutex;
    TaskHandle_t task;

    uint16_t event_id;

    uint8_t entity_count;
    const char* entity_ids[MAX_ENTITIES];
    bool entity_states[MAX_ENTITIES];
    int8_t entity_values[MAX_ENTITIES]; // -1 = unknown (handles switches)
    TickType_t last_command_sent_at_ms[MAX_ENTITIES];
} home_assistant_context_t;

static const char* TAG = "home_assistant";

void hass_update_state(home_assistant_context_t* hass, ConnState state) {
    xSemaphoreTake(hass->mutex, portMAX_DELAY);
    ConnState previous_state = hass->state;
    hass->state = state;
    xSemaphoreGive(hass->mutex);

    if (previous_state == state) {
        return;
    }

    if (state == ConnState::Initializing) {
    } else if (state == ConnState::ConnectionError && previous_state == ConnState::InvalidCredentials) {
    } else {
        store_set_hass_state(hass->store, state);
    }

    xTaskNotifyGive(hass->task);
}

uint16_t hass_generate_event_id(home_assistant_context_t* hass) {
    xSemaphoreTake(hass->mutex, portMAX_DELAY);
    uint16_t event_id = hass->event_id++;
    xSemaphoreGive(hass->mutex);

    return event_id;
}

void hass_send_text(home_assistant_context_t* hass, const char* text) {
    hass->client->sendTXT(text, strlen(text));
}

void hass_cmd_authenticate(home_assistant_context_t* hass) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "access_token", hass->config->home_assistant_token);
    const char* request = cJSON_PrintUnformatted(root);
    hass_send_text(hass, request);
    cJSON_free((void*)request);
    cJSON_Delete(root);
}

int16_t hass_match_entity(home_assistant_context_t* hass, char* key) {
    for (uint8_t i = 0; i < hass->entity_count; i++) {
        if (strcmp(key, hass->entity_ids[i]) == 0) {
            return i;
        }
    }

    return -1;
}

void hass_parse_entity_update(home_assistant_context_t* hass, uint8_t widget_idx, cJSON* item) {
    cJSON* state = cJSON_GetObjectItem(item, "s");
    if (cJSON_IsString(state)) {
        if (strcmp(state->valuestring, "on") == 0) {
            hass->entity_states[widget_idx] = true;
        } else if (strcmp(state->valuestring, "off") == 0) {
            hass->entity_states[widget_idx] = false;
        }
    }

    cJSON* attributes = cJSON_GetObjectItem(item, "a");
    if (cJSON_IsObject(attributes)) {
        cJSON* percentage = cJSON_GetObjectItem(attributes, "percentage");
        if (cJSON_IsNumber(percentage)) {
            hass->entity_values[widget_idx] = percentage->valueint;
        }

        cJSON* brightness = cJSON_GetObjectItem(attributes, "brightness");
        if (cJSON_IsNumber(brightness)) {
            hass->entity_values[widget_idx] = brightness->valueint * 100 / 254;
        }

        cJSON* off_brightness = cJSON_GetObjectItem(attributes, "off_brightness");
        if (cJSON_IsNumber(off_brightness)) {
            hass->entity_values[widget_idx] = off_brightness->valueint * 100 / 254;
        }
    }

    TickType_t now = xTaskGetTickCount();
    if (hass->last_command_sent_at_ms[widget_idx] != 0 &&
        (now - hass->last_command_sent_at_ms[widget_idx]) < pdMS_TO_TICKS(HASS_IGNORE_UPDATE_DELAY_MS)) {
        ESP_LOGI(TAG, "Ignoring update of entity %s", hass->entity_ids[widget_idx]);
    } else {
        uint8_t value = 0;
        if (hass->entity_states[widget_idx]) {
            if (hass->entity_values[widget_idx] == -1) {
                value = 1;
            } else {
                value = hass->entity_values[widget_idx];
            }
        }

        ESP_LOGI(TAG, "Setting value of widget %d to %d", widget_idx, value);
        store_update_value(hass->store, widget_idx, value);
    }
}

void hass_handle_entity_update(home_assistant_context_t* hass, cJSON* event) {
    cJSON* initial_values = cJSON_GetObjectItem(event, "a");
    if (cJSON_IsObject(initial_values)) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, initial_values) {
            int16_t entity_id = hass_match_entity(hass, item->string);
            if (entity_id != -1) {
                ESP_LOGI(TAG, "Found initial value for widget %d (%s)", entity_id, item->string);
                hass_parse_entity_update(hass, entity_id, item);
            }
        }
    }

    cJSON* changes = cJSON_GetObjectItem(event, "c");
    if (cJSON_IsObject(changes)) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, changes) {
            int16_t entity_id = hass_match_entity(hass, item->string);
            if (entity_id != -1) {
                cJSON* plus_value = cJSON_GetObjectItem(item, "+");
                if (cJSON_IsObject(plus_value)) {
                    ESP_LOGI(TAG, "Found update for widget %d (%s)", entity_id, item->string);
                    hass_parse_entity_update(hass, entity_id, plus_value);
                }
            }
        }
    }

    hass_update_state(hass, ConnState::Up);
}

void hass_cmd_subscribe(home_assistant_context_t* hass) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", hass_generate_event_id(hass));
    cJSON_AddStringToObject(root, "type", "subscribe_entities");
    cJSON_AddItemToObject(root, "entity_ids", cJSON_CreateStringArray(hass->entity_ids, hass->entity_count));

    const char* request = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "%s", request);
    hass_send_text(hass, request);
    cJSON_free((void*)request);
    cJSON_Delete(root);
}

void hass_handle_server_payload(home_assistant_context_t* hass, cJSON* json) {
    cJSON* type_item = cJSON_GetObjectItem(json, "type");
    if (cJSON_IsString(type_item) && (type_item->valuestring != NULL)) {
        ESP_LOGI(TAG, "Received Home Assistant message of type %s", type_item->valuestring);

        if (strcmp(type_item->valuestring, "auth_required") == 0) {
            ESP_LOGI(TAG, "Logging in to home assistant...");
            hass_cmd_authenticate(hass);
        } else if (strcmp(type_item->valuestring, "auth_invalid") == 0) {
            hass_update_state(hass, ConnState::InvalidCredentials);
        } else if (strcmp(type_item->valuestring, "auth_ok") == 0) {
            ESP_LOGI(TAG, "Authentication successful, subscribing to events");
            hass_cmd_subscribe(hass);
        } else if (strcmp(type_item->valuestring, "event") == 0) {
            cJSON* event = cJSON_GetObjectItem(json, "event");
            if (cJSON_IsObject(event)) {
                hass_handle_entity_update(hass, event);
            }
        }
    }
}

static home_assistant_context_t* ws_hass_ctx = nullptr;

static void hass_ws_event_handler(WStype_t type, uint8_t* payload, size_t length) {
    home_assistant_context_t* hass = ws_hass_ctx;

    switch (type) {
    case WStype_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        break;
    case WStype_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        hass_update_state(hass, ConnState::ConnectionError);
        break;
    case WStype_TEXT: {
        cJSON* json = cJSON_ParseWithLength((const char*)payload, length);
        if (json) {
            hass_handle_server_payload(hass, json);
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "JSON parsing failed");
        }
        break;
    }
    case WStype_ERROR:
        ESP_LOGI(TAG, "WebSocket error");
        hass_update_state(hass, ConnState::ConnectionError);
        break;
    default:
        break;
    }
}

void hass_send_command(home_assistant_context_t* hass, Command* cmd) {
    cJSON *root, *service_data;
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", hass_generate_event_id(hass));
    cJSON_AddStringToObject(root, "type", "call_service");

    switch (cmd->type) {
    case CommandType::SetLightBrightnessPercentage:
        cJSON_AddStringToObject(root, "domain", "light");
        cJSON_AddStringToObject(root, "service", "turn_on");
        cJSON_AddItemToObject(root, "service_data", service_data = cJSON_CreateObject());
        cJSON_AddStringToObject(service_data, "entity_id", cmd->entity_id);
        cJSON_AddNumberToObject(service_data, "brightness_pct", cmd->value);
        break;
    case CommandType::SetFanSpeedPercentage:
        cJSON_AddStringToObject(root, "domain", "fan");
        cJSON_AddStringToObject(root, "service", "set_percentage");
        cJSON_AddItemToObject(root, "service_data", service_data = cJSON_CreateObject());
        cJSON_AddStringToObject(service_data, "entity_id", cmd->entity_id);
        cJSON_AddNumberToObject(service_data, "percentage", cmd->value);
        break;
    case CommandType::SwitchOnOff:
        cJSON_AddStringToObject(root, "domain", "switch");
        if (cmd->value == 0) {
            cJSON_AddStringToObject(root, "service", "turn_off");
        } else {
            cJSON_AddStringToObject(root, "service", "turn_on");
        }
        cJSON_AddItemToObject(root, "service_data", service_data = cJSON_CreateObject());
        cJSON_AddStringToObject(service_data, "entity_id", cmd->entity_id);
        break;
    case CommandType::AutomationOnOff:
        cJSON_AddStringToObject(root, "domain", "automation");
        if (cmd->value == 0) {
            cJSON_AddStringToObject(root, "service", "turn_off");
        } else {
            cJSON_AddStringToObject(root, "service", "turn_on");
        }
        cJSON_AddItemToObject(root, "service_data", service_data = cJSON_CreateObject());
        cJSON_AddStringToObject(service_data, "entity_id", cmd->entity_id);
        break;
    default:
        ESP_LOGI(TAG, "Service type not supported");
        cJSON_Delete(root);
        return;
    }

    const char* request = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Sending %s", request);

    hass->last_command_sent_at_ms[cmd->entity_idx] = xTaskGetTickCount();
    hass_send_text(hass, request);
    cJSON_free((void*)request);
    cJSON_Delete(root);
}

static void parse_url(const char* url, bool* use_ssl, String* host, uint16_t* port, String* path) {
    String u(url);

    if (u.startsWith("wss://")) {
        *use_ssl = true;
        u = u.substring(6);
    } else if (u.startsWith("ws://")) {
        *use_ssl = false;
        u = u.substring(5);
    }

    int path_start = u.indexOf('/');
    if (path_start == -1) {
        *path = "/";
    } else {
        *path = u.substring(path_start);
        u = u.substring(0, path_start);
    }

    int port_start = u.indexOf(':');
    if (port_start == -1) {
        *host = u;
        *port = *use_ssl ? 443 : 80;
    } else {
        *host = u.substring(0, port_start);
        *port = u.substring(port_start + 1).toInt();
    }
}

void home_assistant_task(void* arg) {
    HomeAssistantTaskArgs* ctx = static_cast<HomeAssistantTaskArgs*>(arg);
    EntityStore* store = ctx->store;

    ESP_LOGI(TAG, "Waiting for wifi...");
    store_wait_for_wifi_up(store);
    ESP_LOGI(TAG, "Wifi is up, connecting...");

    home_assistant_context_t* hass = new home_assistant_context_t{};
    hass->store = store;
    hass->config = ctx->config;
    hass->mutex = xSemaphoreCreateMutex();
    hass->event_id = 1;
    hass->entity_count = store->entity_count;
    hass->task = xTaskGetCurrentTaskHandle();
    for (uint8_t entity_idx = 0; entity_idx < store->entity_count; entity_idx++) {
        hass->entity_ids[entity_idx] = store->entities[entity_idx].entity_id;
        hass->entity_values[entity_idx] = -1;
    }

    ws_hass_ctx = hass;

    bool use_ssl;
    String host, path;
    uint16_t port;
    parse_url(ctx->config->home_assistant_url, &use_ssl, &host, &port, &path);

    WebSocketsClient* wsClient = new WebSocketsClient();
    hass->client = wsClient;

    if (use_ssl) {
        wsClient->beginSSL(host.c_str(), port, path.c_str(), nullptr, "");
        wsClient->setAuthorization("");
    } else {
        wsClient->begin(host.c_str(), port, path.c_str());
    }

    wsClient->onEvent(hass_ws_event_handler);
    wsClient->setReconnectInterval(HASS_RECONNECT_DELAY_MS);
    wsClient->enableHeartbeat(30000, 10000, 2);

    Command command;
    while (1) {
        wsClient->loop();

        xSemaphoreTake(hass->mutex, portMAX_DELAY);
        ConnState state = hass->state;
        xSemaphoreGive(hass->mutex);

        if (state == ConnState::Up) {
            while (store_get_pending_command(store, &command)) {
                hass_send_command(hass, &command);
                store_ack_pending_command(store, &command);
                vTaskDelay(pdMS_TO_TICKS(HASS_TASK_SEND_DELAY_MS));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
