#include "WiFiManager.h"
#include "Config.h"

#if !DEMO_MODE
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

// ---- Wi-Fi setup tunables (override with -D / Config.h if ever needed) ----
#ifndef WIFI_SETUP_AP_SSID
#define WIFI_SETUP_AP_SSID "AIDeskCompanion-Setup"
#endif
#ifndef WIFI_SETUP_AP_PASSWORD
#define WIFI_SETUP_AP_PASSWORD ""            // "" = open network; otherwise 8-63 chars
#endif
#ifndef WIFI_SETUP_AP_AFTER_MS
#define WIFI_SETUP_AP_AFTER_MS     45000UL   // continuously offline this long -> start setup AP
#endif
#ifndef WIFI_SETUP_RETRY_MS
#define WIFI_SETUP_RETRY_MS        60000UL   // while the AP is up, re-try saved/home/hotspot this often
#endif
#ifndef WIFI_SETUP_TRIAL_TIMEOUT_MS
#define WIFI_SETUP_TRIAL_TIMEOUT_MS 20000UL  // time allowed for a newly entered network
#endif
#ifndef WIFI_SETUP_AP_LINGER_MS
#define WIFI_SETUP_AP_LINGER_MS    20000UL   // keep the AP up after success so the page can show the IP
#endif

// Serial diagnostics (115200 baud). Set to 0 to drop the log strings and save flash.
// Passwords are never printed — only SSIDs and password *lengths*.
#ifndef WIFI_DEBUG_LOG
#define WIFI_DEBUG_LOG 1
#endif
#if WIFI_DEBUG_LOG
#define WLOG(...) Serial.printf(__VA_ARGS__)
#else
#define WLOG(...) do {} while (0)
#endif

namespace {
DNSServer dns;

const char* netName(uint8_t n) { return n == 0 ? "Home" : n == 1 ? "Hotspot" : "Saved"; }

#if WIFI_DEBUG_LOG
const char* statusName(int st) {
  switch (st) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network not found)";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (wrong password / security)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "other";
  }
}

const char* discReason(uint8_t r) {
  switch (r) {
    case 2:   return "AUTH_EXPIRE";
    case 8:   return "ASSOC_LEAVE (left/kicked by AP)";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (wrong password?)";
    case 200: return "BEACON_TIMEOUT (signal lost)";
    case 201: return "NO_AP_FOUND (SSID not visible on 2.4GHz / out of range / hidden)";
    case 202: return "AUTH_FAIL (wrong password / security mismatch)";
    case 203: return "ASSOC_FAIL (AP refused: hotspot full or incompatible)";
    case 204: return "HANDSHAKE_TIMEOUT (wrong password?)";
    case 205: return "CONNECTION_FAIL";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY (e.g. WPA3-only hotspot)";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD (too weak)";
    default:  return "other";
  }
}

const char* authName(int a) {
  switch (a) {
    case 0: return "OPEN";   case 1: return "WEP";       case 2: return "WPA";
    case 3: return "WPA2";   case 4: return "WPA/WPA2";  case 5: return "WPA2-ENT";
    case 6: return "WPA3";   case 7: return "WPA2/WPA3"; default: return "?";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  char ssid[33];
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
      uint8_t n = info.wifi_sta_connected.ssid_len;
      if (n > 32) n = 32;
      memcpy(ssid, info.wifi_sta_connected.ssid, n);
      ssid[n] = '\0';
      WLOG("[wifi] Associated with '%s' on channel %u\n", ssid, (unsigned)info.wifi_sta_connected.channel);
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      WLOG("[wifi] Got IP %s  RSSI %d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t n = info.wifi_sta_disconnected.ssid_len;
      if (n > 32) n = 32;
      memcpy(ssid, info.wifi_sta_disconnected.ssid, n);
      ssid[n] = '\0';
      WLOG("[wifi] Disconnected from '%s', reason %u: %s\n", ssid,
           (unsigned)info.wifi_sta_disconnected.reason, discReason(info.wifi_sta_disconnected.reason));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      WLOG("[wifi] Setup AP: a device joined\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      WLOG("[wifi] Setup AP: a device left\n");
      break;
    default:
      break;
  }
}
#endif
const char NVS_NS[] = "deskwifi";

const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>AI Desk Companion</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 14px}input,select,button{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 14px;font-size:16px}button{background:#06c;color:#fff;border:0;border-radius:6px}.g{background:#666}</style></head><body><h2>AI Desk Companion</h2><h3>Wi-Fi Setup</h3><form method=post action=/save><label>Wi-Fi Name (SSID):</label><input name=ssid id=n maxlength=32 required autocapitalize=none autocorrect=off><label>Wi-Fi Password:</label><input name=password type=password maxlength=63><button>Save &amp; Connect</button></form><button class=g type=button onclick=sc()>Scan Wi-Fi Networks</button><select id=l style=display:none onchange="n.value=this.value"></select><p id=m></p><p><a href=/reset-wifi>Forget saved Wi-Fi</a></p><script>function sc(){m.textContent='Scanning...';fetch('/scan').then(r=>r.json()).then(j=>{if(!j.r){setTimeout(sc,1500);return}l.innerHTML='<option value="">Select a network</option>';j.n.forEach(s=>{var o=document.createElement('option');o.value=o.textContent=s;l.appendChild(o)});l.style.display='block';m.textContent=j.n.length+' network(s) found'}).catch(()=>m.textContent='Scan failed')}</script></body></html>)HTML";

const char STATUS_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>AI Desk Companion</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 14px}a{display:none;padding:10px;background:#06c;color:#fff;border-radius:6px;text-align:center;text-decoration:none}</style></head><body><h2>AI Desk Companion</h2><h3 id=t>Connecting...</h3><p id=s>Please wait...</p><p id=i></p><a id=b href=/>Try Again</a><script>function p(){fetch('/status').then(r=>r.text()).then(x=>{var a=x.split('|'),e=a.slice(2).join('|');if(a[0]=='2'){t.textContent='Connected!';s.textContent='SSID: '+e;i.textContent='IP Address: '+a[1]}else if(a[0]=='3'){t.textContent='Connection failed.';s.textContent='Please check the Wi-Fi name/password.';b.style.display='block'}else{s.textContent='SSID: '+e+' - please wait...';setTimeout(p,1500)}}).catch(()=>{s.textContent='Lost contact with the setup network. The device has most likely joined your Wi-Fi and is returning to normal operation.';setTimeout(p,3000)})}p()</script></body></html>)HTML";

const char RESET_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>AI Desk Companion</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 14px}button{width:100%;padding:10px;font-size:16px}</style></head><body><h2>AI Desk Companion</h2><p>Forget the Wi-Fi network saved from the setup page? The built-in Home/Hotspot networks are not affected.</p><form method=post action=/reset-wifi><button>Forget saved Wi-Fi</button></form></body></html>)HTML";

const char RESET_DONE_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>AI Desk Companion</title></head><body style="font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 14px"><h2>AI Desk Companion</h2><p>Saved Wi-Fi cleared.</p></body></html>)HTML";
}  // namespace
#endif

WiFiManager::WiFiManager()
  : connecting(false), connectStartTime(0), lastAttemptTime(0),
    lastHomeProbeTime(0), activeNetwork(255), attemptNetwork(255) {}

void WiFiManager::begin() {
#if DEMO_MODE
  // Nothing to do — isConnected() always reports true below.
#else
  loadSaved();
#if WIFI_DEBUG_LOG
  WiFi.onEvent(onWiFiEvent);
  WLOG("[wifi] Boot. Priority: Saved='%s' -> Home='%s' -> Hotspot='%s'  (empty = not configured)\n",
       hasSaved ? savedSsid : "", HOME_WIFI_SSID, HOTSPOT_WIFI_SSID);
#endif
  WiFi.mode(WIFI_STA);
  downTracking = true;
  downSince = millis();
  beginAttempt(firstNetwork(), millis());
#endif
}

// ---- network priority: 2 (saved via setup page) -> 0 (Home) -> 1 (Hotspot) ----

bool WiFiManager::networkConfigured(uint8_t network) const {
#if DEMO_MODE
  return false;
#else
  if (network == 0) return HOME_WIFI_SSID[0] != '\0';
  if (network == 1) return HOTSPOT_WIFI_SSID[0] != '\0';
  return hasSaved;
#endif
}

uint8_t WiFiManager::firstNetwork() const {
  static const uint8_t ORDER[3] = {2, 0, 1};
  for (uint8_t i = 0; i < 3; i++) if (networkConfigured(ORDER[i])) return ORDER[i];
  return 0;
}

uint8_t WiFiManager::nextNetwork(uint8_t after) const {
  static const uint8_t ORDER[3] = {2, 0, 1};
  uint8_t i = 0;
  while (i < 3 && ORDER[i] != after) i++;
  for (i++; i < 3; i++) if (networkConfigured(ORDER[i])) return ORDER[i];
  return 255;
}

uint8_t WiFiManager::configuredCount() const {
  return (uint8_t)networkConfigured(0) + (uint8_t)networkConfigured(1) + (uint8_t)networkConfigured(2);
}

void WiFiManager::beginAttempt(uint8_t network, unsigned long now) {
#if !DEMO_MODE
  const char* ssid = network == 0 ? HOME_WIFI_SSID : network == 1 ? HOTSPOT_WIFI_SSID : savedSsid;
  const char* password = network == 0 ? HOME_WIFI_PASSWORD : network == 1 ? HOTSPOT_WIFI_PASSWORD : savedPass;
  if (!ssid || !ssid[0]) {
    WLOG("[wifi] Skip %s: not configured\n", netName(network));
    connecting = false;
    attemptNetwork = network;
    const uint8_t next = nextNetwork(network);
    if (next != 255) beginAttempt(next, now);
    else if (setupMode) setupRetrying = false;
    return;
  }

  WLOG("[wifi] Attempt: %s '%s' (password %u chars, timeout %lu ms)\n",
       netName(network), ssid, (unsigned)strlen(password), (unsigned long)WIFI_CONNECT_TIMEOUT_MS);
  WiFi.disconnect(false, false);
  WiFi.begin(ssid, password);
  connecting = true;
  connectStartTime = now;
  lastAttemptTime = now;
  attemptNetwork = network;
#endif
}

void WiFiManager::startHomeAttempt(unsigned long now) {
#if !DEMO_MODE
  beginAttempt(firstNetwork(), now);
#endif
}

bool WiFiManager::isConnected() const {
#if DEMO_MODE
  return true;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

String WiFiManager::localIP() const {
#if DEMO_MODE
  return String("127.0.0.1");
#else
  return isConnected() ? WiFi.localIP().toString() : String();
#endif
}

String WiFiManager::currentSSID() const {
#if DEMO_MODE
  return String("DEMO");
#else
  return isConnected() ? WiFi.SSID() : String();
#endif
}

// ---- persistent runtime-saved network (NVS, namespace "deskwifi") ----

void WiFiManager::loadSaved() {
#if !DEMO_MODE
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  const String s = p.getString("ssid", "");
  const String w = p.getString("pass", "");
  p.end();
  if (s.length() > 0 && s.length() <= 32 && w.length() <= 63) {
    strlcpy(savedSsid, s.c_str(), sizeof(savedSsid));
    strlcpy(savedPass, w.c_str(), sizeof(savedPass));
    hasSaved = true;
  }
#endif
}

void WiFiManager::saveCredentials(const char *ssid, const char *pass) {
#if !DEMO_MODE
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
  }
  strlcpy(savedSsid, ssid, sizeof(savedSsid));
  strlcpy(savedPass, pass, sizeof(savedPass));
  hasSaved = true;
  Serial.print(F("[wifi] Saved network: "));
  Serial.println(savedSsid); // SSID only — the password is never logged
#endif
}

void WiFiManager::processForget(unsigned long now) {
#if !DEMO_MODE
  pendingForget = false;
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.clear();
    p.end();
  }
  const bool wasActive = hasSaved && activeNetwork == 2;
  hasSaved = false;
  savedSsid[0] = '\0';
  savedPass[0] = '\0';
  Serial.println(F("[wifi] Saved network forgotten"));
  if (wasActive && WiFi.status() == WL_CONNECTED) {
    // Drop the forgotten network so the normal priority (Home -> Hotspot -> setup AP) takes over.
    WiFi.disconnect(false, false);
    connecting = false;
    activeNetwork = 255;
    lastAttemptTime = 0;
  }
#endif
}

// ---- setup access point ----

void WiFiManager::startSetup(unsigned long now) {
#if !DEMO_MODE
  setupMode = true;
  setupRetrying = false;
  connecting = false;
  setupLastRetry = now;
  apStopAt = 0;
  trialActive = false;
  pendingConnect = false;
  trialState = 0;

  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP_STA);
  const char *apPass = WIFI_SETUP_AP_PASSWORD;
  WiFi.softAP(WIFI_SETUP_AP_SSID, apPass[0] ? apPass : nullptr);
  dns.start(53, "*", WiFi.softAPIP()); // captive portal: every name resolves to the ESP32
  WLOG("[wifi] No network connected for %lu s -> starting setup AP\n", (unsigned long)(WIFI_SETUP_AP_AFTER_MS / 1000UL));
#if WIFI_DEBUG_LOG
  diagScan = true;
  WiFi.scanNetworks(true); // async; results are printed from serviceSetup()
#endif
  Serial.print(F("[wifi] Setup AP started: "));
  Serial.print(F(WIFI_SETUP_AP_SSID));
  Serial.print(F("  ->  http://"));
  Serial.println(WiFi.softAPIP());
#endif
}

void WiFiManager::stopSetup() {
#if !DEMO_MODE
  dns.stop();
  WiFi.softAPdisconnect(true);
  setupMode = false;
  setupRetrying = false;
  apStopAt = 0;
  Serial.println(F("[wifi] Setup AP stopped"));
#endif
}

// Returns true when the normal reconnect logic in update() should also run.
bool WiFiManager::serviceSetup(unsigned long now) {
#if DEMO_MODE
  return true;
#else
  dns.processNextRequest();

#if WIFI_DEBUG_LOG
  if (diagScan) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      diagScan = false;
      WLOG("[wifi] Scan: %d network(s) visible on 2.4GHz (ESP32 cannot see 5GHz)\n", n);
      bool seenHome = false, seenHot = false, seenSaved = false;
      for (int i = 0; i < n; i++) {
        const String ss = WiFi.SSID(i);
        const bool isHome = HOME_WIFI_SSID[0] && ss == HOME_WIFI_SSID;
        const bool isHot = HOTSPOT_WIFI_SSID[0] && ss == HOTSPOT_WIFI_SSID;
        const bool isSaved = hasSaved && ss == savedSsid;
        seenHome |= isHome; seenHot |= isHot; seenSaved |= isSaved;
        WLOG("[wifi]   %2d. '%s'  ch%d  %d dBm  %s%s%s%s\n", i + 1, ss.c_str(), (int)WiFi.channel(i),
             (int)WiFi.RSSI(i), authName((int)WiFi.encryptionType(i)),
             isSaved ? "  <== SAVED" : "", isHome ? "  <== HOME" : "", isHot ? "  <== HOTSPOT" : "");
      }
      if (HOME_WIFI_SSID[0] && !seenHome)
        WLOG("[wifi]   Home '%s' NOT visible (off, out of range, or 5GHz-only)\n", HOME_WIFI_SSID);
      if (HOTSPOT_WIFI_SSID[0] && !seenHot)
        WLOG("[wifi]   Hotspot '%s' NOT visible: turn hotspot ON, keep the phone screen awake nearby, "
             "use 2.4GHz (iPhone: Maximize Compatibility), check exact spelling/case\n", HOTSPOT_WIFI_SSID);
      if (hasSaved && !seenSaved)
        WLOG("[wifi]   Saved '%s' NOT visible\n", savedSsid);
      WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
      diagScan = false; // the setup page's own scan took the results
    }
  }
#endif

  // A network entered on the setup page: try it with the AP still up so the
  // phone can read the result.
  if (!trialActive && pendingConnect) {
    pendingConnect = false;
    connecting = false;
    setupRetrying = false;
    WiFi.disconnect(false, false);
    WLOG("[wifi] Setup page: trying '%s' (password %u chars)\n", trialSsid, (unsigned)strlen(trialPass));
    WiFi.begin(trialSsid, trialPass);
    trialActive = true;
    trialStart = now;
    trialState = 1;
  }
  if (trialActive) {
    if (WiFi.status() == WL_CONNECTED) {
      saveCredentials(trialSsid, trialPass); // only good credentials are persisted
      activeNetwork = 2;
      attemptNetwork = 2;
      connecting = false;
      trialActive = false;
      trialState = 2;
      apStopAt = now + WIFI_SETUP_AP_LINGER_MS;
      WLOG("[wifi] Setup page: connected to '%s'\n", trialSsid);
    } else if (now - trialStart >= WIFI_SETUP_TRIAL_TIMEOUT_MS) {
      WLOG("[wifi] Setup page: '%s' failed, status=%s\n", trialSsid, statusName(WiFi.status()));
      WiFi.disconnect(false, false);
      trialActive = false;
      trialState = 3;
      setupLastRetry = now;
    }
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    setupRetrying = false;
    if (!apStopAt) apStopAt = now; // joined through the normal retry -> close the AP
    if ((long)(now - apStopAt) >= 0) stopSetup();
    return true;
  }

  apStopAt = 0;
  const bool hasClient = WiFi.softAPgetStationNum() > 0;
  if (setupRetrying) {
    if (hasClient) { // never disturb someone who is using the setup page
      connecting = false;
      setupRetrying = false;
      setupLastRetry = now;
      WiFi.disconnect(false, false);
      return false;
    }
    return true; // let the normal chain run (saved -> Home -> Hotspot)
  }
  if (!hasClient && now - setupLastRetry >= WIFI_SETUP_RETRY_MS) {
    setupRetrying = true;
    setupLastRetry = now;
    WLOG("[wifi] Setup AP: retrying saved/Home/Hotspot\n");
    beginAttempt(firstNetwork(), now);
  }
  return false;
#endif
}

// ---- web routes (added to the Draw Pad's existing AsyncWebServer on port 80) ----

void WiFiManager::registerSetupRoutes(AsyncWebServer &server) {
#if !DEMO_MODE
  // Setup page: only for clients on the setup AP. Draw Pad's "/" for Wi-Fi/LAN clients is untouched.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send_P(200, "text/html; charset=utf-8", SETUP_HTML);
  }).setFilter(ON_AP_FILTER);

  server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *r) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) { r->send(200, "application/json", "{\"r\":0}"); return; }
    if (n == WIFI_SCAN_FAILED) {
      if (trialState == 1) { r->send(200, "application/json", "{\"r\":1,\"n\":[]}"); return; }
      WiFi.scanNetworks(true);
      r->send(200, "application/json", "{\"r\":0}");
      return;
    }
    String j = F("{\"r\":1,\"n\":[");
    bool first = true;
    for (int i = 0; i < n && i < 20; i++) {
      const String s = WiFi.SSID(i);
      if (!s.length()) continue;
      String q = "\"";
      for (size_t k = 0; k < s.length(); k++) {
        const char c = s[k];
        if ((uint8_t)c < 0x20) continue;
        if (c == '"' || c == '\\') q += '\\';
        q += c;
      }
      q += '"';
      if (j.indexOf(q) >= 0) continue; // same SSID seen on several access points
      if (!first) j += ',';
      first = false;
      j += q;
    }
    j += F("]}");
    WiFi.scanDelete();
    r->send(200, "application/json", j);
  }).setFilter(ON_AP_FILTER);

  server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *r) {
    const AsyncWebParameter *ps = r->getParam("ssid", true);
    const AsyncWebParameter *pp = r->getParam("password", true);
    const String ssid = ps ? ps->value() : String();
    const String pass = pp ? pp->value() : String();
    if (ssid.length() < 1 || ssid.length() > 32) {
      r->send(400, "text/html", F("<p>Wi-Fi name must be 1-32 characters.</p><a href=/>Back</a>"));
      return;
    }
    if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
      r->send(400, "text/html", F("<p>Password must be 8-63 characters (leave empty for an open network).</p><a href=/>Back</a>"));
      return;
    }
    if (trialActive || pendingConnect) {
      r->send(409, "text/html", F("<p>Already connecting, please wait.</p><a href=/>Back</a>"));
      return;
    }
    strlcpy(trialSsid, ssid.c_str(), sizeof(trialSsid));
    strlcpy(trialPass, pass.c_str(), sizeof(trialPass));
    trialState = 1;
    pendingConnect = true; // picked up by update() in loop()
    r->send_P(200, "text/html; charset=utf-8", STATUS_HTML);
  }).setFilter(ON_AP_FILTER);

  // "state|ip|ssid" — state: 0 idle, 1 connecting, 2 connected, 3 failed. Never contains the password.
  server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *r) {
    String out = String((int)trialState);
    out += '|';
    if (trialState == 2) out += WiFi.localIP().toString();
    out += '|';
    out += trialSsid;
    r->send(200, "text/plain", out);
  }).setFilter(ON_AP_FILTER);

  // Forget the network saved from the setup page (works from the setup AP or the home LAN).
  server.on("/reset-wifi", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send_P(200, "text/html; charset=utf-8", RESET_HTML);
  });
  server.on("/reset-wifi", HTTP_POST, [this](AsyncWebServerRequest *r) {
    forgetDueAt = millis() + 1500UL; // let this response flush before any disconnect
    pendingForget = true;
    r->send_P(200, "text/html; charset=utf-8", RESET_DONE_HTML);
  });

  // Captive portal: phones probe /generate_204, /hotspot-detect.html, /connecttest.txt, /ncsi.txt ...
  // On the setup AP any unknown URL is redirected to the setup page; elsewhere it stays a plain 404.
  server.onNotFound([](AsyncWebServerRequest *r) {
    if (ON_AP_FILTER(r)) {
      r->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
    } else {
      r->send(404);
    }
  });
#endif
}

void WiFiManager::update() {
#if DEMO_MODE
  return;
#else
  const unsigned long now = millis();

  if (pendingForget && (long)(now - forgetDueAt) >= 0) processForget(now);
  if (setupMode && !serviceSetup(now)) return;

  if (WiFi.status() == WL_CONNECTED) {
    downTracking = false;
    if (connecting) {
      connecting = false;
      activeNetwork = attemptNetwork;
      WLOG("[wifi] Connected via %s: '%s'\n", netName(activeNetwork), WiFi.SSID().c_str());
      // When on the fallback network, periodically give the preferred network
      // another chance. If it fails, the chain restores the hotspot.
      if (activeNetwork == 1) lastHomeProbeTime = now;
    }

    if (activeNetwork == 1 && HOTSPOT_WIFI_SSID[0] &&
        now - lastHomeProbeTime >= WIFI_HOME_RETRY_INTERVAL_MS) {
      lastHomeProbeTime = now;
      WLOG("[wifi] On Hotspot - probing preferred network\n");
      startHomeAttempt(now);
    }
    return;
  }

  // Offline for long enough with nothing connecting -> open the setup AP.
  if (!downTracking) {
    downTracking = true;
    downSince = now;
  }
  if (!setupMode && now - downSince >= WIFI_SETUP_AP_AFTER_MS) {
    startSetup(now);
    return;
  }

  if (connecting) {
    if (now - connectStartTime < WIFI_CONNECT_TIMEOUT_MS) return;

    WLOG("[wifi] %s timed out after %lu ms, status=%s\n", netName(attemptNetwork),
         (unsigned long)(now - connectStartTime), statusName(WiFi.status()));
    connecting = false;
    // Saved/Home failed -> try the next network immediately. The last network
    // failed -> restart from the top (saved, else Home) so the preferred
    // network always gets priority. While the setup AP is up, one pass is
    // enough; the next one happens after WIFI_SETUP_RETRY_MS.
    const uint8_t next = nextNetwork(attemptNetwork);
    if (next != 255) {
      beginAttempt(next, now);
    } else if (setupMode) {
      setupRetrying = false;
      setupLastRetry = now;
      WiFi.disconnect(false, false);
    } else if (configuredCount() > 1) {
      beginAttempt(firstNetwork(), now);
    }
    return;
  }

  if (now - lastAttemptTime < WIFI_RECONNECT_INTERVAL_MS) return;

  // Every fresh cycle starts with the highest-priority network (saved, else
  // Home). This keeps the preferred network first without blocking the main
  // application loop.
  beginAttempt(firstNetwork(), now);
#endif
}
