#pragma once
#include <cstdint>
#include <Arduino.h>

// HA REST API client - handles all HTTP communication with Home Assistant.
// Stateless: each call is independent. No persistent connections.

struct HAConfig {
    const char* base_url;    // e.g., "http://192.168.0.102:8123"
    const char* token;       // Long-lived access token
};

struct HAEntityState {
    char entity_id[64];
    char state[32];          // "on", "off", "52.0", etc.
    int brightness = -1;     // 0-255, -1 = not applicable
    int percentage = -1;     // 0-100, -1 = not applicable
    int current_position = -1;
    double volume_level = -1;
    char friendly_name[48];
};

class HARestClient {
public:
    HARestClient() = default;

    void begin(const HAConfig& config);

    // Test connectivity - returns HTTP status code (200 = OK)
    int testConnection();

    // Fetch state of a single entity
    bool getEntityState(const char* entity_id, HAEntityState* out);

    // Fetch all entity states (for discovery or bulk sync)
    // Calls the callback for each entity found.
    // Returns number of entities parsed, or -1 on error.
    int getAllStates(void (*callback)(const HAEntityState& state, void* ctx), void* ctx);

    // Send a service call (turn_on, turn_off, set_brightness, etc.)
    bool callService(const char* domain, const char* service,
                     const char* entity_id, const char* extra_json = nullptr);

private:
    HAConfig _config = {};
    String _buildUrl(const char* path);
    bool _doGet(const char* url, String& response);
    bool _doPost(const char* url, const String& body, String& response);
};
