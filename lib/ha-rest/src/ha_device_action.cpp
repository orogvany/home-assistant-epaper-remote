#include "ha_device_action.h"
#include <cstring>
#include <cstdio>

HADeviceAction::HADeviceAction(HARestClient* client, const char* entity_id,
                               DeviceType type, const char* friendly_name)
    : _client(client), _type(type) {
    strlcpy(_entity_id, entity_id, sizeof(_entity_id));
    strlcpy(_friendly_name, friendly_name, sizeof(_friendly_name));
}

const char* HADeviceAction::_getDomain() {
    // entity_id format: "domain.name" - return everything before the dot
    static char domain[32];
    const char* dot = strchr(_entity_id, '.');
    if (dot) {
        size_t len = dot - _entity_id;
        if (len >= sizeof(domain)) len = sizeof(domain) - 1;
        memcpy(domain, _entity_id, len);
        domain[len] = '\0';
    } else {
        strlcpy(domain, _entity_id, sizeof(domain));
    }
    return domain;
}

bool HADeviceAction::sendCommand(uint8_t value) {
    const char* domain = _getDomain();
    char extra[64] = {};

    switch (_type) {
    case DeviceType::LIGHT:
        if (value == 0) {
            return _client->callService(domain, "turn_off", _entity_id);
        }
        snprintf(extra, sizeof(extra), "\"brightness_pct\":%d", value);
        return _client->callService(domain, "turn_on", _entity_id, extra);

    case DeviceType::FAN:
        if (value == 0) {
            return _client->callService(domain, "turn_off", _entity_id);
        }
        snprintf(extra, sizeof(extra), "\"percentage\":%d", value);
        return _client->callService(domain, "set_percentage", _entity_id, extra);

    case DeviceType::SWITCH:
    case DeviceType::AUTOMATION:
    case DeviceType::INPUT_BOOLEAN:
        return _client->callService(domain,
            value == 0 ? "turn_off" : "turn_on", _entity_id);

    case DeviceType::COVER:
        snprintf(extra, sizeof(extra), "\"position\":%d", value);
        return _client->callService(domain, "set_cover_position", _entity_id, extra);

    case DeviceType::MEDIA_PLAYER:
        if (_type == DeviceType::MEDIA_PLAYER && value <= 1) {
            return _client->callService(domain,
                value == 0 ? "media_pause" : "media_play", _entity_id);
        }
        snprintf(extra, sizeof(extra), "\"volume_level\":%.2f", value / 100.0);
        return _client->callService(domain, "volume_set", _entity_id, extra);

    case DeviceType::LOCK:
        return _client->callService(domain,
            value == 0 ? "unlock" : "lock", _entity_id);

    case DeviceType::SCENE:
    case DeviceType::SCRIPT:
        return _client->callService(domain, "turn_on", _entity_id);

    case DeviceType::INPUT_NUMBER:
        snprintf(extra, sizeof(extra), "\"value\":%d", value);
        return _client->callService(domain, "set_value", _entity_id, extra);

    case DeviceType::VACUUM:
        if (value == 0) return _client->callService(domain, "stop", _entity_id);
        if (value == 1) return _client->callService(domain, "start", _entity_id);
        return _client->callService(domain, "return_to_base", _entity_id);

    default:
        return false;
    }
}

DeviceState HADeviceAction::_parseState(const HAEntityState& ha) {
    DeviceState ds;

    // Check string state values
    if (strcmp(ha.state, "on") == 0 || strcmp(ha.state, "locked") == 0 ||
        strcmp(ha.state, "playing") == 0 || strcmp(ha.state, "open") == 0) {
        ds.is_on = true;
    } else if (strcmp(ha.state, "off") == 0 || strcmp(ha.state, "unlocked") == 0 ||
               strcmp(ha.state, "paused") == 0 || strcmp(ha.state, "idle") == 0 ||
               strcmp(ha.state, "closed") == 0) {
        ds.is_on = false;
    } else {
        // Try numeric state (input_number: "52.0")
        char* endptr;
        double numeric = strtod(ha.state, &endptr);
        if (endptr != ha.state && *endptr == '\0') {
            ds.is_on = true;
            ds.value = (uint8_t)numeric;
            return ds;
        }
    }

    // Determine value from attributes
    if (ha.brightness >= 0) {
        ds.value = ha.brightness * 100 / 255;
    } else if (ha.percentage >= 0) {
        ds.value = ha.percentage;
    } else if (ha.current_position >= 0) {
        ds.value = ha.current_position;
    } else if (ha.volume_level >= 0) {
        ds.value = (uint8_t)(ha.volume_level * 100);
    } else if (ds.is_on) {
        ds.value = 1; // Binary on/off with no analog value
    }

    return ds;
}

DeviceState HADeviceAction::pollState() {
    HAEntityState ha_state = {};
    if (_client->getEntityState(_entity_id, &ha_state)) {
        return _parseState(ha_state);
    }
    // On failure, return unreachable state
    DeviceState ds;
    ds.reachable = false;
    return ds;
}
