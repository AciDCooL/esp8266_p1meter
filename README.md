
  ______  _____ _____   ___ ___   __   __   ___  __   __ ___ _____ ___ ___ 
 |  ____|/ ____|  __ \ / _ \__ \ / /  /_ | |__ \/_ | / /|__ \____ |__ \__ \
 | |__  | (___ | |__) | (_) | ) / /_   | |    ) || |/ /_   ) |  / /  ) | ) |
 |  __|  \___ \|  ___/ \__, |/ / '_ \  | |   / / | | '_ \ / /  / /  / / / / 
 | |____ ____) | |       / /|_| (_) |  | |  / /_ | | (_) / /_ / /  / /_|_| 
 |______|_____/|_|      /_/(_) \___/   |_| |____||_|\___/____/_/  |____(_)
                                                                           
# ⚡ ESP8266 P1-METER ⚡ [v1.2.2 - 2026 EDITION]

> "Stabilizing the grid, one telegram at a time." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered in 2026 to fix legacy buffer overflows, memory fragmentation, and bring true "plug-and-play" Home Assistant integration.

---

## 🚀 WHAT'S NEW IN v1.2.2 (THE "NO-CRASH" PATCH)

We went through the code with a fine-toothed comb to ensure this thing runs for months without a stutter. 

*   **⚡ 1s REAL-TIME UPDATES:** Polling interval reduced from 30s to 1s. Catch those power spikes as they happen.
*   **🧠 HEAP PROTECTION (Anti-Frag Engine):** Scrubbed all `String` and `std::vector` objects from the transmission loop. We parse the datagram entirely in-place. Zero allocations = zero memory "Swiss cheese" fragmentation = infinite uptime.
*   **🏠 HOME ASSISTANT AUTO-DISCOVERY:** Enable it in the WebUI, and your meter instantly appears in Home Assistant with 24 fully configured sensors. No YAML required!
    *   **Native Energy Dashboard:** Fully tagged with the correct `state_class` and `device_class` to work out-of-the-box with HA's built-in Energy tracking.
    *   **LWT (Last Will & Testament):** Your meter now has a persistent status topic. If it loses power, Home Assistant immediately marks all 24 sensors as "Unavailable".
    *   **Diagnostic Sensors:** Monitor the ESP's **WiFi RSSI (Signal Strength)** and **Local IP Address** straight from the Home Assistant device page.
    *   **TCP Overflow Protection:** (v1.2.1) The massive discovery payload is now metered to prevent the ESP8266 WiFi stack from choking on boot.
*   **🕵️ SMART CHANGE DETECTION:** Only sends MQTT data if the value actually changes (with a 20s heartbeat). Saves your WiFi and your MQTT broker from unnecessary noise.
*   **🛡️ STACK SAFETY:** Moved large MQTT buffers to static memory. Say goodbye to Stack Overflows.
*   **💾 CRASH REPORTING:** If it reboots, it tells you *why* over MQTT (`/last_reset`). Milestone tracking pinpointing exactly where it failed.
*   **😎 HACKER WEB-UI:** The WiFi config portal has been upgraded with a lean, neon-green "terminal" aesthetic.
*   **🧱 BUFFER & EEPROM HARDENING:** Fixed the infamous `telegram` array overflow that was wiping WiFi settings, and added a read-before-write check to massively extend EEPROM lifespan.

---

## 🧠 DEEP DIVE: ARCHITECTURAL IMPROVEMENTS

### 🛡️ THE "ANTI-FRAG" ENGINE (Heap Protection)
The original code used `std::string`, `String`, and vectors for parsing. On an ESP8266, this is a death sentence. Every time you create a `String`, it allocates RAM. 
**v1.2.0** uses `snprintf`, direct `char*` pointer iteration, and fixed buffers. It reads the 13-month peak history directly out of the raw byte array without creating a single copy. 

### 📉 SMART CHANGE DETECTION (Efficiency)
Modern P1 meters (DSMR 5.0) blast data every second. Sending 20+ MQTT messages every second is a massive waste of WiFi juice and CPU cycles. 
**v1.2.0** caches the last value of every single sensor. It only hits the radio if the value moves or if the 20s "heartbeat" timer hits. Your Home Assistant gets instant updates on power spikes, but stays quiet when nothing is happening. 🤫

### 🧬 POST-MORTEM TELEMETRY (Crash Debugging)
Ever wonder why your ESP just "stopped" responding? **v1.2.0** captures the `ResetReason` from the bootloader and the `Milestone` from RTC memory. 
- **Milestones:** We track if it was `Booting`, `WiFi Connecting`, `Reading P1`, or `Sending MQTT` when it died. 
- **MQTT Report:** On the next boot, it publishes the full autopsy report to `.../last_reset`.

---

## 🛠️ THE TECH STACK

- **Core:** ESP8266 (Arduino Framework)
- **Protocol:** DSMR 5.0+ (with inverted Serial RX)
- **Messaging:** MQTT (PubSubClient)
- **Config:** WiFiManager (Auto-AP mode if connection fails)
- **OTA:** Wireless updates ready

---

## 📡 MQTT TOPICS & METRICS

Your automations stay the same. We kept the legacy structure but made it faster and added missing 3-Phase metrics.

**Core Topics:**
| Topic | Description |
| :--- | :--- |
| `sensors/power/p1meter/actual_consumption` | Instant W usage |
| `sensors/power/p1meter/l1_instant_power_usage` | **NEW:** L1, L2, L3 Usage (W) |
| `sensors/power/p1meter/l1_instant_power_returndelivery` | **NEW:** L1, L2, L3 Return (W) |
| `sensors/power/p1meter/last_reset` | **NEW:** Post-mortem crash report |
| `hass/status` | Online/Offline status with Version ID |

*(Note: Enabling Home Assistant Auto-Discovery does **not** change these topics. It just maps them for HA automatically).*

---

## 🎮 INSTALLATION

### Option 1: PlatformIO (Recommended)
1. Open the project folder in **VS Code** with the **PlatformIO** extension.
2. Edit `esp8266_p1meter/settings.h` for your specific needs (though defaults are now ⚡ cracked).
3. Click **Upload**. PlatformIO will automatically download all required libraries and flash the board.

### Option 2: Arduino IDE
1. Open `esp8266_p1meter/esp8266_p1meter.ino` in the Arduino IDE.
2. Go to **Sketch > Include Library > Manage Libraries...** and install the following:
   - `WiFiManager` by tzapu (v2.0+)
   - `PubSubClient` by Nick O'Leary
   - `DoubleResetDetector` by Stephen Denne
   - `ArduinoJson` by Benoit Blanchon (v7.0+)
3. Edit `settings.h`, connect your board, and click **Upload**.

### First Boot Setup
1. Connect to the new `p1meter` WiFi Access Point.
2. You'll be greeted by the neon Hacker UI.
3. Feed it your WiFi and MQTT credentials.
4. Check the **"Enable HA Auto-Discovery"** box if you use Home Assistant.
5. Click Save. It will reboot and start streaming data!

### 🔄 How to change settings later (Re-accessing the WebUI)
For security and performance, the WebUI **shuts down** once the meter connects to your WiFi. If you ever need to change your MQTT broker IP or WiFi password, use the **Double Reset** method:
1. Press the physical **RST (Reset) button** on your D1 Mini.
2. Wait a second, then **press it again** (must be within 10 seconds of the first press).
3. The internal LED will start flashing rapidly, indicating the settings have been wiped.
4. The `p1meter` Access Point and WebUI will now be broadcasting again.

---

### 📜 VERSION HISTORY
- **v1.2.2** - 2026-03-20: Hotfix - Fixed HA Auto-Discovery topic structure to properly group all entities into a single "Device" in Home Assistant. Upgraded Hacker WebUI to be mobile-responsive.
- **v1.2.1** - 2026-03-20: Hotfix - MQTT TCP buffer overflow protection and Client ID collision fix during HA discovery burst.
- **v1.2.0** - 2026-03-20: HA Auto-Discovery, Hacker UI, 3-Phase Metrics, Zero-Allocation Anti-Frag Engine.
- **v1.1.0** - Buffer overflow fixes, MQTT buffer increase, and Crash milestones.
- **v1.0.0** - The OG release.

**"Stay static, stay stable."** ✌️💀
