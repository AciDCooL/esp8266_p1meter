  _____ ____  ____   ___  ____   __      ____  _      __  __ _____ _____ _____ ____  
 | ____/ ___||  _ \ ( _ )(  _ \ / /_    |  _ \/ |    |  \/  | ____|_   _| ____|  _ \ 
 |  _| \___ \| |_) |/ _ \/ _ \ | '_ \   | |_) | |    | |\/| |  _|   | | |  _| | |_) |
 | |___ ___) |  __/| (_)  (_) || (_) |  |  __/| |    | |  | | |___  | | | |___|  _ < 
 |_____|____/|_|    \___/\___/ \___/   |_|   |_|    |_|  |_|_____| |_| |_____|_| \_\

# ⚡ ESP8266 P1-METER ⚡ [v1.4.0 - 2026 EDITION]

![WebUI Preview](assets/webui_preview.svg)

> "Stabilizing the grid, one telegram at a time." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered in 2026 to fix legacy buffer overflows, memory fragmentation, and bring true "plug-and-play" Home Assistant integration.

---

## 🚀 WHAT'S NEW IN v1.4.0 (THE "ASYNC" PATCH)

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

### 📉 SMART CHANGE DETECTION (Efficiency)
Modern P1 meters (DSMR 5.0) blast data every second. Sending 20+ MQTT messages every second is a massive waste of WiFi juice and CPU cycles. 
**v1.2.0** caches the last value of every single sensor. It only hits the radio if the value moves or if the 20s "heartbeat" timer hits. Your Home Assistant gets instant updates on power spikes, but stays quiet when nothing is happening. 🤫

... (rest of sections unchanged) ...

### 📜 VERSION HISTORY
- **v1.4.0** - 2026-03-22: Non-blocking Async WebServer & ElegantOTA integration. OTA available 24/7 at `/update`.
- **v1.3.4** - 2026-03-22: Hotfix - Resolved reporting deadlock and added CRC debug logs.
- **v1.3.1** - 2026-03-22: Industrial Hardening. Implemented the **Sanity Shield** (spike protection), **Gated Availability** (boot verification), and **RTC Persistent State Tracking**.
- **v1.3.0** - 2026-03-22: Precision hardening and European locale support.
- **v1.2.6** - 2026-03-21: Implemented Dynamic HA Discovery.
- **v1.1.0** - Buffer overflow fixes and Crash milestones.
- **v1.0.0** - The OG release.

---

### ❤️ CREDITS
A huge thank you to [Daniel Jong](https://github.com/daniel-jong) for the original implementation and the foundation of this project.

**"Stay static, stay stable."** ✌️💀
