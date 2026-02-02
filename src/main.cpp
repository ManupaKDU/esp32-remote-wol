#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>

#include <WiFiManager.h>   // tzapu/WiFiManager
#include "mbedtls/sha256.h"

// ===================== MQTT =====================
static const char* MQTT_HOST = "724f4005ddac40d5a4d1586443333e56.s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT = 8883;
static const char* MQTT_USER = "client";
static const char* MQTT_PASS = "BJa938Cguzds4fx";

// ===================== Globals =====================
WiFiClientSecure tls;
PubSubClient mqtt(tls);
WiFiUDP udp;

static String rawId;
static String deviceHash;
static String TOPIC_CMD;

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

// ---------------- WiFi Manager ----------------
static void ensureWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  wm.setMinimumSignalQuality(60);

  // Minimal HTML, big ID, acceptable spacing
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
  for (unsigned i = 0; i < length; i++) msg += (char)payload[i];

  uint8_t mac[6];
  if (parseMac(msg, mac)) sendWOL(mac);
}

static void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  tls.setInsecure();

  while (!mqtt.connected()) {
    mqtt.connect(("wol-"+deviceHash.substring(0,16)).c_str(), MQTT_USER, MQTT_PASS);
    delay(500);
  }
  mqtt.subscribe(TOPIC_CMD.c_str());
}

// --------------------- Arduino ---------------------
void setup() {
  Serial.begin(115200);

  rawId = String((uint32_t)ESP.getEfuseMac(), HEX);
  rawId.toLowerCase();

  deviceHash = sha256Hex(rawId);
  TOPIC_CMD = "wol/" + deviceHash;

  ensureWiFi();
  connectMQTT();

  Serial.println("Device ID: " + rawId);
}

void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
}
