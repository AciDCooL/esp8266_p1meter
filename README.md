  _____ ____  ____   ___  ____   __      ____  _      __  __ _____ _____ _____ ____  
 | ____/ ___||  _ \ ( _ )(  _ \ / /_    |  _ \/ |    |  \/  | ____|_   _| ____|  _ \ 
 |  _| \___ \| |_) |/ _ \/ _ \ | '_ \   | |_) | |    | |\/| |  _|   | | |  _| | |_) |
 | |___ ___) |  __/| (_)  (_) || (_) |  |  __/| |    | |  | | |___  | | | |___|  _ < 
 |_____|____/|_|    \___/\___/ \___/   |_|   |_|    |_|  |_|_____| |_| |_____|_| \_\

# ⚡ ESP8266 P1-METER ⚡ [v1.5.7 - 2026 EDITION]

![WebUI Preview](assets/webui_preview.svg)

> "Stabilizing the grid, one telegram at a time." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered in 2026 to fix legacy buffer overflows, memory fragmentation, and bring true "plug-and-play" Home Assistant integration.

---

## 🚀 WHAT'S NEW IN v1.5.7 (THE "INDUSTRIAL" PATCH)

We went through the code with a fine-toothed comb to ensure this thing runs for months without a stutter. 

*   **⚡ ZERO-BLOCKING SERIAL (v1.5.3):** Completely refactored the P1 serial reading logic into a background state machine. Features **Smart Garbage Collection** to instantly discard fragmented packets. The loop remains fluid even during 115200 baud bursts.
*   **🌐 24/7 OTA WEB-UI:** The update portal at `/update` is now always online, powered by **ElegantOTA v3**. It runs alongside the P1 loop without disrupting real-time data flow.
*   **🔒 OTA SECURITY (v1.5.1):** The OTA portal is protected by HTTP Basic Auth (`admin`). You can dynamically set your custom password in the main configuration portal.
*   **🎯 INDUSTRIAL PRECISION (v1.5.7):** Architectural overhaul for metric accuracy. Replaced slow software floating-point math with high-speed **Scaled Integers (x1000)**. Voltage, Current, and Gas are natively preserved with 100% precision without the CPU overhead that caused stabilization locks in earlier versions.
*   **🛡️ THE SANITY SHIELD (v1.3.1):** Every telegram is validated against physically impossible spikes. If a reading jumps more than 10kWh in 1s, it's discarded to protect your HA Energy Dashboard.
*   **💾 RTC STATE AWARENESS (v1.3.1):** Critical totals are stored in **RTC User Memory** (SRAM). Survives soft-reboots and updates without wearing out Flash memory.
*   **🧠 HEAP PROTECTION (Anti-Frag Engine):** Total eradication of `String` and `std::vector` from the main loop. Zero allocations = zero memory fragmentation = infinite uptime.
*   **🏠 PRO HOME ASSISTANT INTEGRATION:** 
    *   **Dynamic Discovery:** ESP "listens" to your meter to only register sensors that actually exist.
    *   **Native Energy Dashboard:** Fully tagged with `state_class` and `device_class` for instant HA Energy support.
    *   **LWT (Last Will & Testament):** Instantly alerts HA if the device loses power or disconnects.
*   **💾 CRASH REPORTING:** If it reboots, it tells you *why* over MQTT (`/last_reset`) with milestone tracking.

---

## 🧠 DEEP DIVE: ARCHITECTURAL IMPROVEMENTS

### 🛡️ THE "ANTI-FRAG" ENGINE (Heap Protection)
The original code used `std::string`, `String`, and vectors for parsing. On an ESP8266, this causes "Swiss cheese" heap fragmentation, leading to crashes. **v1.5.0+** uses `snprintf`, fixed buffers, and direct pointer iteration. 

### 🎯 SCALED INTEGER ARCHITECTURE (Precision vs Speed)
The ESP8266 lacks a hardware Floating Point Unit (FPU). Software float math is slow and can cause the serial buffer to overflow at 115200 baud. 
**v1.5.7** stores all telemetry (Voltage, Current, Gas) as **Scaled Integers (x1000)**.
- **Internal:** `230.1 V` is stored as `230100` (milliVolts).
- **MQTT:** The decimal point is manually inserted (`230.100`) before transmission.
This provides the precision of a float with the raw speed of integer math, ensuring zero serial data loss.

### 📉 SMART CHANGE DETECTION (Efficiency)
Caches the last value of every sensor. It only hits the radio if the value moves or if the 20s "heartbeat" timer hits, saving WiFi juice and broker noise. 🤫

---

## 🔌 CONNECTING TO THE P1 METER

Connect the esp8266 to an RJ11 cable following the diagram.

| P1 pin | ESP8266 Pin |
| :--- | :--- |
| 2 - RTS | 3.3v |
| 3 - GND | GND |
| 5 - RXD (data) | RX (gpio3) |

#### Standard Wiring:
![Standard Wiring](assets/esp8266_p1meter_bb.png)

#### Optional: Powering using your DSMR5+ meter
| P1 pin | ESP8266 Pin |
| :--- | :--- |
| 1 - 5v out | 5v or Vin |
| 2 - RTS | 3.3v |
| 3 - GND | GND |
| 5 - RXD (data) | RX (gpio3) |
| 6 - GND | GND |

---

## 📡 MQTT TOPICS & METRICS

**Core Topics:**
| Topic | Description |
| :--- | :--- |
| `sensors/power/p1meter/actual_consumption` | Instant W usage |
| `sensors/power/p1meter/l1_voltage` | L1, L2, L3 Voltage (V) |
| `sensors/power/p1meter/gas_meter_m3` | Gas Meter (m³) |
| `sensors/power/p1meter/last_reset` | Post-mortem crash report |
| `hass/status` | Online/Offline/Stabilizing status |

---

## 🎮 INSTALLATION

### Option 1: PlatformIO (Recommended)
1. Open in **VS Code** with the **PlatformIO** extension.
2. Edit `esp8266_p1meter/settings.h`.
3. Click **Upload**.

### Option 2: Arduino IDE
1. Open `esp8266_p1meter/esp8266_p1meter.ino`.
2. Install these libraries via Library Manager:
   - `WiFiManager` by tzapu (v2.0+)
   - `PubSubClient` by Nick O'Leary
   - `DoubleResetDetector` by Stephen Denne
   - `ArduinoJson` by Benoit Blanchon (v7.0+)
   - `ElegantOTA` by Ayush Sharma (**v3.1.0+**)
3. Connect your board and click **Upload**.

---

### 📜 VERSION HISTORY
- **v1.5.7** - 2026-03-22: "Industrial Scaled Integer" Precision Patch. Replaced slow floating-point math with high-speed integer scaling (x1000). Values like Voltage and Gas now use manual decimal insertion for 100% precision. Removed top-level comments for a cleaner `.ino`.
- **v1.5.4** - 2026-03-22: Fixed ElegantOTA UI "Upload Failed" bug and improved MQTT graceful disconnect.
- **v1.5.3** - 2026-03-22: Smart Garbage Collection serial parser. Waits for clean start byte (`/`) to prevent fragmented packets.
- **v1.5.1** - 2026-03-22: Added Dynamic OTA Password field to WebUI.
- **v1.5.0** - 2026-03-22: Implemented ElegantOTA security tweaks and 24/7 background portal.
- **v1.4.9** - 2026-03-22: Increased Hardware Serial RX buffer to 2048 bytes.
- **v1.4.6** - 2026-03-22: Reverted to Standard WebServer for Arduino IDE compatibility.
- **v1.4.4** - 2026-03-22: Non-blocking Serial parsing refactor.
- **v1.3.1** - 2026-03-22: Industrial Hardening (Sanity Shield, RTC Persistence).
- **v1.2.6** - 2026-03-21: Implemented Dynamic HA Discovery.
- **v1.0.0** - The OG release.

---

### ❤️ CREDITS
A huge thank you to [Daniel Jong](https://github.com/daniel-jong) for the original implementation.

**"Stay static, stay stable."** ✌️💀
