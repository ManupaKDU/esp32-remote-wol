# esp32-remote-wol

Remote **Wake-on-LAN** firmware for ESP32 boards.

Wake a PC remotely over the internet, even if your home network is behind **CGNAT**
or has **no public IP**.

---

## How it works

1. ESP32 connects to Wi-Fi  
2. ESP32 connects outbound to an MQTT broker  
3. A **device ID** is generated from the ESP32 hardware ID  
4. ESP32 subscribes to its own MQTT topic  
5. A valid MQTT message triggers a Wake-on-LAN packet  

> Wake-on-LAN must already work on the local network before using this remotely.

---

## Web interface (official demo)

**https://wol.kreaxv.top/**

Features:
- Flash firmware directly in the browser (ESP Web Tools)
- Configure Wi-Fi
- Wake a PC using **Device ID** and **MAC address**

---

## Device ID modes (important)

This firmware supports two operating modes depending on how you want to send
Wake-on-LAN commands.

---

### Mode 1: Official demo web + default broker (recommended)

Use the firmware **as-is** with default settings.

- No configuration needed
- Uses the official demo web interface
- Uses the default MQTT broker

Best choice for most users.

---

### Mode 2: Your own MQTT broker + client

Use this mode if you want **full control** and do not use the demo web or broker.

#### Firmware setup

- Set `USE_HASHED_ID = false`
- Replace:
  - `MQTT_HOST`
  - `MQTT_PORT`
  - `MQTT_USER`
  - `MQTT_PASS`

with your own broker credentials.

#### MQTT command format

- **Topic:** `wol/<device-id>`
- **Payload:** `AA:BB:CC:DD:EE:FF`

#### Example

- Topic: `wol/1a2b3c4d`
- Payload: `3C:52:82:11:9A:EF`

> Messages on other topics or with invalid payloads are ignored.

---

## Supported ESP32 boards

Chip-family based.  
Board vendor and layout do **not** matter.

Supported chip families:
- **ESP32**
- **ESP32-S2**
- **ESP32-S3**
- **ESP32-C3**

---

## Building from source

1. Open the project in **PlatformIO**
2. Select the matching environment
3. Build and upload

See `platformio.ini` for available environments.

---

## Security notes

- Device listens only to its own MQTT topic
- Optional SHA-256 based device identifiers
- No inbound connections to the local network
- No exposed services
- No port forwarding required

---

## License

MIT
