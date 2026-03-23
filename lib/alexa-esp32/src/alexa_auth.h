#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Authentication for Amazon's unofficial Alexa API.
// Implements the app-mimicking registration flow (same as alexapy).
//
// Flow:
//   1. Call startOAuthFlow() — generates PKCE + OAuth URL
//   2. User opens getAuthUrl() in their browser and logs in to Amazon
//   3. Amazon redirects to maplanding page with auth code in URL
//   4. User pastes the maplanding URL back into the device's web UI
//   5. Call handleAuthCallback(authCode) — exchanges code for tokens + cookies
//   6. Pass credentials to AlexaAPI::begin()
//
// On subsequent boots, call loadFromNVS() to restore stored credentials.

enum class AlexaAuthState {
    NOT_CONFIGURED,
    WAITING_FOR_AUTH,
    AUTHENTICATED,
    AUTH_FAILED
};

class AlexaAuth {
public:
    AlexaAuth();

    // NVS Storage
    bool loadFromNVS();
    bool saveToNVS();
    void clearNVS();

    // OAuth Flow
    bool startOAuthFlow(const char *domain);
    const String &getAuthUrl() const;
    bool handleAuthCallback(const char *authCode);

    // Token Management
    bool refreshAccessToken();
    bool exchangeCookies();
    bool refreshCredentials();

    // State
    AlexaAuthState getState() const;
    bool hasValidTokens() const;
    const String &getLastError() const;

    // Credential Getters (pass to AlexaAPI::begin())
    const String &getRefreshToken() const;
    const String &getAccessToken() const;
    const String &getCookies() const;
    const String &getCustomerID() const;
    const String &getDomain() const;

    static const char *DEVICE_TYPE;

private:
    AlexaAuthState _state;
    String _domain;
    String _deviceSerial;
    String _clientId;
    String _codeVerifier;
    String _authUrl;

    String _refreshToken;
    String _accessToken;
    String _cookies;
    String _customerID;
    String _lastError;

    String _generateDeviceSerial();
    String _generateCodeVerifier();
    String _computeCodeChallenge(const String &verifier);
    String _buildClientId();

    String _generateFrc();
    bool _registerDevice(const char *authCode);
    bool _exchangeTokensForCookies(bool force = false);
    bool _fetchCustomerID();
};
