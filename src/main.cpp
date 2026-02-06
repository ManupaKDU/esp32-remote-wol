// Headless ESP32 WoL listener:
// connects to Wi-Fi + MQTT, waits for WoL commands, self-recovers on network loss

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>

#include <WiFiManager.h>      // tzapu/WiFiManager
#include "mbedtls/md.h"       // stable SHA-256 wrapper (mbedtls_md)

// ================= Device ID mode =================
//
// USE_HASHED_ID = 1 -> topic uses SHA-256(rawId)
// USE_HASHED_ID = 0 -> topic uses rawId
//
// MQTT:
//   Topic:   wol/<device-id>
//   Payload: "AA:BB:CC:DD:EE:FF"
//
#define USE_HASHED_ID 1

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
static String deviceId;   // rawId or sha256(rawId)
static String TOPIC_CMD;

// ---- runtime wifi watchdog ----
// policy: if Wi-Fi drops -> reconnect every 20s, reboot after 180s
static uint32_t wifiLostAt   = 0;
static uint32_t lastWifiKick = 0;

// --------------------- SHA-256 (clean + portable) ---------------------
static String sha256Hex(const String& input) {
  uint8_t out[32];

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return String();

  int rc = mbedtls_md(info,
                      (const unsigned char*)input.c_str(),
                      input.length(),
                      out);
  if (rc != 0) return String();

  char buf[65];
  for (int i = 0; i < 32; i++) sprintf(buf + i * 2, "%02x", out[i]);
  buf[64] = 0;
  return String(buf);
}

// --------------------- MAC ---------------------
static bool parseMac(const String& macStr, uint8_t out[6]) {
  int v[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
             &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
    return false;

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
static void ensureWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

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
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_CMD) return;

  String msg;
  msg.reserve(length);
  for (unsigned i = 0; i < length; i++) msg += (char)payload[i];

  uint8_t mac[6];
  if (parseMac(msg, mac)) sendWOL(mac);
}

static void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  static uint32_t nextTry = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextTry) < 0) return;
  nextTry = now + 3000;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  tls.setInsecure(); // (optional) replace with CA cert if you want strict TLS

  String clientId = "wol-" + deviceId.substring(0, 16);
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    mqtt.subscribe(TOPIC_CMD.c_str());
  }
}

// ================= Main runtime =================
void setup() {
  // Important: ESP.getEfuseMac() is 64-bit; this keeps your old behavior
  rawId = String((uint32_t)ESP.getEfuseMac(), HEX);
  rawId.toLowerCase();

#if USE_HASHED_ID
  deviceId  = sha256Hex(rawId);
#else
  deviceId  = rawId;
#endif

  TOPIC_CMD = "wol/" + deviceId;

  ensureWiFi();
  connectMQTT();
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
