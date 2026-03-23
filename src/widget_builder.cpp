#include "widget_builder.h"
#include "assets/icons.h"
#include "boards.h"
#include "entity_value.h"
#include "esp_log.h"
#include "widgets/WeatherWidget.h"
#include <cstring>

static const char* TAG = "widget_builder";

// Map icon name string to compiled icon array
static const uint8_t* icon_by_name(const char* name) {
    if (strcmp(name, "fan") == 0) return btn_fan;
    if (strcmp(name, "fan_off") == 0) return btn_fan_off;
    if (strcmp(name, "lightbulb_outline") == 0) return btn_lightbulb_outline;
    if (strcmp(name, "lightbulb_off_outline") == 0) return btn_lightbulb_off_outline;
    if (strcmp(name, "robot_outline") == 0) return btn_robot_outline;
    if (strcmp(name, "robot_off_outline") == 0) return btn_robot_off_outline;
    return btn_lightbulb_outline; // Fallback
}

// Default icon mapping by domain
static const char* default_icon_on(const char* domain) {
    if (strcmp(domain, "light") == 0) return "lightbulb_outline";
    if (strcmp(domain, "fan") == 0) return "fan";
    if (strcmp(domain, "vacuum") == 0) return "robot_outline";
    // TODO: add switch/plug, cover/blinds, lock, media_player/speaker icons
    return "lightbulb_outline"; // Generic fallback
}

static const char* default_icon_off(const char* domain) {
    if (strcmp(domain, "light") == 0) return "lightbulb_off_outline";
    if (strcmp(domain, "fan") == 0) return "fan_off";
    if (strcmp(domain, "vacuum") == 0) return "robot_off_outline";
    return "lightbulb_off_outline";
}

// Map entity_id domain to CommandType
static CommandType domain_to_command_type(const char* entity_id, const char* widget_type) {
    // Extract domain from "domain.name"
    char domain[16] = {};
    const char* dot = strchr(entity_id, '.');
    if (dot) {
        size_t len = dot - entity_id;
        if (len >= sizeof(domain)) len = sizeof(domain) - 1;
        memcpy(domain, entity_id, len);
    }

    if (strcmp(domain, "light") == 0) {
        return strcmp(widget_type, "slider") == 0
            ? CommandType::SetLightBrightnessPercentage
            : CommandType::SwitchOnOff;
    }
    if (strcmp(domain, "switch") == 0) return CommandType::SwitchOnOff;
    if (strcmp(domain, "fan") == 0) return CommandType::SetFanSpeedPercentage;
    if (strcmp(domain, "cover") == 0) return CommandType::SetCoverPosition;
    if (strcmp(domain, "scene") == 0) return CommandType::ActivateScene;
    if (strcmp(domain, "script") == 0) return CommandType::RunScript;
    if (strcmp(domain, "lock") == 0) return CommandType::LockUnlock;
    if (strcmp(domain, "media_player") == 0) {
        return strcmp(widget_type, "slider") == 0
            ? CommandType::SetMediaPlayerVolume
            : CommandType::MediaPlayerPlayPause;
    }
    if (strcmp(domain, "input_number") == 0) return CommandType::SetInputNumber;
    if (strcmp(domain, "input_boolean") == 0) return CommandType::InputBooleanToggle;
    if (strcmp(domain, "automation") == 0) return CommandType::AutomationOnOff;
    if (strcmp(domain, "vacuum") == 0) return CommandType::VacuumCommand;

    return CommandType::SwitchOnOff; // Fallback
}

int build_widgets_from_config(const AppConfig& app, EntityStore* store, Screen* screen) {
    if (app.ui_device_count == 0) return 0;

    // Auto-layout: stack widgets vertically
    constexpr uint16_t margin = 30;
    constexpr uint16_t slider_height = 170;
    constexpr uint16_t button_spacing = 140; // Button height + gap
    constexpr uint16_t slider_spacing = slider_height + 40;
    uint16_t y = margin;

    constexpr uint16_t weather_height = 160;
    constexpr uint16_t weather_spacing = weather_height + 20;

    int count = 0;
    for (int i = 0; i < app.ui_device_count && i < MAX_WIDGETS_PER_SCREEN; i++) {
        const UIDevice& dev = app.ui_devices[i];
        bool is_slider = (strcmp(dev.widget_type, "slider") == 0);
        bool is_weather = (strcmp(dev.widget_type, "weather") == 0);
        bool is_alexa = (strcmp(dev.source, "alexa") == 0);

        if (is_weather) {
            EntityConfig entity = {
                .entity_id = dev.entity_id,
                .command_type = CommandType::SwitchOnOff,
                .value_type = EntityValueType::Weather,
                .source = is_alexa ? EntitySource::Alexa : EntitySource::HomeAssistant,
                .read_only = true,
            };

            screen_add_weather(
                WeatherWidgetConfig{
                    .entity_ref = store_add_entity(store, entity),
                    .label = dev.label,
                    .pos_x = margin,
                    .pos_y = y,
                    .width = (uint16_t)(DISPLAY_WIDTH - 2 * margin),
                    .height = weather_height,
                },
                screen);
            y += weather_spacing;
        } else {
            CommandType cmd_type;
            if (is_alexa) {
                cmd_type = is_slider ? CommandType::AlexaSetBrightness : CommandType::AlexaSetPower;
            } else {
                cmd_type = domain_to_command_type(dev.entity_id, dev.widget_type);
            }

            EntityConfig entity = {
                .entity_id = dev.entity_id,
                .command_type = cmd_type,
                .source = is_alexa ? EntitySource::Alexa : EntitySource::HomeAssistant,
            };

            // Extract domain for default icon lookup
            char domain[16] = {};
            const char* dot = strchr(dev.entity_id, '.');
            if (dot) { size_t len = dot - dev.entity_id; if (len >= sizeof(domain)) len = sizeof(domain)-1; memcpy(domain, dev.entity_id, len); }

            const uint8_t* ico_on = icon_by_name(dev.icon_on[0] ? dev.icon_on : default_icon_on(domain));
            const uint8_t* ico_off = icon_by_name(dev.icon_off[0] ? dev.icon_off : default_icon_off(domain));

            if (is_slider) {
                screen_add_slider(
                    SliderConfig{
                        .entity_ref = store_add_entity(store, entity),
                        .label = dev.label,
                        .icon_on = ico_on,
                        .icon_off = ico_off,
                        .pos_x = margin,
                        .pos_y = y,
                        .width = (uint16_t)(DISPLAY_WIDTH - 2 * margin),
                        .height = slider_height,
                    },
                    screen);
                y += slider_spacing;
            } else {
                screen_add_button(
                    ButtonConfig{
                        .entity_ref = store_add_entity(store, entity),
                        .label = dev.label,
                        .icon_on = ico_on,
                        .icon_off = ico_off,
                        .pos_x = margin,
                        .pos_y = y,
                    },
                    screen);
                y += button_spacing;
            }
        }

        ESP_LOGI(TAG, "Widget %d: %s (%s) at y=%d", count, dev.entity_id, dev.widget_type, y);
        count++;
    }

    ESP_LOGI(TAG, "Built %d widgets from config", count);
    return count;
}
