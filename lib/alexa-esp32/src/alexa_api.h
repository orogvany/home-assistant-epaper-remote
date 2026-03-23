#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Standalone C++ library for controlling Alexa smart home devices
// via Amazon's unofficial consumer API.
//
// Ported from alexapy (https://gitlab.com/keatontaylor/alexapy)
// Original authors: alandtse, keatontaylor
// License: Apache-2.0

class AlexaAPI {
public:
    AlexaAPI();

    // Initialize with stored credentials. Call once after loading tokens from NVS.
    bool begin(const char *refreshToken, const char *accessToken,
               const char *cookieData, const char *customerID,
               const char *domain = "amazon.com",
               const char *locale = "en-US");

    // Refresh the access token using the refresh token.
    bool refreshAccessToken();

    // Update credentials after re-auth (token refresh + cookie exchange)
    void updateCredentials(const char *accessToken, const char *cookieData);

    // Send a text command as if typed to Alexa.
    // This goes through Alexa's NLU - works with ANY device Alexa supports.
    // deviceSerial: serial number of an Echo device to target
    // deviceType: device type string of the target Echo
    bool sendTextCommand(const char *text,
                         const char *deviceSerial,
                         const char *deviceType);

    // Direct device control via /api/phoenix/state
    bool setLightState(const char *entityId, bool powerOn,
                       int brightness = -1,
                       const char *colorName = nullptr,
                       const char *colorTemperatureName = nullptr);

    // Query device state
    String getEntityState(const char *entityId);

    // Discover Echo/hardware devices on the account
    String discoverDevices();

    // Discover smart home appliances via GraphQL
    String discoverSmartHome();

    // Poll device state — pass JSON body with stateRequests array
    String pollDeviceState(const String &entityIdsJson);

    // Health check
    bool ping();

    // Check if authenticated
    bool isAuthenticated() const { return _authenticated; }

    // Get the current access token (for debug/storage)
    const String &getAccessToken() const { return _accessToken; }
    const String &getRefreshToken() const { return _refreshToken; }

private:
    String _refreshToken;
    String _accessToken;
    String _cookies;
    String _customerID;
    String _domain;
    String _locale;
    String _csrfToken;
    String _apiBase;
    bool _authenticated;
    int _lastHttpCode;

    // Internal HTTP methods
    String _request(const char *method, const char *uri,
                    const String &body = "", const char *contentType = "application/json");
    String _get(const char *uri);
    String _post(const char *uri, const String &body);
    String _put(const char *uri, const String &body);

    // Build a behavior/sequence JSON payload
    String _buildBehaviorPayload(const char *sequenceType,
                                 JsonObject &operationPayload);

    // Discover the regional API base URL
    bool _discoverApiBase();
};
