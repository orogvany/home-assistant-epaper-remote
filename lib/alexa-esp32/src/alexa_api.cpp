#include "alexa_api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const char *TAG = "AlexaAPI";

AlexaAPI::AlexaAPI()
    : _authenticated(false), _lastHttpCode(0) {}

bool AlexaAPI::begin(const char *refreshToken, const char *accessToken,
                     const char *cookieData, const char *customerID,
                     const char *domain, const char *locale) {
    _refreshToken = refreshToken;
    _accessToken = accessToken;
    _cookies = cookieData;
    _customerID = customerID;
    _domain = domain;
    _locale = locale;

    if (!_discoverApiBase()) {
        return false;
    }

    _authenticated = true;
    return true;
}

bool AlexaAPI::refreshAccessToken() {
    Serial.printf("[api] refreshAccessToken: refresh_token(%d)\n", _refreshToken.length());

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
    Serial.printf("[api] /auth/token -> HTTP %d\n", code);
    if (code != 200) {
        String errBody = http.getString();
        Serial.printf("[api] token refresh error: %s\n", errBody.c_str());
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        Serial.printf("[api] token refresh JSON parse failed, body: %s\n", response.c_str());
        return false;
    }

    if (doc["access_token"].is<const char*>()) {
        _accessToken = doc["access_token"].as<String>();
        Serial.printf("[api] new access_token(%d)\n", _accessToken.length());
        return true;
    }

    Serial.printf("[api] no access_token in response: %s\n", response.c_str());
    return false;
}

void AlexaAPI::updateCredentials(const char *accessToken, const char *cookieData) {
    _accessToken = accessToken;
    _cookies = cookieData;
    Serial.printf("[api] credentials updated: token(%d) cookies(%d)\n",
                  _accessToken.length(), _cookies.length());
}

bool AlexaAPI::sendTextCommand(const char *text,
                               const char *deviceSerial,
                               const char *deviceType) {
    JsonDocument doc;
    JsonObject payload = doc.to<JsonObject>();
    payload["deviceType"] = deviceType;
    payload["deviceSerialNumber"] = deviceSerial;
    payload["locale"] = _locale;
    payload["customerId"] = _customerID;
    payload["skillId"] = "amzn1.ask.1p.tellalexa";
    payload["text"] = text;

    String body = _buildBehaviorPayload("Alexa.TextCommand", payload);
    _post("/api/behaviors/preview", body);
    return _lastHttpCode >= 200 && _lastHttpCode < 300;
}

bool AlexaAPI::setLightState(const char *entityId, bool powerOn,
                             int brightness,
                             const char *colorName,
                             const char *colorTemperatureName) {
    JsonDocument doc;
    JsonArray controlRequests = doc["controlRequests"].to<JsonArray>();

    JsonObject powerReq = controlRequests.add<JsonObject>();
    powerReq["entityId"] = entityId;
    powerReq["entityType"] = "ENTITY";
    JsonObject powerParams = powerReq["parameters"].to<JsonObject>();
    powerParams["action"] = powerOn ? "turnOn" : "turnOff";

    if (brightness >= 0 && brightness <= 100) {
        JsonObject brightReq = controlRequests.add<JsonObject>();
        brightReq["entityId"] = entityId;
        brightReq["entityType"] = "ENTITY";
        JsonObject brightParams = brightReq["parameters"].to<JsonObject>();
        brightParams["action"] = "setBrightness";
        brightParams["brightness"] = String(brightness);
    }

    if (colorName) {
        JsonObject colorReq = controlRequests.add<JsonObject>();
        colorReq["entityId"] = entityId;
        colorReq["entityType"] = "ENTITY";
        JsonObject colorParams = colorReq["parameters"].to<JsonObject>();
        colorParams["action"] = "setColor";
        colorParams["colorName"] = colorName;
    }

    if (colorTemperatureName) {
        JsonObject ctReq = controlRequests.add<JsonObject>();
        ctReq["entityId"] = entityId;
        ctReq["entityType"] = "ENTITY";
        JsonObject ctParams = ctReq["parameters"].to<JsonObject>();
        ctParams["action"] = "setColorTemperature";
        ctParams["colorTemperatureName"] = colorTemperatureName;
    }

    String body;
    serializeJson(doc, body);
    String response = _put("/api/phoenix/state", body);
    return response.length() > 0;
}

String AlexaAPI::getEntityState(const char *entityId) {
    JsonDocument doc;
    JsonArray stateRequests = doc["stateRequests"].to<JsonArray>();
    JsonObject req = stateRequests.add<JsonObject>();
    req["entityId"] = entityId;
    req["entityType"] = "ENTITY";

    String body;
    serializeJson(doc, body);
    return _post("/api/phoenix/state", body);
}

String AlexaAPI::discoverDevices() {
    return _get("/api/devices-v2/device");
}

static const char GQL_SMARTHOME_QUERY[] PROGMEM =
    "query CustomerSmartHome {"
    "  endpoints("
    "    endpointsQueryParams: { paginationParams: { disablePagination: true } }"
    "  ) {"
    "    items {"
    "      legacyAppliance {"
    "        applianceId"
    "        applianceTypes"
    "        friendlyName"
    "        friendlyDescription"
    "        manufacturerName"
    "        connectedVia"
    "        modelName"
    "        entityId"
    "        aliases"
    "        capabilities"
    "        customerDefinedDeviceType"
    "        driverIdentity"
    "        alexaDeviceIdentifierList"
    "      }"
    "    }"
    "  }"
    "}";

String AlexaAPI::discoverSmartHome() {
    JsonDocument doc;
    doc["query"] = GQL_SMARTHOME_QUERY;
    String body;
    serializeJson(doc, body);
    return _post("/nexus/v1/graphql", body);
}

String AlexaAPI::pollDeviceState(const String &entityIdsJson) {
    return _post("/api/phoenix/state", entityIdsJson);
}

bool AlexaAPI::ping() {
    String response = _get("/api/ping");
    return response.length() > 0;
}

// --- Private methods ---

String AlexaAPI::_buildBehaviorPayload(const char *sequenceType,
                                       JsonObject &operationPayload) {
    JsonDocument doc;
    doc["behaviorId"] = "PREVIEW";
    doc["status"] = "ENABLED";

    JsonDocument seqDoc;
    seqDoc["@type"] = "com.amazon.alexa.behaviors.model.Sequence";
    JsonObject startNode = seqDoc["startNode"].to<JsonObject>();
    startNode["@type"] = "com.amazon.alexa.behaviors.model.OpaquePayloadOperationNode";
    startNode["type"] = sequenceType;
    JsonObject opPayload = startNode["operationPayload"].to<JsonObject>();
    for (JsonPair kv : operationPayload) {
        opPayload[kv.key()] = kv.value();
    }

    String seqJson;
    serializeJson(seqDoc, seqJson);
    doc["sequenceJson"] = seqJson;

    String result;
    serializeJson(doc, result);
    return result;
}

bool AlexaAPI::_discoverApiBase() {
    _apiBase = "https://alexa." + _domain;
    Serial.printf("[api] apiBase set to %s\n", _apiBase.c_str());
    Serial.printf("[api] cookies(%d) token(%d)\n", _cookies.length(), _accessToken.length());
    return true;
}

String AlexaAPI::_request(const char *method, const char *uri,
                          const String &body, const char *contentType) {
    WiFiClientSecure client;
    client.setInsecure(); // TODO: add CA cert for production

    HTTPClient http;
    String url = _apiBase + uri;

    // Add timestamp to prevent caching
    if (String(uri).indexOf('?') >= 0) {
        url += "&_=" + String(millis());
    } else {
        url += "?_=" + String(millis());
    }

    http.begin(client, url);
    http.addHeader("Content-Type", contentType);
    http.addHeader("Cookie", _cookies);
    http.addHeader("Referer", "https://alexa." + _domain + "/spa/index.html");

    if (_csrfToken.length() > 0) {
        http.addHeader("csrf", _csrfToken);
    }
    if (_accessToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + _accessToken);
    }

    int code;
    if (strcmp(method, "GET") == 0) {
        code = http.GET();
    } else if (strcmp(method, "POST") == 0) {
        code = http.POST(body);
    } else if (strcmp(method, "PUT") == 0) {
        code = http.PUT(body);
    } else if (strcmp(method, "DELETE") == 0) {
        code = http.sendRequest("DELETE", body);
    } else {
        http.end();
        _lastHttpCode = 0;
        return "";
    }

    _lastHttpCode = code;
    Serial.printf("[api] %s %s -> HTTP %d\n", method, uri, code);

    if (code == 401) {
        Serial.printf("[api] 401 on %s, attempting token refresh\n", uri);
        http.end();
        if (refreshAccessToken()) {
            Serial.println("[api] token refresh OK, retrying");
            return _request(method, uri, body, contentType);
        }
        Serial.println("[api] token refresh FAILED");
        _authenticated = false;
        return "";
    }

    String response = http.getString();
    http.end();

    if (code < 200 || code >= 300) {
        Serial.printf("[api] error response: %s\n", response.c_str());
        return "";
    }

    return response;
}

String AlexaAPI::_get(const char *uri) {
    return _request("GET", uri);
}

String AlexaAPI::_post(const char *uri, const String &body) {
    return _request("POST", uri, body);
}

String AlexaAPI::_put(const char *uri, const String &body) {
    return _request("PUT", uri, body);
}
