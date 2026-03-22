#include "config_server.h"
#include "esp_log.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static const char* TAG = "config_server";
static WebServer* server = nullptr;
static ConfigStore* store = nullptr;

// Embedded HTML page - served at /
static const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>HA Remote Config</title>
    <style>
        body { font-family: -apple-system, sans-serif; max-width: 600px; margin: 0 auto; padding: 20px; background: #f5f5f5; }
        h1 { color: #333; }
        h2 { color: #555; margin-top: 30px; border-bottom: 1px solid #ddd; padding-bottom: 8px; }
        label { display: block; margin-top: 12px; font-weight: bold; color: #444; }
        input, select { width: 100%; padding: 10px; margin-top: 4px; border: 1px solid #ccc; border-radius: 6px; font-size: 16px; box-sizing: border-box; }
        button { background: #2196F3; color: white; border: none; padding: 14px 28px; border-radius: 6px; font-size: 16px; cursor: pointer; margin-top: 20px; width: 100%; }
        button:hover { background: #1976D2; }
        .status { padding: 10px; margin-top: 10px; border-radius: 6px; }
        .success { background: #c8e6c9; color: #2e7d32; }
        .error { background: #ffcdd2; color: #c62828; }
    </style>
</head>
<body>
    <h1>HA Remote Configuration</h1>

    <h2>Home Assistant</h2>
    <label>HA URL</label>
    <input type="text" id="ha_url" placeholder="http://192.168.0.102:8123">
    <label>Access Token</label>
    <input type="text" id="ha_token" placeholder="Long-lived access token">

    <h2>Polling</h2>
    <label>Poll Interval (seconds)</label>
    <input type="number" id="poll_interval" value="10" min="1" max="300">

    <h2>Power Management</h2>
    <label>WiFi Idle Disconnect (minutes)</label>
    <input type="number" id="idle_disconnect" value="5" min="1" max="60">

    <button onclick="saveConfig()">Save Settings</button>
    <div id="config-status"></div>

    <h2>Devices</h2>
    <button onclick="discoverDevices()">Discover HA Devices</button>
    <div id="device-list"></div>
    <button onclick="saveDevices()" style="display:none" id="save-devices-btn">Save Device Selection</button>
    <div id="device-status"></div>

    <h2>Active Devices</h2>
    <div id="active-devices">Loading...</div>

    <script>
        let discoveredDevices = [];
        let activeDevices = [];

        fetch('/api/config')
            .then(r => r.json())
            .then(cfg => {
                document.getElementById('ha_url').value = cfg.ha_url || '';
                document.getElementById('ha_token').value = cfg.ha_token || '';
                document.getElementById('poll_interval').value = (cfg.poll_interval_ms || 10000) / 1000;
                document.getElementById('idle_disconnect').value = (cfg.idle_wifi_disconnect_ms || 300000) / 60000;
            })
            .catch(e => showStatus('config-status', 'Failed to load config', true));

        fetch('/api/ha/devices')
            .then(r => r.json())
            .then(data => {
                activeDevices = data.devices || [];
                renderActiveDevices();
            })
            .catch(e => { document.getElementById('active-devices').textContent = 'None configured'; });

        function saveConfig() {
            const cfg = {
                ha_url: document.getElementById('ha_url').value,
                ha_token: document.getElementById('ha_token').value,
                poll_interval_ms: parseInt(document.getElementById('poll_interval').value) * 1000,
                idle_wifi_disconnect_ms: parseInt(document.getElementById('idle_disconnect').value) * 60000
            };
            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(cfg)
            })
            .then(r => r.json())
            .then(res => showStatus('config-status', res.message || 'Saved!', false))
            .catch(e => showStatus('config-status', 'Save failed: ' + e, true));
        }

        function discoverDevices() {
            document.getElementById('device-list').innerHTML = '<p>Discovering...</p>';
            fetch('/api/ha/discover')
                .then(r => r.json())
                .then(data => {
                    discoveredDevices = data.devices || [];
                    renderDiscoveredDevices();
                    document.getElementById('save-devices-btn').style.display = 'block';
                })
                .catch(e => showStatus('device-status', 'Discovery failed: ' + e, true));
        }

        function renderDiscoveredDevices() {
            const el = document.getElementById('device-list');
            if (discoveredDevices.length === 0) {
                el.innerHTML = '<p>No devices found</p>';
                return;
            }
            const activeIds = activeDevices.map(d => d.entity_id);
            let html = '<table style="width:100%;border-collapse:collapse">';
            html += '<tr><th style="text-align:left">Add</th><th style="text-align:left">Name</th><th>Type</th><th>Widget</th><th>State</th></tr>';
            discoveredDevices.forEach(d => {
                const checked = activeIds.includes(d.entity_id) ? 'checked' : '';
                const widgetType = ['light','fan','cover','input_number','media_player'].includes(d.domain) ? 'slider' : 'button';
                html += '<tr style="border-bottom:1px solid #eee">';
                html += '<td><input type="checkbox" data-id="' + d.entity_id + '" data-name="' + d.friendly_name + '" data-widget="' + widgetType + '" ' + checked + '></td>';
                html += '<td>' + d.friendly_name + '</td>';
                html += '<td><small>' + d.domain + '</small></td>';
                html += '<td><select data-widget-select="' + d.entity_id + '"><option value="slider"' + (widgetType==='slider'?' selected':'') + '>Slider</option><option value="button"' + (widgetType==='button'?' selected':'') + '>Button</option></select></td>';
                html += '<td><small>' + d.state + '</small></td>';
                html += '</tr>';
            });
            html += '</table>';
            el.innerHTML = html;
        }

        function renderActiveDevices() {
            const el = document.getElementById('active-devices');
            if (activeDevices.length === 0) {
                el.innerHTML = '<p>None configured - use Discover to add devices</p>';
                return;
            }
            let html = '<ul style="list-style:none;padding:0">';
            activeDevices.forEach(d => {
                html += '<li style="padding:8px;border-bottom:1px solid #eee">' + d.label + ' (' + d.entity_id + ') - ' + d.widget_type + '</li>';
            });
            html += '</ul>';
            el.innerHTML = html;
        }

        function saveDevices() {
            const checkboxes = document.querySelectorAll('#device-list input[type=checkbox]:checked');
            const devices = [];
            let order = 0;
            checkboxes.forEach(cb => {
                const id = cb.dataset.id;
                const widgetSelect = document.querySelector('[data-widget-select="' + id + '"]');
                devices.push({
                    entity_id: id,
                    label: cb.dataset.name,
                    widget_type: widgetSelect ? widgetSelect.value : cb.dataset.widget,
                    sort_order: order++
                });
            });
            fetch('/api/ha/devices', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({devices})
            })
            .then(r => r.json())
            .then(res => {
                showStatus('device-status', res.message || 'Saved!', false);
                activeDevices = devices;
                renderActiveDevices();
            })
            .catch(e => showStatus('device-status', 'Save failed: ' + e, true));
        }

        function showStatus(elId, msg, isError) {
            const el = document.getElementById(elId);
            el.textContent = msg;
            el.className = 'status ' + (isError ? 'error' : 'success');
        }
    </script>
</body>
</html>
)rawliteral";

static void handle_root() {
    server->send(200, "text/html", CONFIG_PAGE);
}

static void handle_get_config() {
    const AppConfig& cfg = store->config();
    JsonDocument doc;
    doc["ha_url"] = cfg.ha_url;
    doc["ha_token"] = cfg.ha_token;
    doc["poll_interval_ms"] = cfg.poll_interval_ms;
    doc["idle_wifi_disconnect_ms"] = cfg.idle_wifi_disconnect_ms;
    doc["pms150g_shutdown_idle_ms"] = cfg.pms150g_shutdown_idle_ms;

    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

static void handle_post_config() {
    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server->arg("plain"));
    if (err) {
        server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    AppConfig& cfg = store->mutableConfig();

    if (doc.containsKey("ha_url")) {
        strlcpy(cfg.ha_url, doc["ha_url"] | "", sizeof(cfg.ha_url));
    }
    if (doc.containsKey("ha_token")) {
        strlcpy(cfg.ha_token, doc["ha_token"] | "", sizeof(cfg.ha_token));
    }
    if (doc.containsKey("poll_interval_ms")) {
        cfg.poll_interval_ms = doc["poll_interval_ms"] | 10000;
    }
    if (doc.containsKey("idle_wifi_disconnect_ms")) {
        cfg.idle_wifi_disconnect_ms = doc["idle_wifi_disconnect_ms"] | 300000;
    }

    cfg.configured = true;
    store->save();

    ESP_LOGI(TAG, "Config saved: HA=%s, poll=%dms", cfg.ha_url, cfg.poll_interval_ms);
    server->send(200, "application/json", "{\"message\":\"Configuration saved! Reboot to apply.\"}");
}

// Discover HA entities by proxying GET /api/states from HA
static void handle_ha_discover() {
    const AppConfig& cfg = store->config();
    if (cfg.ha_url[0] == '\0' || cfg.ha_token[0] == '\0') {
        server->send(400, "application/json", "{\"error\":\"HA not configured\"}");
        return;
    }

    HTTPClient http;
    String url = String(cfg.ha_url) + "/api/states";
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + cfg.ha_token);
    int code = http.GET();

    if (code != 200) {
        char err[64];
        snprintf(err, sizeof(err), "{\"error\":\"HA returned %d\"}", code);
        server->send(502, "application/json", err);
        http.end();
        return;
    }

    // Parse HA response and filter to useful entities
    String response = http.getString();
    http.end();

    JsonDocument ha_doc;
    DeserializationError err = deserializeJson(ha_doc, response);
    if (err) {
        server->send(500, "application/json", "{\"error\":\"Failed to parse HA response\"}");
        return;
    }

    // Build filtered response with only controllable entity types
    JsonDocument out_doc;
    JsonArray devices = out_doc["devices"].to<JsonArray>();

    const char* domains[] = {"light", "switch", "fan", "cover", "scene", "script",
                              "lock", "media_player", "input_number", "input_boolean",
                              "automation", "vacuum"};
    int domain_count = sizeof(domains) / sizeof(domains[0]);

    for (JsonObject entity : ha_doc.as<JsonArray>()) {
        const char* entity_id = entity["entity_id"];
        if (!entity_id) continue;

        // Check if entity domain is in our supported list
        bool supported = false;
        for (int i = 0; i < domain_count; i++) {
            size_t dlen = strlen(domains[i]);
            if (strncmp(entity_id, domains[i], dlen) == 0 && entity_id[dlen] == '.') {
                supported = true;
                break;
            }
        }
        if (!supported) continue;

        JsonObject d = devices.add<JsonObject>();
        d["entity_id"] = entity_id;
        d["state"] = entity["state"];
        d["friendly_name"] = entity["attributes"]["friendly_name"] | entity_id;

        // Extract domain
        char domain[16] = {};
        const char* dot = strchr(entity_id, '.');
        if (dot) {
            size_t len = dot - entity_id;
            if (len >= sizeof(domain)) len = sizeof(domain) - 1;
            memcpy(domain, entity_id, len);
        }
        d["domain"] = domain;
    }

    String json;
    serializeJson(out_doc, json);
    server->send(200, "application/json", json);
    ESP_LOGI(TAG, "Discovery returned %d devices", devices.size());
}

// Get currently configured UI devices
static void handle_ha_get_devices() {
    const AppConfig& cfg = store->config();
    JsonDocument doc;
    JsonArray devices = doc["devices"].to<JsonArray>();

    for (int i = 0; i < cfg.ui_device_count; i++) {
        JsonObject d = devices.add<JsonObject>();
        d["entity_id"] = cfg.ui_devices[i].entity_id;
        d["label"] = cfg.ui_devices[i].label;
        d["widget_type"] = cfg.ui_devices[i].widget_type;
        d["sort_order"] = cfg.ui_devices[i].sort_order;
    }

    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

// Save UI device configuration
static void handle_ha_post_devices() {
    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"error\":\"No body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server->arg("plain"));
    if (err) {
        server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    AppConfig& cfg = store->mutableConfig();
    JsonArray devices = doc["devices"];
    cfg.ui_device_count = 0;

    for (JsonObject d : devices) {
        if (cfg.ui_device_count >= MAX_UI_DEVICES) break;
        UIDevice& dev = cfg.ui_devices[cfg.ui_device_count];
        strlcpy(dev.entity_id, d["entity_id"] | "", sizeof(dev.entity_id));
        strlcpy(dev.label, d["label"] | "", sizeof(dev.label));
        strlcpy(dev.widget_type, d["widget_type"] | "button", sizeof(dev.widget_type));
        dev.sort_order = d["sort_order"] | cfg.ui_device_count;
        cfg.ui_device_count++;
    }

    cfg.configured = true;
    store->save();

    ESP_LOGI(TAG, "Saved %d UI devices", cfg.ui_device_count);
    server->send(200, "application/json", "{\"message\":\"Devices saved! Reboot to apply.\"}");
}

void config_server_start(ConfigStore* config_store) {
    if (server) return;
    store = config_store;

    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_root);
    server->on("/api/config", HTTP_GET, handle_get_config);
    server->on("/api/config", HTTP_POST, handle_post_config);
    server->on("/api/ha/discover", HTTP_GET, handle_ha_discover);
    server->on("/api/ha/devices", HTTP_GET, handle_ha_get_devices);
    server->on("/api/ha/devices", HTTP_POST, handle_ha_post_devices);
    server->begin();

    ESP_LOGI(TAG, "Config server started on http://%s", WiFi.localIP().toString().c_str());
}

void config_server_stop() {
    if (!server) return;
    server->stop();
    delete server;
    server = nullptr;
    store = nullptr;
    ESP_LOGI(TAG, "Config server stopped");
}

void config_server_poll() {
    if (server) server->handleClient();
}

bool config_server_is_active() {
    return server != nullptr;
}
