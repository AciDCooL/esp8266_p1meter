
  ______  _____ _____   ___ ___   __   __   ___  __   __ ___ _____ ___ ___ 
 |  ____|/ ____|  __ \ / _ \__ \ / /  /_ | |__ \/_ | / /|__ \____ |__ \__ \
 | |__  | (___ | |__) | (_) | ) / /_   | |    ) || |/ /_   ) |  / /  ) | ) |
 |  __|  \___ \|  ___/ \__, |/ / '_ \  | |   / / | | '_ \ / /  / /  / / / / 
 | |____ ____) | |       / /|_| (_) |  | |  / /_ | | (_) / /_ / /  / /_|_| 
 |______|_____/|_|      /_/(_) \___/   |_| |____||_|\___/____/_/  |____(_)
                                                                           
# ⚡ ESP8266 P1-METER ⚡ [v1.2.0 - 2026 EDITION]

> "Stabilizing the grid, one telegram at a time." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered in 2026 to fix legacy buffer overflows and memory fragmentation.

---

## 🚀 WHAT'S NEW IN v1.2.0 (THE "NO-CRASH" PATCH)

We went through the code with a fine-toothed comb to ensure this thing runs for months without a stutter. 

*   **⚡ 1s REAL-TIME UPDATES:** Polling interval reduced from 30s to 1s. Catch those power spikes as they happen.
*   **🧠 HEAP PROTECTION:** Scrubbed all `String` objects from the transmission loop. No more memory "Swiss cheese" fragmentation.
*   **🕵️ CHANGE DETECTION:** Only sends MQTT data if the value actually changes (with a 60s heartbeat). Saves your WiFi and your MQTT broker from unnecessary noise.
*   **🛡️ STACK SAFETY:** Moved large MQTT buffers to static memory. Say goodbye to Stack Overflows.
*   **💾 CRASH REPORTING:** If it reboots, it tells you *why* over MQTT (`/last_reset`). Milestone tracking pinpointing exactly where it failed.
*   **🧱 BUFFER HARDENING:** Fixed the infamous `telegram` array overflow that was wiping WiFi settings.

---

## 🧠 DEEP DIVE: ARCHITECTURAL IMPROVEMENTS

### 🛡️ THE "ANTI-FRAG" ENGINE (Heap Protection)
The original code used `std::string` and `String` objects for every MQTT topic and payload. On an ESP8266, this is a death sentence. Every time you create a `String`, it allocates RAM. When you delete it, it leaves a "hole." Eventually, you have no big holes left (fragmentation), and the device crashes.
**v1.2.0** uses `snprintf` and `ltoa` on fixed, pre-allocated char buffers. Zero allocations = zero fragmentation = infinite uptime. ♾️

### 📉 SMART CHANGE DETECTION (Efficiency)
Modern P1 meters (DSMR 5.0) blast data every second. Sending 20+ MQTT messages every second is a massive waste of WiFi juice and CPU cycles. 
**v1.2.0** caches the last value of every single sensor. It only hits the radio if the value moves or if the 60s "heartbeat" timer hits. Your Home Assistant gets instant updates on power spikes, but stays quiet when nothing is happening. 🤫

### 🧬 POST-MORTEM TELEMETRY (Crash Debugging)
Ever wonder why your ESP just "stopped" responding? **v1.2.0** captures the `ResetReason` from the bootloader and the `Milestone` from RTC memory. 
- **Milestones:** We track if it was `Booting`, `WiFi Connecting`, `Reading P1`, or `Sending MQTT` when it died. 
- **MQTT Report:** On the next boot, it publishes the full autopsy report to `.../last_reset`. No serial cable needed to see why it crashed! 🕵️‍♂️

### 🧱 STACK & BUFFER HARDENING
The `telegram` buffer was too small (2048) and the code was writing two bytes past the end (`\n\0`). This "silent killer" would overwrite the WiFi configuration in RAM, causing a reboot and triggering the `DoubleResetDetector` to wipe your settings. 
**v1.2.0** increased the buffer to 2050, added strict bounds checking, and moved the 1024-byte MQTT JSON buffer to **Static Memory** to keep the Stack lean and mean.

---

## 🛠️ THE TECH STACK

- **Core:** ESP8266 (Arduino Framework)
- **Protocol:** DSMR 5.0+ (with inverted Serial RX)
- **Messaging:** MQTT (PubSubClient)
- **Config:** WiFiManager (Auto-AP mode if connection fails)
- **OTA:** Wireless updates ready

---

## 📡 MQTT TOPICS

Your automations stay the same. We kept the legacy structure but made it faster.

| Topic | Description |
| :--- | :--- |
| `sensors/power/p1meter/actual_consumption` | Instant W usage |
| `sensors/power/p1meter/last_reset` | **NEW:** Post-mortem crash report |
| `hass/status` | Online/Offline status with Version ID |

---

## 🎮 INSTALLATION

1. Open `esp8266_p1meter.ino` in VS Code + PlatformIO.
2. Edit `settings.h` for your specific needs (though defaults are now ⚡ cracked).
3. Flash that 💩.
4. If it's your first time, connect to the `p1meter` AP and feed it your MQTT creds.

---

### 📜 VERSION HISTORY
- **v1.2.0** - 2026-03-20: Memory optimization, change detection, and 1s updates.
- **v1.1.0** - Buffer overflow fixes, MQTT buffer increase, and Crash milestones.
- **v1.0.0** - The OG release.

**"Stay static, stay stable."** ✌️💀
