#include "alexa_auth.h"
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

const char *AlexaAuth::DEVICE_TYPE = "A2IVLV5VM2W81";

static const char *NVS_NS = "alexa";

AlexaAuth::AlexaAuth() : _state(AlexaAuthState::NOT_CONFIGURED) {}

// --- NVS Storage ---

bool AlexaAuth::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, true)) return false;

    _refreshToken  = prefs.getString("refresh", "");
    _cookies       = prefs.getString("cookies", "");
    _customerID    = prefs.getString("customer", "");
    _domain        = prefs.getString("domain", "amazon.com");
    _deviceSerial  = prefs.getString("serial", "");
    _clientId      = prefs.getString("client_id", "");
    prefs.end();

    if (_refreshToken.length() > 0) {
        _state = AlexaAuthState::AUTHENTICATED;
        return true;
    }
    return false;
}

bool AlexaAuth::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, false)) return false;

    prefs.putString("refresh",   _refreshToken);
    prefs.putString("cookies",   _cookies);
    prefs.putString("customer",  _customerID);
    prefs.putString("domain",    _domain);
    prefs.putString("serial",    _deviceSerial);
    prefs.putString("client_id", _clientId);
    prefs.end();
    return true;
}

void AlexaAuth::clearNVS() {
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
        prefs.clear();
        prefs.end();
    }
    _refreshToken = "";
    _accessToken  = "";
    _cookies      = "";
    _customerID   = "";
    _state = AlexaAuthState::NOT_CONFIGURED;
}

// --- OAuth Flow ---

bool AlexaAuth::startOAuthFlow(const char *domain) {
    _domain = domain;

    if (_deviceSerial.length() == 0) {
        _deviceSerial = _generateDeviceSerial();
    }

    _codeVerifier = _generateCodeVerifier();
    _clientId = _buildClientId();

    String challenge = _computeCodeChallenge(_codeVerifier);

    _authUrl  = "https://www." + _domain + "/ap/register?";
    _authUrl += "openid.ns=http://specs.openid.net/auth/2.0";
    _authUrl += "&openid.ns.oa2=http://www.amazon.com/ap/ext/oauth/2";
    _authUrl += "&openid.oa2.response_type=code";
    _authUrl += "&openid.oa2.code_challenge_method=S256";
    _authUrl += "&openid.oa2.code_challenge=" + challenge;
    _authUrl += "&openid.oa2.client_id=device:" + _clientId;
    _authUrl += "&openid.oa2.scope=device_auth_access+offline_access";
    _authUrl += "&openid.return_to=https://www.amazon.com/ap/maplanding";
    _authUrl += "&openid.assoc_handle=amzn_dp_project_dee_ios";
    _authUrl += "&openid.claimed_id=http://specs.openid.net/auth/2.0/identifier_select";
    _authUrl += "&openid.identity=http://specs.openid.net/auth/2.0/identifier_select";
    _authUrl += "&openid.mode=checkid_setup";
    _authUrl += "&openid.ns.pape=http://specs.openid.net/extensions/pape/1.0";
    _authUrl += "&openid.pape.max_auth_age=0";
    _authUrl += "&accountStatusPolicy=P1";
    _authUrl += "&pageId=amzn_dp_project_dee_ios";
    _authUrl += "&language=en_US";

    saveToNVS();
    _state = AlexaAuthState::WAITING_FOR_AUTH;
    return true;
}

const String &AlexaAuth::getAuthUrl() const {
    return _authUrl;
}

bool AlexaAuth::handleAuthCallback(const char *authCode) {
    _lastError = "";

    if (!_registerDevice(authCode)) {
        Serial.printf("[auth] registerDevice failed: %s\n", _lastError.c_str());
        _state = AlexaAuthState::AUTH_FAILED;
        return false;
    }
    Serial.println("[auth] registerDevice OK");

    if (!_exchangeTokensForCookies()) {
        if (_lastError.length() == 0) _lastError = "Cookie exchange failed";
        Serial.printf("[auth] exchangeTokens failed: %s\n", _lastError.c_str());
        _state = AlexaAuthState::AUTH_FAILED;
        return false;
    }
    Serial.println("[auth] exchangeTokens OK");

    if (!_fetchCustomerID()) {
        Serial.printf("[auth] fetchCustomerID failed: %s\n", _lastError.c_str());
        Serial.println("[auth] proceeding without customer ID — tokens are valid");
    } else {
        Serial.printf("[auth] customerID=%s\n", _customerID.c_str());
    }

    saveToNVS();
    _state = AlexaAuthState::AUTHENTICATED;
    return true;
}

const String &AlexaAuth::getLastError() const { return _lastError; }

// --- Token Management ---

bool AlexaAuth::refreshAccessToken() {
    Serial.printf("[auth] refreshAccessToken: refresh_token(%d)\n", _refreshToken.length());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://api." + _domain + "/auth/token";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "app_name=HomeControl"
                  "&source_token=" + _refreshToken +
                  "&source_token_type=refresh_token"
                  "&requested_token_type=access_token";

    int code = http.POST(body);
    Serial.printf("[auth] /auth/token -> HTTP %d\n", code);
    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("[auth] token refresh error: %s\n", errBody.c_str());
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        Serial.printf("[auth] token refresh JSON parse failed, body: %s\n", response.c_str());
        return false;
    }

    if (doc["access_token"].is<const char*>()) {
        _accessToken = doc["access_token"].as<String>();
        Serial.printf("[auth] new access_token(%d)\n", _accessToken.length());
        return true;
    }
    Serial.printf("[auth] no access_token in response: %s\n", response.c_str());
    return false;
}

bool AlexaAuth::exchangeCookies() {
    return _exchangeTokensForCookies();
}

bool AlexaAuth::refreshCredentials() {
    Serial.println("[auth] refreshCredentials: refreshing token + cookies");
    if (!refreshAccessToken()) {
        Serial.println("[auth] access token refresh failed");
        return false;
    }
    if (!_exchangeTokensForCookies(true)) {
        Serial.println("[auth] cookie exchange failed");
        return false;
    }
    saveToNVS();
    Serial.printf("[auth] credentials refreshed: token(%d) cookies(%d)\n",
                  _accessToken.length(), _cookies.length());
    return true;
}

// --- State ---

AlexaAuthState AlexaAuth::getState() const { return _state; }

bool AlexaAuth::hasValidTokens() const {
    return _refreshToken.length() > 0 && _cookies.length() > 0;
}

const String &AlexaAuth::getRefreshToken() const { return _refreshToken; }
const String &AlexaAuth::getAccessToken() const  { return _accessToken; }
const String &AlexaAuth::getCookies() const      { return _cookies; }
const String &AlexaAuth::getCustomerID() const   { return _customerID; }
const String &AlexaAuth::getDomain() const       { return _domain; }

// --- Private: Crypto ---

String AlexaAuth::_generateDeviceSerial() {
    String serial;
    serial.reserve(32);
    for (int i = 0; i < 32; i++) {
        serial += String(esp_random() % 16, HEX);
    }
    serial.toUpperCase();
    return serial;
}

String AlexaAuth::_generateCodeVerifier() {
    uint8_t randomBytes[32];
    for (int i = 0; i < 32; i++) {
        randomBytes[i] = (uint8_t)(esp_random() & 0xFF);
    }

    uint8_t b64[64];
    size_t olen;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, randomBytes, 32);

    String verifier;
    verifier.reserve(olen);
    for (size_t i = 0; i < olen; i++) {
        char c = (char)b64[i];
        if (c == '+')      verifier += '-';
        else if (c == '/') verifier += '_';
        else if (c == '=') continue;
        else               verifier += c;
    }
    return verifier;
}

String AlexaAuth::_computeCodeChallenge(const String &verifier) {
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)verifier.c_str(), verifier.length(), hash, 0);

    uint8_t b64[64];
    size_t olen;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, hash, 32);

    String challenge;
    challenge.reserve(olen);
    for (size_t i = 0; i < olen; i++) {
        char c = (char)b64[i];
        if (c == '+')      challenge += '-';
        else if (c == '/') challenge += '_';
        else if (c == '=') continue;
        else               challenge += c;
    }
    return challenge;
}

String AlexaAuth::_buildClientId() {
    String hashAndType = String("#") + DEVICE_TYPE;
    String hexSuffix;
    hexSuffix.reserve(hashAndType.length() * 2);
    for (size_t i = 0; i < hashAndType.length(); i++) {
        char buf[3];
        sprintf(buf, "%02x", (uint8_t)hashAndType[i]);
        hexSuffix += buf;
    }

    String concat = _deviceSerial + hexSuffix;
    String hexId;
    hexId.reserve(concat.length() * 2);
    for (size_t i = 0; i < concat.length(); i++) {
        char buf[3];
        sprintf(buf, "%02x", (uint8_t)concat[i]);
        hexId += buf;
    }
    return hexId;
}

// --- Private: Registration ---

String AlexaAuth::_generateFrc() {
    uint8_t randomBytes[313];
    for (int i = 0; i < 313; i++) {
        randomBytes[i] = (uint8_t)(esp_random() & 0xFF);
    }
    uint8_t b64[512];
    size_t olen;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, randomBytes, 313);
    while (olen > 0 && b64[olen - 1] == '=') olen--;
    return String((char *)b64, olen);
}

bool AlexaAuth::_registerDevice(const char *authCode) {
    Serial.printf("[auth] register: code=%s serial=%s\n", authCode, _deviceSerial.c_str());
    Serial.printf("[auth] clientId(%d)=%s\n", _clientId.length(), _clientId.c_str());
    Serial.printf("[auth] verifier(%d)=%s\n", _codeVerifier.length(), _codeVerifier.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://api." + _domain + "/auth/register";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    JsonObject authData = doc["auth_data"].to<JsonObject>();
    authData["authorization_code"] = authCode;
    authData["client_domain"]      = "DeviceLegacy";
    authData["client_id"]          = _clientId;
    authData["code_algorithm"]     = "SHA-256";
    authData["code_verifier"]      = _codeVerifier;

    JsonObject cookies = doc["cookies"].to<JsonObject>();
    cookies["domain"] = "." + _domain;
    JsonArray websiteCookiesArr = cookies["website_cookies"].to<JsonArray>();
    (void)websiteCookiesArr;

    JsonObject regData = doc["registration_data"].to<JsonObject>();
    regData["app_name"]         = "HomeControl";
    regData["app_version"]      = "2.2.556530.0";
    regData["device_model"]     = "iPhone";
    regData["device_name"]      = "%FIRST_NAME%'s%DUPE_STRATEGY_1ST%HomeControl";
    regData["device_serial"]    = _deviceSerial;
    regData["device_type"]      = DEVICE_TYPE;
    regData["domain"]           = "Device";
    regData["os_version"]       = "16.6";
    regData["software_version"] = "1";

    JsonArray requestedTokens = doc["requested_token_type"].to<JsonArray>();
    requestedTokens.add("bearer");
    requestedTokens.add("mac_dms");
    requestedTokens.add("website_cookies");

    JsonArray requestedExtensions = doc["requested_extensions"].to<JsonArray>();
    requestedExtensions.add("device_info");
    requestedExtensions.add("customer_info");

    JsonObject userCtx = doc["user_context_map"].to<JsonObject>();
    userCtx["frc"] = _generateFrc();

    String body;
    serializeJson(doc, body);

    Serial.printf("[auth] POST %s (%d bytes)\n", url.c_str(), body.length());
    int code = http.POST(body);
    Serial.printf("[auth] register response: HTTP %d\n", code);

    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("[auth] register error body: %s\n", errBody.c_str());
        _lastError = "Registration HTTP " + String(code) + ": " + errBody;
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();
    Serial.printf("[auth] register response (%d bytes)\n", response.length());

    JsonDocument respDoc;
    if (deserializeJson(respDoc, response)) {
        _lastError = "Failed to parse registration response";
        Serial.printf("[auth] register JSON parse failed, body: %s\n", response.c_str());
        return false;
    }

    JsonObject success = respDoc["response"]["success"];
    if (success.isNull()) {
        _lastError = "No success in response: " + response;
        Serial.printf("[auth] no success key: %s\n", response.c_str());
        return false;
    }

    JsonObject tokens = success["tokens"];

    JsonObject bearer = tokens["bearer"];
    if (!bearer.isNull()) {
        _accessToken  = bearer["access_token"].as<String>();
        _refreshToken = bearer["refresh_token"].as<String>();
        Serial.printf("[auth] got tokens: access(%d) refresh(%d)\n",
                      _accessToken.length(), _refreshToken.length());
    } else {
        Serial.println("[auth] no bearer token in response");
    }

    JsonArray websiteCookies = tokens["website_cookies"];
    if (!websiteCookies.isNull()) {
        String cookieStr;
        for (JsonObject cookie : websiteCookies) {
            if (cookieStr.length() > 0) cookieStr += "; ";
            cookieStr += cookie["Name"].as<String>();
            cookieStr += "=";
            cookieStr += cookie["Value"].as<String>();
        }
        _cookies = cookieStr;
        Serial.printf("[auth] got cookies(%d)\n", _cookies.length());
    } else {
        Serial.println("[auth] no website_cookies in response");
    }

    return _refreshToken.length() > 0;
}

bool AlexaAuth::_exchangeTokensForCookies(bool force) {
    if (!force && _cookies.length() > 0) return true;

    Serial.printf("[auth] exchangeTokensForCookies (force=%d)\n", force);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://www." + _domain + "/ap/exchangetoken/cookies";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "requested_token_type=auth_cookies"
                  "&app_name=HomeControl"
                  "&domain=www." + _domain +
                  "&source_token_type=refresh_token"
                  "&source_token=" + _refreshToken;

    int code = http.POST(body);
    Serial.printf("[auth] exchangetoken -> HTTP %d\n", code);
    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("[auth] exchangetoken error: %s\n", errBody.c_str());
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        Serial.printf("[auth] exchangetoken JSON parse failed, body: %s\n", response.c_str());
        return false;
    }

    JsonObject cookiesByDomain = doc["response"]["tokens"]["cookies"];
    if (cookiesByDomain.isNull()) {
        Serial.printf("[auth] no cookies in exchangetoken response: %s\n",
                      response.c_str());
        return false;
    }

    String cookieStr;
    for (JsonPair domain : cookiesByDomain) {
        for (JsonObject cookie : domain.value().as<JsonArray>()) {
            if (cookieStr.length() > 0) cookieStr += "; ";
            cookieStr += cookie["Name"].as<String>();
            cookieStr += "=";
            cookieStr += cookie["Value"].as<String>();
        }
    }

    _cookies = cookieStr;
    Serial.printf("[auth] got fresh cookies(%d)\n", _cookies.length());
    return _cookies.length() > 0;
}

bool AlexaAuth::_fetchCustomerID() {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://alexa." + _domain + "/api/users/me";
    Serial.printf("[auth] GET %s\n", url.c_str());
    Serial.printf("[auth] cookies(%d), token(%d)\n", _cookies.length(), _accessToken.length());
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Cookie", _cookies);
    http.addHeader("Referer", "https://alexa." + _domain + "/spa/index.html");
    if (_accessToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + _accessToken);
    }

    int code = http.GET();
    Serial.printf("[auth] /api/users/me HTTP %d\n", code);
    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("[auth] users/me error: %s\n", errBody.c_str());
        _lastError = "Fetch customer ID HTTP " + String(code) + ": " + errBody;
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();
    Serial.printf("[auth] users/me response: %s\n", response.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        _lastError = "Failed to parse /api/users/me response";
        return false;
    }

    const char *idFields[] = {"customerId", "id", "directedId", "personIdV2"};
    for (const char *field : idFields) {
        if (doc[field].is<const char*>()) {
            _customerID = doc[field].as<String>();
            Serial.printf("[auth] found customerID via '%s': %s\n", field, _customerID.c_str());
            return true;
        }
    }

    _lastError = "No customer ID field found in /api/users/me response";
    return false;
}
