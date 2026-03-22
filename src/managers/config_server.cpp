#include "config_server.h"
#include "esp_log.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static const char* TAG = "config_server";
static WebServer* server = nullptr;
static ConfigStore* store = nullptr;

// Embedded HTML page — served at /
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

    <button onclick="saveConfig()">Save Configuration</button>
    <div id="status"></div>

    <script>
        // Load current config on page load
        fetch('/api/config')
            .then(r => r.json())
            .then(cfg => {
                document.getElementById('ha_url').value = cfg.ha_url || '';
                document.getElementById('ha_token').value = cfg.ha_token || '';
                document.getElementById('poll_interval').value = (cfg.poll_interval_ms || 10000) / 1000;
                document.getElementById('idle_disconnect').value = (cfg.idle_wifi_disconnect_ms || 300000) / 60000;
            })
            .catch(e => showStatus('Failed to load config', true));

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
            .then(res => showStatus(res.message || 'Saved!', false))
            .catch(e => showStatus('Save failed: ' + e, true));
        }

        function showStatus(msg, isError) {
            const el = document.getElementById('status');
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

void config_server_start(ConfigStore* config_store) {
    if (server) return;
    store = config_store;

    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_root);
    server->on("/api/config", HTTP_GET, handle_get_config);
    server->on("/api/config", HTTP_POST, handle_post_config);
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
