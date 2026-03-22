#pragma once
#include <cstdint>

// Generic device types - shared across all backends (HA, Alexa, etc.)
enum class DeviceType : uint8_t {
    LIGHT,
    SWITCH,
    FAN,
    COVER,
    MEDIA_PLAYER,
    LOCK,
    VACUUM,
    SCENE,
    SCRIPT,
    INPUT_NUMBER,
    INPUT_BOOLEAN,
    AUTOMATION,
    OTHER,
};

// Widget types for UI rendering
enum class WidgetType : uint8_t {
    SLIDER,
    BUTTON,
};

// Generic device state - what the UI cares about
struct DeviceState {
    bool is_on = false;
    uint8_t value = 0;       // 0-100 for dimmers/sliders, 0/1 for toggles
    bool reachable = true;
};

// Device action interface - implemented by each backend (HA REST, Alexa, etc.)
// This is the contract between the UI layer and the backend.
class DeviceAction {
public:
    virtual ~DeviceAction() = default;

    // Send a command to the device. Returns true if the call succeeded.
    virtual bool sendCommand(uint8_t value) = 0;

    // Poll the current state from the backend.
    virtual DeviceState pollState() = 0;

    // Get the entity/device identifier (e.g., "light.living_room" for HA)
    virtual const char* getEntityId() = 0;

    // Get the device type
    virtual DeviceType getDeviceType() = 0;

    // Get a friendly name for display
    virtual const char* getFriendlyName() = 0;
};
