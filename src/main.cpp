#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ImprovWiFiLibrary.h>
#include "mbedtls/md.h"
#include <WebServer.h>  // minimal web page after Wi-Fi

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
WebServer server(80);

static uint8_t mac[6];
static char esp32mac[65];
static char topicCmd[80];
static char macColon[18];

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
  for (int i = 0; i < 6; i++) udp.write(0xFF);
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

// --------------------- Display ESP32 MAC address on Web page ---------------------
void handleRoot() {
  char page[1024];
  snprintf(page,sizeof(page),
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>ESP32 Device</title>"
    "<style>"
    "body{margin:0;height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;"
    "background:#0d1117;color:#c9d1d9;font-family:system-ui,sans-serif}"
    "h2{margin:.5rem 0;font-size:1.2rem;color:#8b949e}"
    "p.mac{font-family:monospace;font-size:2rem;background:#196f3d;color:#fff;padding:.5rem 1rem;"
    "border-radius:12px;border:1px solid #145a2e;margin:.25rem 0}"
    "p.note,a{font-size:.9rem;color:#8b949e;margin-top:.5rem}"
    "a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}"
    "</style></head><body>"
    "<h2>ESP32 MAC Address</h2>"
    "<p class=\"mac\">%s</p>"
    "<p class=\"note\">Copy this MAC for WOL site</p>"
    "<a href=\"https://wol.kreaxv.top/\" target=\"_blank\">wol.kreaxv.top</a>"
    "</body></html>",
    macColon
  );
  server.send(200,"text/html",page);
}

// ================= Main =================
void setup() {
  Serial.begin(115200);

  // Read MAC
  WiFi.macAddress(mac);

  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02x%02x%02x%02x%02x%02x",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  if (USE_HASHED_ID) sha256Hex(macHex, esp32mac);
  else strncpy(esp32mac, macHex, sizeof(esp32mac));

  snprintf(topicCmd, sizeof(topicCmd), "wol/%s", esp32mac);

  snprintf(macColon, sizeof(macColon), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  // Improv device info
  improvSerial.setDeviceInfo(
    (ImprovTypes::ChipFamily)0,
    macColon,  // show MAC in portal
    "",
    ""
  );

  // Let Improv handle AP / portal automatically
  WiFi.begin();  

  // Web server for after Wi-Fi connected
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  improvSerial.handleSerial();

  server.handleClient(); // handle minimal web page

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();
  }
}
