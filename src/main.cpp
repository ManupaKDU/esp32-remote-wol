#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ImprovWiFiLibrary.h>
#include "mbedtls/md.h"
// ================= ESP32 MAC mode =================
//
// USE_HASHED_ID = 1 -> topic uses SHA-256(esp32mac)
// USE_HASHED_ID = 0 -> topic uses esp32mac
//
// MQTT:
//   Topic:   wol/<esp32-mac>
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
ImprovWiFi improvSerial(&Serial);

static uint8_t mac[6];
static char esp32mac[65];
static char topicCmd[80];

// --------------------- SHA-256 ---------------------
static void sha256Hex(const char* input, char* output) {
  uint8_t hash[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (mbedtls_md(info, (const unsigned char*)input, strlen(input), hash) != 0) {
    output[0] = 0;
    return;
  }
  for (int i = 0; i < 32; i++) sprintf(output + i * 2, "%02x", hash[i]);
  output[64] = 0;
}

// --------------------- Hex Parsing ---------------------
static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

// --------------------- WOL ---------------------
static void sendWOL(const char* macStr) {
  uint8_t targetMac[6];
  for (int i = 0; i < 6; i++) {
    targetMac[i] = (hexNibble(macStr[i*3]) << 4) | hexNibble(macStr[i*3 + 1]);
  }

  udp.beginPacket(WiFi.broadcastIP(), 9);

  // Send 6 x 0xFF header
  for (int i = 0; i < 6; i++) udp.write(0xFF);

  // Send 16 repetitions of MAC directly
  for (int i = 0; i < 16; i++) udp.write(targetMac, 6);

  udp.endPacket();
}

// --------------------- MQTT ---------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char msg[32] = {0};
  int len = length < sizeof(msg)-1 ? length : sizeof(msg)-1;
  memcpy(msg, payload, len);
  sendWOL(msg);
}

static void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  tls.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  char clientId[40];
  snprintf(clientId, sizeof(clientId), "wol-%.*s", 16, esp32mac);

  if (mqtt.connect(clientId, MQTT_USER, MQTT_PASS)) {
    mqtt.subscribe(topicCmd);
  }
}

// ================= Main =================
void setup() {
  Serial.begin(115200);

  WiFi.macAddress(mac);

  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02x%02x%02x%02x%02x%02x",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  if (USE_HASHED_ID) sha256Hex(macHex, esp32mac);
  else strncpy(esp32mac, macHex, sizeof(esp32mac));

  snprintf(topicCmd, sizeof(topicCmd), "wol/%s", esp32mac);

  char macColon[18];
  snprintf(macColon, sizeof(macColon), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  char headerLabel[32];
  snprintf(headerLabel, sizeof(headerLabel), "MAC: %s", macColon);

  improvSerial.setDeviceInfo(
    (ImprovTypes::ChipFamily)0,
    headerLabel,
    "",
    ""
  );

  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Improv handles AP/portal
}

void loop() {
  improvSerial.handleSerial();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();
  }
}
