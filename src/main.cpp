// Headless ESP32 WoL listener:
// connects to Wi-Fi + MQTT, waits for WoL commands, self-recovers on network loss

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>

#include <WiFiManager.h>   // tzapu/WiFiManager
#include "mbedtls/sha256.h"

// ----------------------- MQTT -----------------------
static const char* MQTT_HOST = "724f4005ddac40d5a4d1586443333e56.s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT = 8883;
static const char* MQTT_USER = "client";
static const char* MQTT_PASS = "BJa938Cguzds4fx";

// ----------------------- Globals -----------------------
WiFiClientSecure tls;
PubSubClient mqtt(tls);
WiFiUDP udp;

static String rawId;
static String deviceHash;
static String TOPIC_CMD;

// ---- runtime wifi watchdog ----
// policy: if Wi-Fi drops -> reconnect every 20s, reboot after 180s
static uint32_t wifiLostAt   = 0;
static uint32_t lastWifiKick = 0;

// --------------------- SHA-256 ---------------------
static String sha256Hex(const String& input) {
  uint8_t out[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, (const unsigned char*)input.c_str(), input.length());
  mbedtls_sha256_finish_ret(&ctx, out);
  mbedtls_sha256_free(&ctx);

  char buf[65];
  for (int i = 0; i < 32; i++) sprintf(buf + i * 2, "%02x", out[i]);
  buf[64] = 0;
  return String(buf);
}

// --------------------- MAC ---------------------
static bool parseMac(const String& macStr, uint8_t out[6]) {
  int v[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
             &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

// --------------------- WOL ---------------------
static void sendWOL(const uint8_t mac[6]) {
  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (int i = 6; i < 102; i += 6) memcpy(packet + i, mac, 6);

  udp.begin(0);
  udp.beginPacket(WiFi.broadcastIP(), 9);
  udp.write(packet, sizeof(packet));
  udp.endPacket();
}

// --------------------- WiFi Manager ---------------------
// Initial Wi-Fi setup:
// - connect to last saved Wi-Fi
// - open config portal only if needed
static void ensureWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setMinimumSignalQuality(60);

  String html =
    "<b>Device ID:</b><br>"
    "<span style='font-size:36px;font-weight:800;'>"
    + rawId +
    "</span>";

  WiFiManagerParameter info(html.c_str());
  wm.addParameter(&info);

  wm.autoConnect();
}

// --------------------- MQTT ---------------------
// MQTT payload must be MAC string: "AA:BB:CC:DD:EE:FF"
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_CMD) return;

  String msg;
  for (unsigned i = 0; i < length; i++) msg += (char)payload[i];

  uint8_t mac[6];
  if (parseMac(msg, mac)) sendWOL(mac);
}

static void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  // retry throttle (non-blocking)
  static uint32_t nextTry = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextTry) < 0) return;
  nextTry = now + 3000; // try every 3 seconds

  // (safe to call repeatedly)
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  tls.setInsecure();

  // attempt one connect
  if (mqtt.connect(("wol-" + deviceHash.substring(0,16)).c_str(),
                   MQTT_USER, MQTT_PASS)) {
    mqtt.subscribe(TOPIC_CMD.c_str());
  }
}

// ================= Main runtime =================
void setup() {
  rawId = String((uint32_t)ESP.getEfuseMac(), HEX);
  rawId.toLowerCase();

  deviceHash = sha256Hex(rawId);

  // Subscribe to: wol/<sha256(deviceId)>
  TOPIC_CMD = "wol/" + deviceHash;

  ensureWiFi();
  connectMQTT(); // tries once
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (wifiLostAt == 0) wifiLostAt = now;

    if ((uint32_t)(now - lastWifiKick) > 20000) {
      lastWifiKick = now;
      WiFi.reconnect();
    }

    if ((uint32_t)(now - wifiLostAt) > 180000) ESP.restart();

    delay(50);
    return;
  }

  wifiLostAt = 0;
  lastWifiKick = 0;

  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
}
