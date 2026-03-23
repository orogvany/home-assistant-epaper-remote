#pragma once
#include "constants.h"
#include "entity_ref.h"
#include "entity_value.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "screen.h"
#include "ui_state.h"
#include <cstdint>

enum class EntitySource : uint8_t {
    HomeAssistant,
    Alexa,
};

enum class CommandType : uint8_t {
    SetLightBrightnessPercentage,
    SetFanSpeedPercentage,
    SwitchOnOff,
    AutomationOnOff,
    SetCoverPosition,
    ActivateScene,
    RunScript,
    LockUnlock,
    SetMediaPlayerVolume,
    MediaPlayerPlayPause,
    SetInputNumber,
    InputBooleanToggle,
    VacuumCommand,
    AlexaSetPower,
    AlexaSetBrightness,
};

struct HomeAssistantEntity {
    const char* entity_id;
    CommandType command_type;
    EntitySource source = EntitySource::HomeAssistant;
    EntityValue current;
    uint8_t command_value;
    bool command_pending;
    bool read_only = false;
};

struct EntityConfig {
    const char* entity_id;
    CommandType command_type;
    EntityValueType value_type = EntityValueType::Toggle;
    EntitySource source = EntitySource::HomeAssistant;
    bool read_only = false;
};

enum class ConnState : uint8_t {
    Initializing,
    InvalidCredentials,
    ConnectionError,
    Up,
};

struct BatteryState {
    uint16_t voltage_mv = 0;
    uint8_t percentage = 0;
    bool charging = false;
};

struct EntityStore {
    ConnState wifi = ConnState::Initializing;
    ConnState home_assistant = ConnState::Initializing;
    ConnState alexa = ConnState::Initializing;
    BatteryState battery;

    HomeAssistantEntity entities[MAX_ENTITIES];
    uint8_t entity_count;

    uint32_t last_touch_ms = 0;
    bool wifi_idle_disconnected = false;
    UiMode ui_mode_override = UiMode::Blank; // Blank = no override, use normal logic
    bool touch_ready = false;
    uint8_t pin_digits_entered = 0;
    bool pin_wrong = false;
    bool alexa_enabled = false;

    SemaphoreHandle_t mutex;
    TaskHandle_t home_assistant_task;
    TaskHandle_t alexa_task;
    TaskHandle_t ui_task;
    EventGroupHandle_t event_group = nullptr;
};

struct Command {
    CommandType type;
    const char* entity_id;
    uint8_t entity_idx;
    uint8_t value;
};

constexpr EventBits_t BIT_WIFI_UP = (1 << 0);

void store_init(EntityStore* store);
void store_set_wifi_state(EntityStore* store, ConnState state);
void store_set_hass_state(EntityStore* store, ConnState state);
void store_set_alexa_state(EntityStore* store, ConnState state);
void store_update_value(EntityStore* store, uint8_t entity_idx, uint8_t value);
void store_update_weather(EntityStore* store, uint8_t entity_idx, const WeatherState& weather);
void store_send_command(EntityStore* store, uint8_t entity_idx, uint8_t value);
bool store_get_pending_command(EntityStore* store, Command* command);
void store_ack_pending_command(EntityStore* store, const Command* command);
void store_update_ui_state(EntityStore* store, const Screen* screen, UIState* ui_state);
void store_wait_for_wifi_up(EntityStore* store);
void store_flush_pending_commands(EntityStore* store);
void store_set_battery(EntityStore* store, uint16_t voltage_mv, uint8_t percentage, bool charging);
void store_set_last_touch(EntityStore* store, uint32_t ms);
void store_set_ui_mode_override(EntityStore* store, UiMode mode);
uint32_t store_get_last_touch(EntityStore* store);
void store_set_wifi_idle(EntityStore* store, bool idle);
bool store_get_wifi_idle(EntityStore* store);
EntityRef store_add_entity(EntityStore* store, EntityConfig entity);