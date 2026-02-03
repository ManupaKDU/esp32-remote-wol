# esp32-remote-wol

Remote Wake-on-LAN firmware for ESP32 boards.

Wake a PC remotely over the internet, even when your home network is behind CGNAT
or has no public IP.

The ESP32 connects **outbound** to a public MQTT broker and sends Wake-on-LAN
packets on your local network.

No inbound connections.  
No port forwarding.  
No public IP required.

---

## How it works

1. ESP32 connects to Wi-Fi and an MQTT broker
2. A unique **Device ID** is generated
3. Web request publishes a hashed command
4. ESP32 listens only to its own topic
5. Valid command triggers a WoL packet

> Wake-on-LAN must already work on the target PC.

---

## Web interface

https://wol.kreaxv.top/

- Flash firmware in-browser (ESP Web Tools)
- Configure Wi-Fi
- Wake a PC using Device ID and MAC address

Firmware is open source and can also be built manually.

---

## Supported ESP32 boards

Chip-family based. Board vendor does not matter.

- **ESP32**
- **ESP32-S2**
- **ESP32-S3**
- **ESP32-C3**

Select the matching PlatformIO environment.

---

## Building from source

- Open in PlatformIO
- Select the matching environment
- Build and upload

See `platformio.ini`.

---

## Security notes

- Hashed command identifiers
- Device only accepts its own commands
- No inbound access to the local network

---

## License

MIT
