#include "store.h"
#include "constants.h"
#include "esp_system.h"
#include "wake_lock.h"

static const char* TAG = "store";

void store_init(EntityStore* store) {
    store->mutex = xSemaphoreCreateMutex();
    store->event_group = xEventGroupCreate();
}

void store_set_wifi_state(EntityStore* store, ConnState state) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    ConnState previous_state = store->wifi;
    store->wifi = state;
    xSemaphoreGive(store->mutex);

    if (state != previous_state) {
        if (state == ConnState::Up) {
            xEventGroupSetBits(store->event_group, BIT_WIFI_UP);
        } else {
            xEventGroupClearBits(store->event_group, BIT_WIFI_UP);
        }

        if (store->ui_task) {
            xTaskNotifyGive(store->ui_task);
        }
    }
}

void store_set_hass_state(EntityStore* store, ConnState state) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    ConnState previous_state = store->home_assistant;
    store->home_assistant = state;
    xSemaphoreGive(store->mutex);

    if (state != previous_state && store->ui_task) {
        xTaskNotifyGive(store->ui_task);
    }
}

void store_update_value(EntityStore* store, uint8_t entity_idx, uint8_t value) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    HomeAssistantEntity& entity = store->entities[entity_idx];
    uint8_t previous_value = entity.current_value;
    entity.current_value = value;
    xSemaphoreGive(store->mutex);

    if (previous_value != value && store->ui_task) {
        xTaskNotifyGive(store->ui_task);
    }
}

void store_send_command(EntityStore* store, uint8_t entity_idx, uint8_t value) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    HomeAssistantEntity& entity = store->entities[entity_idx];
    entity.current_value = value;
    entity.command_value = value;
    entity.command_pending = true;
    xSemaphoreGive(store->mutex);

    ESP_LOGI(TAG, "Sending command to update entity %s to value %d", store->entities[entity_idx].entity_id, value);

    // Hold wake lock until HA task processes the command — prevents CPU
    // from sleeping between touch releasing its lock and HA acquiring one
    wake_lock_acquire();

    if (store->home_assistant_task) {
        xTaskNotifyGive(store->home_assistant_task);
    }
    if (store->ui_task) {
        xTaskNotifyGive(store->ui_task);
    }
}

bool store_get_pending_command(EntityStore* store, Command* command) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);

    for (uint8_t entity_idx = 0; entity_idx < store->entity_count; ++entity_idx) {
        HomeAssistantEntity& entity = store->entities[entity_idx];
        if (entity.command_pending) {
            command->entity_id = entity.entity_id;
            command->entity_idx = entity_idx;
            command->type = entity.command_type;
            command->value = entity.command_value;
            xSemaphoreGive(store->mutex);
            return true;
        }
    }

    xSemaphoreGive(store->mutex);
    return false;
}

void store_ack_pending_command(EntityStore* store, const Command* command) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);

    HomeAssistantEntity& entity = store->entities[command->entity_idx];
    if (entity.command_value == command->value) {
        entity.command_pending = false;
    }

    xSemaphoreGive(store->mutex);
}

void store_update_ui_state(EntityStore* store, const Screen* screen, UIState* ui_state) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);

    // Check for UI mode override (settings menu, setup screens, etc.)
    if (store->ui_mode_override != UiMode::Blank) {
        ui_state->mode = store->ui_mode_override;
    } else if (store->wifi == ConnState::Initializing &&
               store->home_assistant == ConnState::Initializing) {
        ui_state->mode = UiMode::Boot; // Only show boot screen on initial startup
    } else {
        // Always show main screen — gear icon lets user access settings
        // Connection status will be shown via status icons (TODO)
        ui_state->mode = UiMode::MainScreen;
    }

    for (uint8_t widget_idx = 0; widget_idx < screen->widget_count; widget_idx++) {
        uint8_t entity_id = screen->entity_ids[widget_idx];
        ui_state->widget_values[widget_idx] = store->entities[entity_id].current_value;
    }

    ui_state->battery_percentage = store->battery.percentage;
    ui_state->battery_charging = store->battery.charging;

    xSemaphoreGive(store->mutex);
}

void store_wait_for_wifi_up(EntityStore* store) {
    xEventGroupWaitBits(store->event_group, BIT_WIFI_UP, pdFALSE, pdTRUE, portMAX_DELAY);
}

void store_set_battery(EntityStore* store, uint16_t voltage_mv, uint8_t percentage, bool charging) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    bool changed = store->battery.percentage != percentage || store->battery.charging != charging;
    store->battery.voltage_mv = voltage_mv;
    store->battery.percentage = percentage;
    store->battery.charging = charging;
    xSemaphoreGive(store->mutex);

    if (changed && store->ui_task) {
        xTaskNotifyGive(store->ui_task);
    }
}

void store_set_last_touch(EntityStore* store, uint32_t ms) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    store->last_touch_ms = ms;
    xSemaphoreGive(store->mutex);
}

uint32_t store_get_last_touch(EntityStore* store) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    uint32_t ms = store->last_touch_ms;
    xSemaphoreGive(store->mutex);
    return ms;
}

void store_set_wifi_idle(EntityStore* store, bool idle) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    store->wifi_idle_disconnected = idle;
    xSemaphoreGive(store->mutex);
}

bool store_get_wifi_idle(EntityStore* store) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    bool idle = store->wifi_idle_disconnected;
    xSemaphoreGive(store->mutex);
    return idle;
}

void store_set_ui_mode_override(EntityStore* store, UiMode mode) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    store->ui_mode_override = mode;
    xSemaphoreGive(store->mutex);

    if (store->ui_task) {
        xTaskNotifyGive(store->ui_task);
    }
}

void store_flush_pending_commands(EntityStore* store) {
    xSemaphoreTake(store->mutex, portMAX_DELAY);
    for (uint8_t entity_idx = 0; entity_idx < store->entity_count; ++entity_idx) {
        store->entities[entity_idx].command_pending = false;
    }
    xSemaphoreGive(store->mutex);
}

EntityRef store_add_entity(EntityStore* store, EntityConfig entity) {
    if (store->entity_count >= MAX_ENTITIES) {
        esp_system_abort("too many entities declared !");
    }

    uint8_t entity_id = store->entity_count++;
    store->entities[entity_id] = {
        .entity_id = entity.entity_id,
        .command_type = entity.command_type,
    };

    return EntityRef{.index = entity_id};
}