# Gemini Project Context: ESP8266 P1-METER

This file serves as the foundational mandate for all future AI interactions with this codebase. These instructions take precedence over general defaults.

## 🛡️ CORE ARCHITECTURAL MANDATE: THE "ANTI-FRAG" ENGINE
**Zero Heap Fragmentation Policy:**
- **NO Arduino `String` objects** are allowed in the main transmission/parsing loop.
- **NO `std::vector` or `std::string`** allocations are allowed.
- All string manipulation must be done using fixed-size `char` buffers, `snprintf`, `dtostrf`, and direct `char*` pointer iteration.
- **Rationale:** The ESP8266 has limited RAM. Dynamic allocations cause "Swiss cheese" heap fragmentation, leading to crashes after several days of uptime. Static allocation ensures infinite stability.

## 🏠 HOME ASSISTANT & MQTT STANDARDS
- **Auto-Discovery:** Must use the `homeassistant/sensor/p1meter_[CHIPID]/[SENSOR_ID]/config` topic structure.
- **Device Grouping:** All entities must be linked to a single Device via the `device` JSON object in the discovery payload.
- **Unique IDs:** Every entity must have a `unique_id` prefixed with the physical Chip ID to allow multiple meters on one network.
- **Network Stability:** When publishing large bursts of discovery messages (20+ topics), use `mqtt_client.loop()`, `yield()`, and small delays (20ms) to prevent TCP buffer overflows in the ESP8266 WiFi stack.
- **LWT (Last Will & Testament):** Connection must always register a "status" topic with "online"/"offline" payloads.

## 🧱 HARDWARE & MEMORY SAFETY
- **EEPROM Wear Leveling:** All `write_eeprom` operations must perform a "read-before-write" check. Only write to flash if the new data differs from the existing byte.
- **RTC Persistent State Tracking:** Critical totals (kWh, Gas) must be stored in RTC User Memory to survive soft reboots and OTA updates without Flash wear.
- **Sanity Shield:** Implement validation checks to discard physically impossible readings (spikes).
- **Buffer Limits:**
    - `telegram` buffer: 2050 bytes (DSMR 5.0 lines can be long).
    - `static_mqtt_buffer`: 1024 bytes (for JSON payloads).
- **Milestone Tracking:** Use RTC user memory to track boot milestones (`Booting`, `WiFi`, `MQTT`, etc.) to provide post-mortem autopsy reports after a crash.

## 🎨 WEB-UI & BRANDING
- **Style:** "Hacker 2026" aesthetic (Black background, terminal-green `#0f0` monospace text, glowing borders).
- **Hardcoded Branding:** Manufacturer, Model, and Base Device Name are hardcoded in `settings.h`. No WebUI configuration for these fields.
- **Async WebServer:** (v1.4.0) Use `ESPAsyncWebServer` for a non-blocking UI. The update portal at `/update` must remain online 24/7 without affecting the P1 loop. (v1.4.2) Migrated to `ElegantOTA` v3 for improved reliability.

## 🛠️ CODING STYLE
- **Prototypes:** Always maintain function prototypes at the top of the `.ino` file to prevent scope/compilation errors.
- **Locale Awareness:** The P1 parser must handle both `.` and `,` decimal separators to support various European smart meter models.
- **Precision:** Current, Voltage, and Gas must be transmitted as high-precision floats (scaled by 1000 in raw datagram, divided by 1000.0 before MQTT publish).

---
**Current Stable Version:** 1.6.9
**Project State:** Production-Ready / Optimized.
