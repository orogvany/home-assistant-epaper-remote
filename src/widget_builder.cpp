#include "widget_builder.h"
#include "assets/icons.h"
#include "boards.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "widget_builder";

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

    int count = 0;
    for (int i = 0; i < app.ui_device_count && i < MAX_WIDGETS_PER_SCREEN; i++) {
        const UIDevice& dev = app.ui_devices[i];
        bool is_slider = (strcmp(dev.widget_type, "slider") == 0);

        EntityConfig entity = {
            .entity_id = dev.entity_id,
            .command_type = domain_to_command_type(dev.entity_id, dev.widget_type),
        };

        if (is_slider) {
            screen_add_slider(
                SliderConfig{
                    .entity_ref = store_add_entity(store, entity),
                    .label = dev.label,
                    .icon_on = lightbulb_outline,
                    .icon_off = lightbulb_off_outline,
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
                    .icon_on = lightbulb_outline,
                    .icon_off = lightbulb_off_outline,
                    .pos_x = margin,
                    .pos_y = y,
                },
                screen);
            y += button_spacing;
        }

        ESP_LOGI(TAG, "Widget %d: %s (%s) at y=%d",
                 count, dev.entity_id, dev.widget_type, y - (is_slider ? slider_spacing : button_spacing));
        count++;
    }

    ESP_LOGI(TAG, "Built %d widgets from config", count);
    return count;
}
