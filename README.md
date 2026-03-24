  _____ ____  ____   ___  ____   __      ____  _      __  __ _____ _____ _____ ____  
 | ____/ ___||  _ \ ( _ )(  _ \ / /_    |  _ \/ |    |  \/  | ____|_   _| ____|  _ \ 
 |  _| \___ \| |_) |/ _ \/ _ \ | '_ \   | |_) | |    | |\/| |  _|   | | |  _| | |_) |
 | |___ ___) |  __/| (_)  (_) || (_) |  |  __/| |    | |  | | |___  | | | |___|  _ < 
 |_____|____/|_|    \___/\___/ \___/   |_|   |_|    |_|  |_|_____| |_| |_____|_| \_\

# ⚡ ESP8266 P1-METER ⚡ [v1.4.1 - 2026 EDITION]

![WebUI Preview](assets/webui_preview.svg)

> "Stabilizing the grid, one telegram at a time." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered in 2026 to fix legacy buffer overflows, memory fragmentation, and bring true "plug-and-play" Home Assistant integration.

---

## 🚀 WHAT'S NEW IN v1.4.1 (THE "DEBUG" PATCH)

We went through the code with a fine-toothed comb to ensure this thing runs for months without a stutter. 

*   **🌐 NON-BLOCKING WEB-UI (v1.4.0):** The web server is now **Asynchronous**. It runs 24/7 without slowing down the P1 meter parsing. You can now perform browser-based OTA updates at `/update` anytime!
*   **🛡️ THE SANITY SHIELD (v1.3.1):** Every P1 telegram is now validated against physically impossible spikes. If a reading jumps more than 10kWh in 1 second, it is automatically discarded. No more vertical spikes in your HA Energy Dashboard!
*   **💾 RTC STATE AWARENESS (v1.3.1):** Critical totals are stored in **RTC User Memory** (SRAM). It survives soft-reboots and OTA updates without wearing out the Flash memory.
*   **⚡ 1s REAL-TIME UPDATES:** Polling interval reduced from 30s to 1s. Catch those power spikes as they happen.
*   **🧠 HEAP PROTECTION (Anti-Frag Engine):** Scrubbed all `String` and `std::vector` objects from the transmission loop. We parse the datagram entirely in-place. Zero allocations = zero memory "Swiss cheese" fragmentation = infinite uptime.
*   **🏠 PRO HOME ASSISTANT INTEGRATION:** 
    *   **Dynamic Discovery:** ESP now "listens" for 30s to see which sensors your meter actually provides before registering them in HA.
    *   **LWT (Last Will & Testament):** If it loses power, Home Assistant immediately marks all sensors as "Unavailable".
*   **🕵️ SMART CHANGE DETECTION:** Only sends MQTT data if the value actually changes (with a 20s heartbeat).
*   **💾 CRASH REPORTING:** If it reboots, it tells you *why* over MQTT (`/last_reset`). Milestone tracking pinpointing exactly where it failed.
*   **😎 HACKER WEB-UI:** The WiFi config portal has been upgraded with a lean, neon-green "terminal" aesthetic. Fully mobile-responsive and state-aware.

---

## 🧠 DEEP DIVE: ARCHITECTURAL IMPROVEMENTS

### 🛡️ THE "ANTI-FRAG" ENGINE (Heap Protection)
The original code used `std::string`, `String`, and vectors for parsing. On an ESP8266, this is a death sentence. Every time you create a `String`, it allocates RAM. 
**v1.4.0** uses `snprintf`, direct `char*` pointer iteration, and fixed buffers. It reads data directly out of the raw byte array without creating copies. 

---

## 🔌 CONNECTING TO THE P1 METER

Connect the esp8266 to an RJ11 cable/connector following the diagram.

**Note:** when using a 4-pin RJ11 connector (instead of a 6-pin connector), pin 1 and 6 are the pins that are not present, so the first pin is pin 2 and the last pin is pin 5

| P1 pin | ESP8266 Pin |
| :--- | :--- |
| 2 - RTS | 3.3v |
| 3 - GND | GND |
| 4 - | |
| 5 - RXD (data) | RX (gpio3) |

On most Landys and Gyr models a 10K resistor should be used between the ESP's 3.3v and the p1's DATA (RXD) pin. Many howto's mention RTS requires 5V (VIN) to activate the P1 port, but for me 3V3 suffices.

### Standard Wiring:
![Standard Wiring](assets/esp8266_p1meter_bb.png)

#### Optional: Powering the esp8266 using your DSMR5+ meter

When using a 6 pin cable you can use the power source provided by the meter.

| P1 pin | ESP8266 Pin |
| :--- | :--- |
| 1 - 5v out | 5v or Vin |
| 2 - RTS | 3.3v |
| 3 - GND | GND |
| 4 - | |
| 5 - RXD (data) | RX (gpio3) |
| 6 - GND | GND |

### Powered by Meter Wiring:
![Powered by Meter](assets/esp8266_p1meter_bb_PoweredByMeter.png)

---

## 📡 MQTT TOPICS & METRICS

**Core Topics:**
| Topic | Description |
| :--- | :--- |
| `sensors/power/p1meter/actual_consumption` | Instant W usage |
| `sensors/power/p1meter/l1_instant_power_usage` | L1, L2, L3 Usage (W) |
| `sensors/power/p1meter/l1_voltage` | L1, L2, L3 Voltage (V) |
| `sensors/power/p1meter/frequency` | Line Frequency (Hz) |
| `sensors/power/p1meter/gas_meter_m3` | Gas Meter (m³) |
| `sensors/power/p1meter/last_reset` | Post-mortem crash report |
| `hass/status` | Online/Offline status with Version ID |

---

## 🎮 INSTALLATION

### Option 1: PlatformIO (Recommended)
1. Open the project folder in **VS Code** with the **PlatformIO** extension.
2. Edit `esp8266_p1meter/settings.h` for your specific needs.
3. Click **Upload**. PlatformIO will automatically download all required libraries and flash the board.

### Option 2: Arduino IDE
1. Open `esp8266_p1meter/esp8266_p1meter.ino` in the Arduino IDE.
2. Go to **Sketch > Include Library > Manage Libraries...** and install:
   - `WiFiManager` by tzapu (v2.0+)
   - `PubSubClient` by Nick O'Leary
   - `DoubleResetDetector` by Stephen Denne
   - `ArduinoJson` by Benoit Blanchon (v7.0+)
   - `ESPAsyncTCP` by me-no-dev
   - `ESP Async WebServer` by me-no-dev
   - `AsyncElegantOTA` by Ayush Sharma (**v2.2.8**)
3. Edit `settings.h`, connect your board, and click **Upload**.

---

### 📜 VERSION HISTORY
- **v1.4.1** - 2026-03-22: Enhanced Serial Debugging. Added raw P1 line printing and increased serial timeout to 500ms.
- **v1.4.0** - 2026-03-22: Non-blocking Async WebServer & ElegantOTA integration. OTA available 24/7 at `/update`.
- **v1.3.4** - 2026-03-22: Hotfix - Resolved reporting deadlock and added CRC debug logs.
- **v1.3.1** - 2026-03-22: Industrial Hardening (Sanity Shield, RTC Persistence).
- **v1.3.0** - 2026-03-22: Precision hardening and European locale support.
- **v1.2.6** - 2026-03-21: Implemented Dynamic HA Discovery.
- **v1.1.0** - Buffer overflow fixes and Crash milestones.
- **v1.0.0** - The OG release.

---

### ❤️ CREDITS
A huge thank you to [Daniel Jong](https://github.com/daniel-jong) for the original implementation.

**"Stay static, stay stable."** ✌️💀
