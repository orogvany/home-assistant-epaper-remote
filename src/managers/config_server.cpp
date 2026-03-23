#include "config_server.h"
#include "esp_log.h"
#include "generated/icon_manifest.h"
#include <alexa_api.h>
#include <alexa_auth.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static const char* TAG = "config_server";
static WebServer* server = nullptr;
static ConfigStore* store = nullptr;
static AlexaAuth alexa_auth;

// Embedded HTML page - served at /
// NOTE: This is a large string literal. If it gets too big for flash,
// move to SPIFFS and serve from filesystem instead.
static const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>HA Remote Config</title>
    <style>
        body { font-family: -apple-system, sans-serif; max-width: 640px; margin: 0 auto; padding: 16px; background: #f5f5f5; color: #333; }
        h1 { margin-bottom: 4px; }
        h2 { color: #555; margin-top: 28px; border-bottom: 1px solid #ddd; padding-bottom: 6px; }
        label { display: block; margin-top: 10px; font-weight: bold; color: #444; font-size: 0.9em; }
        input, select { width: 100%; padding: 8px; margin-top: 3px; border: 1px solid #ccc; border-radius: 6px; font-size: 15px; box-sizing: border-box; }
        .btn { background: #2196F3; color: white; border: none; padding: 12px 20px; border-radius: 6px; font-size: 15px; cursor: pointer; margin-top: 12px; }
        .btn:hover { background: #1976D2; }
        .btn-sm { padding: 6px 14px; font-size: 13px; margin-top: 8px; }
        .btn-outline { background: none; border: 1px solid #2196F3; color: #2196F3; }
        .btn-outline:hover { background: #e3f2fd; }
        .status { padding: 8px; margin-top: 8px; border-radius: 6px; font-size: 0.9em; }
        .success { background: #c8e6c9; color: #2e7d32; }
        .error { background: #ffcdd2; color: #c62828; }
        .dev-list { list-style: none; padding: 0; margin: 12px 0; }
        .dev-row { display: flex; align-items: center; gap: 8px; padding: 10px; margin: 4px 0; background: #fff; border-radius: 8px; border: 1px solid #e0e0e0; cursor: grab; user-select: none; }
        .dev-row.dragging { opacity: 0.3; }
        .dev-row.drag-over { border-top: 3px solid #2196F3; }
        .dev-handle { font-size: 1.3em; color: #aaa; flex-shrink: 0; }
        .dev-info { flex: 1; min-width: 0; }
        .dev-name { font-weight: bold; font-size: 0.95em; }
        .dev-name input { font-weight: bold; padding: 4px 6px; }
        .dev-meta { font-size: 0.8em; color: #888; margin-top: 2px; }
        .dev-state { font-size: 0.8em; padding: 2px 8px; border-radius: 10px; white-space: nowrap; }
        .dev-state-on { background: #c8e6c9; color: #2e7d32; }
        .dev-state-off { background: #e0e0e0; color: #666; }
        .dev-controls { display: flex; align-items: center; gap: 6px; flex-shrink: 0; }
        .dev-controls select { width: auto; padding: 4px; font-size: 13px; }
        .dev-vis { cursor: pointer; font-size: 1.1em; background: none; border: none; padding: 4px; }
        .dev-remove { cursor: pointer; font-size: 1em; background: none; border: none; padding: 4px; color: #e53935; }
        .dev-icon { width: 32px; height: 32px; flex-shrink: 0; cursor: pointer; border-radius: 4px; padding: 2px; }
        .dev-icon:hover { background: #e3f2fd; }
        .hidden-dev { opacity: 0.45; }
        .icon-popover { display:none; position:fixed; z-index:100; background:#fff; border:1px solid #ccc; border-radius:8px; padding:8px; max-width:280px; max-height:240px; overflow-y:auto; box-shadow:0 4px 16px rgba(0,0,0,0.15); }
        .icon-popover.open { display:grid; grid-template-columns:repeat(4,1fr); gap:4px; }
        .icon-opt { display:flex; flex-direction:column; align-items:center; padding:6px 4px; border-radius:4px; cursor:pointer; border:1px solid transparent; }
        .icon-opt:hover { background:#e3f2fd; }
        .icon-opt.selected { border-color:#2196F3; background:#e3f2fd; }
        .icon-opt img { width:32px; height:32px; }
        .icon-opt span { font-size:0.6em; color:#888; margin-top:2px; text-align:center; line-height:1.1; }
        .discover-table { width: 100%; border-collapse: collapse; margin: 8px 0; font-size: 0.9em; }
        .discover-table th { text-align: left; padding: 6px; border-bottom: 2px solid #ddd; }
        .discover-table td { padding: 6px; border-bottom: 1px solid #eee; }
    </style>
</head>
<body>
    <h1>HA Remote</h1>
    <p style="color:#888;margin-top:0">Configuration</p>

    <h2>Home Assistant Connection</h2>
    <label>HA URL</label>
    <input type="text" id="ha_url" placeholder="http://192.168.0.102:8123">
    <label>Access Token</label>
    <input type="text" id="ha_token" placeholder="Long-lived access token">
    <label>Poll Interval (seconds)</label>
    <input type="number" id="poll_interval" value="10" min="1" max="300">
    <label>WiFi Idle Disconnect (minutes)</label>
    <input type="number" id="idle_disconnect" value="5" min="1" max="60">

    <h2>Security</h2>
    <label>PIN Lock</label>
    <select id="pin_enabled">
        <option value="1">Enabled</option>
        <option value="0">Disabled</option>
    </select>
    <label>PIN Code (4 digits)</label>
    <input type="text" id="pin_code" maxlength="4" pattern="[0-9]{4}" placeholder="1234">
    <button class="btn" onclick="saveConfig()">Save Settings</button>
    <div id="config-status"></div>

    <h2>My Devices</h2>
    <p style="color:#888;font-size:0.85em">Drag to reorder. Edit names. Choose widget type. Max 8 devices on screen.</p>
    <ul class="dev-list" id="active-devices"></ul>
    <button class="btn btn-sm btn-outline" onclick="saveDevices()">Save Device Config</button>
    <div id="device-status"></div>

    <div class="icon-popover" id="icon-popover"></div>

    <h2>Discover HA Devices</h2>
    <button class="btn btn-sm" onclick="discoverDevices()">Scan Home Assistant</button>
    <div id="discover-list"></div>

    <h2>Alexa</h2>
    <div id="alexa-section">
        <div id="alexa-status"></div>
        <button class="btn btn-sm" onclick="alexaStartAuth()">Authorize Alexa</button>
        <div id="alexa-auth-flow" style="display:none; margin-top:12px;">
            <p style="font-size:0.85em">1. Click to log in to Amazon:</p>
            <a id="alexa-auth-link" href="#" target="_blank" class="btn btn-sm btn-outline" style="display:inline-block;text-decoration:none">Open Amazon Login</a>
            <p style="font-size:0.85em;margin-top:12px">2. After login, paste the redirect URL here:</p>
            <input type="text" id="alexa-callback-url" placeholder="Paste the maplanding URL here">
            <button class="btn btn-sm" onclick="alexaCompleteAuth()">Complete Authorization</button>
        </div>
        <div id="alexa-auth-result"></div>
        <div style="margin-top:12px">
            <button class="btn btn-sm" onclick="alexaDiscover()">Refresh from Alexa</button>
        </div>
        <div id="alexa-discover-list"></div>
    </div>

    <script>
    let activeDevices = [];
    let icons = [];
    let dragIdx = null;
    let popoverTarget = null;
    const popover = document.getElementById('icon-popover');

    // Load icons
    fetch('/api/icons').then(r=>r.json()).then(data => { icons = data; }).catch(()=>{});

    // Close icon popover on outside click
    document.addEventListener('click', e => {
        if (!popover.contains(e.target) && !e.target.classList.contains('dev-icon')) {
            popover.className = 'icon-popover';
        }
    });

    function iconDataFor(id) { return icons.find(i => i.id === id) || icons[0] || null; }

    function openIconPicker(idx, imgEl) {
        popoverTarget = idx;
        popover.innerHTML = '';
        icons.filter(ico => !ico.id.includes('_off')).forEach(ico => {
            const opt = document.createElement('div');
            opt.className = 'icon-opt' + (activeDevices[idx].icon_on === ico.id ? ' selected' : '');
            opt.innerHTML = '<img src="' + ico.data + '"><span>' + ico.title + '</span>';
            opt.addEventListener('click', () => {
                activeDevices[idx].icon_on = ico.id;
                // Auto-set off icon (try _off variant)
                const offId = ico.id + '_off';
                const offIco = icons.find(i => i.id === offId);
                activeDevices[idx].icon_off = offIco ? offId : ico.id;
                popover.className = 'icon-popover';
                renderActive();
            });
            popover.appendChild(opt);
        });
        const rect = imgEl.getBoundingClientRect();
        popover.style.top = Math.min(rect.bottom + 4, window.innerHeight - 250) + 'px';
        popover.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - 290)) + 'px';
        popover.className = 'icon-popover open';
    }

    // Load config
    fetch('/api/config').then(r=>r.json()).then(cfg => {
        document.getElementById('ha_url').value = cfg.ha_url || '';
        document.getElementById('ha_token').value = cfg.ha_token || '';
        document.getElementById('poll_interval').value = (cfg.poll_interval_ms || 10000) / 1000;
        document.getElementById('idle_disconnect').value = (cfg.idle_wifi_disconnect_ms || 300000) / 60000;
        document.getElementById('pin_enabled').value = cfg.pin_enabled ? '1' : '0';
        document.getElementById('pin_code').value = cfg.pin_code || '1234';
    }).catch(e => msg('config-status', 'Failed to load config', true));

    // Load active devices
    fetch('/api/ha/devices').then(r=>r.json()).then(data => {
        activeDevices = (data.devices || []).map((d,i) => ({...d, sort_order: d.sort_order || i}));
        renderActive();
    }).catch(e => { document.getElementById('active-devices').innerHTML = '<li style="color:#888">None configured</li>'; });

    function msg(id, text, isErr) {
        const el = document.getElementById(id);
        el.textContent = text;
        el.className = 'status ' + (isErr ? 'error' : 'success');
        setTimeout(() => { el.textContent = ''; el.className = ''; }, 4000);
    }

    function saveConfig() {
        fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify({
                ha_url: document.getElementById('ha_url').value,
                ha_token: document.getElementById('ha_token').value,
                poll_interval_ms: parseInt(document.getElementById('poll_interval').value) * 1000,
                idle_wifi_disconnect_ms: parseInt(document.getElementById('idle_disconnect').value) * 60000,
                pin_enabled: document.getElementById('pin_enabled').value === '1',
                pin_code: document.getElementById('pin_code').value
            })
        }).then(r=>r.json()).then(r => msg('config-status', r.message||'Saved!', false))
          .catch(e => msg('config-status', 'Save failed', true));
    }

    // --- Active devices with drag/drop, rename, widget type, remove ---
    function renderActive() {
        const ul = document.getElementById('active-devices');
        ul.innerHTML = '';
        if (!activeDevices.length) {
            ul.innerHTML = '<li style="color:#888;padding:12px">No devices - use Discover below to add</li>';
            return;
        }
        activeDevices.sort((a,b) => a.sort_order - b.sort_order);
        activeDevices.forEach((d, i) => {
            const li = document.createElement('li');
            li.className = 'dev-row';
            li.draggable = true;
            li.dataset.idx = i;
            const ico = iconDataFor(d.icon_on || 'lightbulb');
            li.innerHTML = `
                <span class="dev-handle">&#8801;</span>
                <img class="dev-icon" src="${ico ? ico.data : ''}" alt="${d.icon_on||''}" title="Change icon" data-idx="${i}">
                <div class="dev-info">
                    <div class="dev-name"><input type="text" value="${esc(d.label)}" onchange="activeDevices[${i}].label=this.value" style="border:none;padding:2px 4px;width:90%"></div>
                    <div class="dev-meta">${esc(d.entity_id)}</div>
                </div>
                <div class="dev-controls">
                    <select onchange="activeDevices[${i}].widget_type=this.value">
                        <option value="slider"${d.widget_type==='slider'?' selected':''}>Slider</option>
                        <option value="button"${d.widget_type==='button'?' selected':''}>Button</option>
                    </select>
                    <button class="dev-remove" onclick="activeDevices.splice(${i},1);renderActive()" title="Remove">&#10005;</button>
                </div>
            `;
            li.querySelector('.dev-icon').addEventListener('click', function(e) { e.stopPropagation(); openIconPicker(i, this); });
            // Drag events
            li.addEventListener('dragstart', e => { dragIdx = i; li.classList.add('dragging'); });
            li.addEventListener('dragend', e => { dragIdx = null; li.classList.remove('dragging'); });
            li.addEventListener('dragover', e => { e.preventDefault(); li.classList.add('drag-over'); });
            li.addEventListener('dragleave', e => { li.classList.remove('drag-over'); });
            li.addEventListener('drop', e => {
                e.preventDefault();
                li.classList.remove('drag-over');
                if (dragIdx !== null && dragIdx !== i) {
                    const moved = activeDevices.splice(dragIdx, 1)[0];
                    activeDevices.splice(i, 0, moved);
                    activeDevices.forEach((d,j) => d.sort_order = j);
                    renderActive();
                }
            });
            ul.appendChild(li);
        });
    }

    function saveDevices() {
        activeDevices.forEach((d,i) => d.sort_order = i);
        fetch('/api/ha/devices', { method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify({devices: activeDevices})
        }).then(r=>r.json()).then(r => msg('device-status', r.message||'Saved!', false))
          .catch(e => msg('device-status', 'Save failed', true));
    }

    // --- Discovery ---
    function discoverDevices() {
        document.getElementById('discover-list').innerHTML = '<p>Scanning...</p>';
        fetch('/api/ha/discover').then(r=>r.json()).then(data => {
            const devices = data.devices || [];
            if (!devices.length) {
                document.getElementById('discover-list').innerHTML = '<p>No devices found</p>';
                return;
            }
            const activeIds = activeDevices.map(d => d.entity_id);
            let html = '<table class="discover-table"><tr><th></th><th>Name</th><th>Domain</th><th>State</th></tr>';
            devices.forEach(d => {
                const added = activeIds.includes(d.entity_id);
                const stateClass = (d.state === 'on' || d.state === 'playing' || d.state === 'open') ? 'dev-state-on' : 'dev-state-off';
                const widgetGuess = d.domain === 'weather' ? 'weather' : ['light','fan','cover','input_number','media_player'].includes(d.domain) ? 'slider' : 'button';
                html += '<tr>';
                html += '<td>' + (added ? '<span style="color:#2e7d32">Added</span>' :
                    '<button class="btn btn-sm btn-outline" onclick="addDevice(\'' + esc(d.entity_id) + '\',\'' + esc(d.friendly_name) + '\',\'' + widgetGuess + '\')">Add</button>') + '</td>';
                html += '<td>' + esc(d.friendly_name) + '</td>';
                html += '<td><small>' + d.domain + '</small></td>';
                html += '<td><span class="dev-state ' + stateClass + '">' + d.state + '</span></td>';
                html += '</tr>';
            });
            html += '</table>';
            document.getElementById('discover-list').innerHTML = html;
        }).catch(e => { document.getElementById('discover-list').innerHTML = '<p class="error">Discovery failed</p>'; });
    }

    function defaultIcons(domain) {
        const map = {
            light: ['lightbulb', 'lightbulb_off'],
            fan: ['fan', 'fan_off'],
            vacuum: ['robot', 'robot_off'],
        };
        return map[domain] || ['lightbulb', 'lightbulb_off'];
    }

    function addDevice(entityId, name, widgetType) {
        if (activeDevices.length >= 8) { alert('Max 8 devices'); return; }
        if (activeDevices.find(d => d.entity_id === entityId)) return;
        const domain = entityId.split('.')[0];
        const [iconOn, iconOff] = defaultIcons(domain);
        activeDevices.push({ entity_id: entityId, label: name, widget_type: widgetType, source: 'ha', icon_on: iconOn, icon_off: iconOff, sort_order: activeDevices.length });
        renderActive();
        discoverDevices();
    }

    function esc(s) { const d = document.createElement('div'); d.textContent = s; return d.innerHTML; }

    // Alexa
    function alexaCheckStatus() {
        fetch('/api/alexa/status').then(r => r.json()).then(d => {
            document.getElementById('alexa-status').innerHTML = d.authenticated
                ? '<p style="color:#2e7d32">Alexa: Connected</p>'
                : '<p style="color:#888">Alexa: Not authorized</p>';
        }).catch(() => {});
    }
    alexaCheckStatus();

    function alexaLoadCached() {
        fetch('/api/alexa/devices').then(r => r.json()).then(data => {
            const devices = data.devices || [];
            if (devices.length > 0) alexaRenderDevices(devices);
        }).catch(() => {});
    }
    alexaLoadCached();

    function alexaStartAuth() {
        fetch('/api/alexa/auth/start')
            .then(r => r.json()).then(d => {
                if (d.auth_url) {
                    document.getElementById('alexa-auth-link').href = d.auth_url;
                    document.getElementById('alexa-auth-flow').style.display = 'block';
                } else {
                    document.getElementById('alexa-auth-result').innerHTML = '<p class="error">' + (d.error || 'Failed') + '</p>';
                }
            }).catch(e => { document.getElementById('alexa-auth-result').innerHTML = '<p class="error">Failed to start auth</p>'; });
    }

    function alexaCompleteAuth() {
        const url = document.getElementById('alexa-callback-url').value;
        fetch('/api/alexa/auth/callback', {
            method: 'POST', headers: {'Content-Type':'application/json'},
            body: JSON.stringify({url: url})
        }).then(r => r.json()).then(d => {
            if (d.success) {
                document.getElementById('alexa-auth-result').innerHTML = '<p style="color:#2e7d32">Authorized!</p>';
                document.getElementById('alexa-auth-flow').style.display = 'none';
                alexaCheckStatus();
            } else {
                document.getElementById('alexa-auth-result').innerHTML = '<p class="error">' + (d.error || 'Auth failed') + '</p>';
            }
        }).catch(e => { document.getElementById('alexa-auth-result').innerHTML = '<p class="error">Auth request failed</p>'; });
    }

    function alexaRenderDevices(devices) {
        let html = '<table class="discover-table"><tr><th></th><th>Name</th><th>Type</th><th>Status</th></tr>';
        devices.forEach(d => {
            const added = activeDevices.find(a => a.entity_id === d.entity_id);
            const widgetGuess = d.has_brightness ? 'slider' : 'button';
            const statusClass = d.reachable ? 'dev-state-on' : 'dev-state-off';
            const statusText = d.reachable ? 'Online' : 'Offline';
            html += '<tr' + (d.reachable ? '' : ' class="hidden-dev"') + '>';
            html += '<td>' + (added ? '<span style="color:#2e7d32">Added</span>' :
                '<button class="btn btn-sm btn-outline" onclick="addAlexaDevice(\'' + esc(d.entity_id) + '\',\'' + esc(d.friendly_name) + '\',\'' + widgetGuess + '\')">Add</button>') + '</td>';
            html += '<td>' + esc(d.friendly_name) + '</td>';
            html += '<td><small>' + d.type + '</small></td>';
            html += '<td><span class="dev-state ' + statusClass + '">' + statusText + '</span></td>';
            html += '</tr>';
        });
        html += '</table>';
        document.getElementById('alexa-discover-list').innerHTML = html;
    }

    function alexaDiscover() {
        document.getElementById('alexa-discover-list').innerHTML = '<p>Discovering...</p>';
        fetch('/api/alexa/discover').then(r => r.json()).then(data => {
            if (data.error) { document.getElementById('alexa-discover-list').innerHTML = '<p class="error">' + data.error + '</p>'; return; }
            alexaRenderDevices(data.devices || []);
        }).catch(e => { document.getElementById('alexa-discover-list').innerHTML = '<p class="error">Discovery failed</p>'; });
    }

    function addAlexaDevice(entityId, name, widgetType) {
        if (activeDevices.length >= 8) { alert('Max 8 devices'); return; }
        if (activeDevices.find(d => d.entity_id === entityId)) return;
        activeDevices.push({ entity_id: entityId, label: name, widget_type: widgetType, source: 'alexa', icon_on: 'lightbulb', icon_off: 'lightbulb_off', sort_order: activeDevices.length });
        renderActive();
        alexaLoadCached();
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
    doc["pin_enabled"] = cfg.pin_enabled;
    doc["pin_code"] = cfg.pin_code;

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
    if (doc.containsKey("pin_enabled")) {
        cfg.pin_enabled = doc["pin_enabled"] | true;
    }
    if (doc.containsKey("pin_code")) {
        strlcpy(cfg.pin_code, doc["pin_code"] | "1234", sizeof(cfg.pin_code));
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
    ESP_LOGI(TAG, "Discovery: GET %s", url.c_str());
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + cfg.ha_token);
    int code = http.GET();
    ESP_LOGI(TAG, "Discovery: HA returned %d", code);

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

    ESP_LOGI(TAG, "Discovery: response size %d bytes", response.length());

    JsonDocument ha_doc;
    DeserializationError err = deserializeJson(ha_doc, response, DeserializationOption::NestingLimit(20));
    if (err) {
        ESP_LOGE(TAG, "Discovery: JSON parse failed: %s (response %d bytes)", err.c_str(), response.length());
        server->send(500, "application/json", "{\"error\":\"Failed to parse HA response\"}");
        return;
    }

    // Build filtered response with only controllable entity types
    JsonDocument out_doc;
    JsonArray devices = out_doc["devices"].to<JsonArray>();

    const char* domains[] = {"light", "switch", "fan", "cover", "scene", "script",
                              "lock", "media_player", "input_number", "input_boolean",
                              "automation", "vacuum", "weather"};
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
        d["source"] = cfg.ui_devices[i].source;
        d["icon_on"] = cfg.ui_devices[i].icon_on;
        d["icon_off"] = cfg.ui_devices[i].icon_off;
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
        strlcpy(dev.source, d["source"] | "ha", sizeof(dev.source));
        strlcpy(dev.icon_on, d["icon_on"] | "lightbulb", sizeof(dev.icon_on));
        strlcpy(dev.icon_off, d["icon_off"] | "lightbulb_off", sizeof(dev.icon_off));
        dev.sort_order = d["sort_order"] | cfg.ui_device_count;
        cfg.ui_device_count++;
    }

    cfg.configured = true;
    store->save();

    ESP_LOGI(TAG, "Saved %d UI devices", cfg.ui_device_count);
    server->send(200, "application/json", "{\"message\":\"Devices saved! Reboot to apply.\"}");
}

// Alexa auth status
static void handle_alexa_status() {
    JsonDocument doc;
    doc["authenticated"] = (alexa_auth.getState() == AlexaAuthState::AUTHENTICATED);
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

// Start Alexa OAuth flow
static void handle_alexa_auth_start() {
    ESP_LOGI(TAG, "Starting Alexa OAuth flow");
    alexa_auth.startOAuthFlow("amazon.com");

    JsonDocument doc;
    doc["auth_url"] = alexa_auth.getAuthUrl();
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

// Complete Alexa OAuth with maplanding URL
static void handle_alexa_auth_callback() {
    String body = server->arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* url = doc["url"];
    if (!url) {
        server->send(400, "application/json", "{\"error\":\"No URL provided\"}");
        return;
    }

    // Extract auth code from maplanding URL
    const char* code_start = strstr(url, "openid.oa2.authorization_code=");
    if (!code_start) {
        server->send(400, "application/json", "{\"error\":\"No authorization code found in URL\"}");
        return;
    }
    code_start += strlen("openid.oa2.authorization_code=");

    char code[256] = {};
    const char* code_end = strchr(code_start, '&');
    size_t code_len = code_end ? (size_t)(code_end - code_start) : strlen(code_start);
    if (code_len >= sizeof(code)) code_len = sizeof(code) - 1;
    memcpy(code, code_start, code_len);

    ESP_LOGI(TAG, "Alexa auth callback, code length: %d", code_len);

    if (alexa_auth.handleAuthCallback(code)) {
        AppConfig& cfg = store->mutableConfig();
        cfg.alexa_enabled = true;
        strlcpy(cfg.alexa_domain, alexa_auth.getDomain().c_str(), sizeof(cfg.alexa_domain));
        store->save();

        server->send(200, "application/json", "{\"success\":true}");
    } else {
        String err = alexa_auth.getLastError();
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", err.length() > 0 ? err.c_str() : "Auth failed");
        server->send(500, "application/json", buf);
    }
}

// Discover Alexa smart home devices
static void handle_alexa_discover() {
    if (alexa_auth.getState() != AlexaAuthState::AUTHENTICATED) {
        server->send(401, "application/json", "{\"error\":\"Alexa not authorized\"}");
        return;
    }

    // Ensure tokens are fresh
    alexa_auth.refreshAccessToken();

    AlexaAPI api;
    api.begin(alexa_auth.getRefreshToken().c_str(),
              alexa_auth.getAccessToken().c_str(),
              alexa_auth.getCookies().c_str(),
              alexa_auth.getCustomerID().c_str(),
              alexa_auth.getDomain().c_str());

    ESP_LOGI(TAG, "Alexa: discovering smart home devices...");
    String response = api.discoverSmartHome();
    if (response.isEmpty()) {
        server->send(502, "application/json", "{\"error\":\"Alexa discovery returned empty\"}");
        return;
    }

    JsonDocument ha_doc;
    DeserializationError err = deserializeJson(ha_doc, response, DeserializationOption::NestingLimit(20));
    if (err) {
        ESP_LOGE(TAG, "Alexa discovery JSON parse failed: %s", err.c_str());
        server->send(500, "application/json", "{\"error\":\"Failed to parse Alexa response\"}");
        return;
    }

    // Parse GraphQL response
    JsonDocument out_doc;
    JsonArray devices = out_doc["devices"].to<JsonArray>();

    JsonArray items = ha_doc["data"]["endpoints"]["items"];
    if (items.isNull()) {
        ESP_LOGE(TAG, "Alexa: no data.endpoints.items in response");
        if (!ha_doc["errors"].isNull()) {
            String errStr;
            serializeJson(ha_doc["errors"], errStr);
            ESP_LOGE(TAG, "Alexa GraphQL errors: %s", errStr.c_str());
        }
        server->send(502, "application/json", "{\"error\":\"No devices in Alexa response\"}");
        return;
    }
    ESP_LOGI(TAG, "Alexa: found %d items in GraphQL response", items.size());
    for (JsonObject item : items) {
        JsonObject appliance = item["legacyAppliance"];
        if (!appliance) continue;

        const char* entity_id = appliance["entityId"];
        const char* friendly_name = appliance["friendlyName"];
        if (!entity_id || !friendly_name) continue;

        // Skip Echo devices
        JsonArray app_types = appliance["applianceTypes"];
        bool is_echo = false;
        for (const char* t : app_types) {
            if (t && strcmp(t, "ALEXA_VOICE_ENABLED") == 0) { is_echo = true; break; }
        }
        if (is_echo) continue;

        // Determine capabilities (can be strings or objects with interfaceName)
        JsonArray caps = appliance["capabilities"];
        bool has_power = false;
        bool has_brightness = false;
        for (JsonVariant cap : caps) {
            const char* s = nullptr;
            if (cap.is<const char*>()) {
                s = cap.as<const char*>();
            } else if (cap.is<JsonObject>()) {
                s = cap["interfaceName"];
            }
            if (s) {
                if (strstr(s, "PowerController")) has_power = true;
                if (strstr(s, "BrightnessController")) has_brightness = true;
            }
        }
        if (!has_power && !has_brightness) continue;

        JsonObject d = devices.add<JsonObject>();
        d["entity_id"] = entity_id;
        d["friendly_name"] = friendly_name;
        d["type"] = has_brightness ? "light" : "switch";
        d["has_brightness"] = has_brightness;
        d["reachable"] = false;
    }

    // Poll state to get reachability
    if (devices.size() > 0) {
        JsonDocument poll_doc;
        JsonArray reqs = poll_doc["stateRequests"].to<JsonArray>();
        for (JsonObject d : devices) {
            JsonObject r = reqs.add<JsonObject>();
            r["entityId"] = d["entity_id"];
            r["entityType"] = "ENTITY";
        }
        String poll_body;
        serializeJson(poll_doc, poll_body);

        String poll_resp = api.pollDeviceState(poll_body);
        if (!poll_resp.isEmpty()) {
            JsonDocument poll_result;
            if (!deserializeJson(poll_result, poll_resp, DeserializationOption::NestingLimit(20))) {
                for (JsonObject ds : poll_result["deviceStates"].as<JsonArray>()) {
                    const char* eid = ds["entity"]["entityId"];
                    if (!eid) continue;

                    for (JsonObject d : devices) {
                        if (strcmp(d["entity_id"], eid) != 0) continue;
                        for (JsonVariant cs : ds["capabilityStates"].as<JsonArray>()) {
                            const char* csStr = cs.as<const char*>();
                            if (!csStr) continue;
                            JsonDocument cap_doc;
                            if (deserializeJson(cap_doc, csStr)) continue;
                            if (strcmp(cap_doc["namespace"] | "", "Alexa.EndpointHealth") == 0 &&
                                strcmp(cap_doc["name"] | "", "connectivity") == 0) {
                                const char* val = cap_doc["value"]["value"];
                                if (val) d["reachable"] = (strcmp(val, "OK") == 0);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Persist discovered devices to NVS cache
    int dev_count = std::min((int)devices.size(), (int)MAX_ALEXA_DEVICES);
    AlexaKnownDevice cache[MAX_ALEXA_DEVICES] = {};
    for (int i = 0; i < dev_count; i++) {
        JsonObject d = devices[i];
        strlcpy(cache[i].entity_id, d["entity_id"] | "", sizeof(cache[i].entity_id));
        strlcpy(cache[i].friendly_name, d["friendly_name"] | "", sizeof(cache[i].friendly_name));
        strlcpy(cache[i].type, d["type"] | "switch", sizeof(cache[i].type));
        cache[i].has_brightness = d["has_brightness"] | false;
        cache[i].reachable = d["reachable"] | false;
    }
    store->updateAlexaDevices(cache, dev_count);

    String json;
    serializeJson(out_doc, json);
    server->send(200, "application/json", json);
    ESP_LOGI(TAG, "Alexa discovery returned %d devices (cached to NVS)", devices.size());
}

// Return cached Alexa devices (no API call, instant)
static void handle_alexa_get_devices() {
    const AppConfig& cfg = store->config();
    JsonDocument doc;
    JsonArray devices = doc["devices"].to<JsonArray>();

    for (int i = 0; i < cfg.alexa_device_count; i++) {
        JsonObject d = devices.add<JsonObject>();
        d["entity_id"] = cfg.alexa_devices[i].entity_id;
        d["friendly_name"] = cfg.alexa_devices[i].friendly_name;
        d["type"] = cfg.alexa_devices[i].type;
        d["has_brightness"] = cfg.alexa_devices[i].has_brightness;
        d["reachable"] = cfg.alexa_devices[i].reachable;
    }

    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

void config_server_start(ConfigStore* config_store) {
    if (server) return;
    store = config_store;
    alexa_auth.loadFromNVS();

    server = new WebServer(80);
    server->on("/", HTTP_GET, handle_root);
    server->on("/api/config", HTTP_GET, handle_get_config);
    server->on("/api/config", HTTP_POST, handle_post_config);
    server->on("/api/ha/discover", HTTP_GET, handle_ha_discover);
    server->on("/api/ha/devices", HTTP_GET, handle_ha_get_devices);
    server->on("/api/ha/devices", HTTP_POST, handle_ha_post_devices);
    // Alexa endpoints
    server->on("/api/alexa/status", HTTP_GET, handle_alexa_status);
    server->on("/api/alexa/auth/start", HTTP_GET, handle_alexa_auth_start);
    server->on("/api/alexa/auth/callback", HTTP_POST, handle_alexa_auth_callback);
    server->on("/api/alexa/devices", HTTP_GET, handle_alexa_get_devices);
    server->on("/api/alexa/discover", HTTP_GET, handle_alexa_discover);

    server->on("/api/icons", HTTP_GET, []() {
        // Serve icon manifest from PROGMEM
        String json;
        json.reserve(strlen_P(ICON_MANIFEST_JSON));
        const char* p = ICON_MANIFEST_JSON;
        char c;
        while ((c = pgm_read_byte(p++))) json += c;
        server->send(200, "application/json", json);
    });
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
