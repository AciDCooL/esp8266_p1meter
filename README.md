  _____ ____  ____   ___  ____   __      ____  _      __  __ _____ _____ _____ ____  
 | ____/ ___||  _ \ ( _ )(  _ \ / /_    |  _ \/ |    |  \/  | ____|_   _| ____|  _ \ 
 |  _| \___ \| |_) |/ _ \/ _ \ | '_ \   | |_) | |    | |\/| |  _|   | | |  _| | |_) |
 | |___ ___) |  __/| (_)  (_) || (_) |  |  __/| |    | |  | | |___  | | | |___|  _ < 
 |_____|____/|_|    \___/\___/ \___/   |_|   |_|    |_|  |_|_____| |_| |_____|_| \_\

# ⚡ ESP8266 P1-METER ⚡ [v1.6.3 - 2026 PRO EDITION]

![WebUI Preview](assets/webui_preview.svg)

> "Industrial-grade stability for the modern smart home." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered from the ground up to eliminate common ESP8266 pitfalls like heap fragmentation, buffer overflows, and serial timing drifts.

---

## 🚀 KEY FEATURES

*   **🩹 SELF-HEALING MACHINE (v1.6.2+):** Advanced state monitoring that automatically detects and fixes data stream desyncs. 
    *   **CRC Streak Guard:** Resets UART if 10 bad telegrams arrive in a row.
    *   **Stabilization Watchdog:** Force-recovers if the device is stuck "Stabilizing" for more than 5 mins.
    *   **Wedge Protection:** Silently re-inits the serial port if no data is seen for 60s.
*   **📊 PERSISTENT REBOOT COUNTER (v1.6.3):** Tracks reboots since the last hard power cycle using RTC SRAM. Perfect for identifying unstable firmware or OTA update cycles without physical access.
*   **🎯 INDUSTRIAL PRECISION:** Uses high-speed **Scaled Integers (x1000)** instead of slow software floats. Provides 100% accurate Voltage, Current, and Gas readings without CPU overhead.
*   **🛡️ BULLETPROOF BOOT v2:** UART initialization is delayed until the absolute end of setup. Automated hardware buffer purging ensures a clean start, preventing the "Stabilizing" lock.
*   **🔋 POWER STABILITY:** Fixed brownout issues by implementing `WIFI_NONE_SLEEP` mode and spreading MQTT publishes with 50ms delays.
*   **🧠 ANTI-FRAG ENGINE:** Zero dynamic memory allocations (`String`/`vector`) in the main loop. Prevents "Swiss cheese" heap fragmentation.
*   **🌐 24/7 BACKGROUND OTA:** Powered by **ElegantOTA v3**. The update portal at `/update` is always online and protected by HTTP Basic Auth.
*   **🏠 PRO HOME ASSISTANT INTEGRATION:** 
    *   **Dynamic Auto-Discovery:** Only registers the sensors your specific meter actually broadcasts.
    *   **Advanced Diagnostics:** Native reporting of MAC Address, Board Type, Firmware Version, MQTT Broker info, and Reboot Count.

---

## 🔌 HARDWARE WIRING

Connect your ESP8266 to an RJ11 cable following these diagrams. 

### Standard Wiring (External Power)
Recommended for most setups. A 10K resistor is often required between 3.3V and the Data (RXD) pin for signal stability.

| P1 pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- |
| 2 - RTS | 3.3v | Request to Send (Activates Port) |
| 3 - GND | GND | Ground |
| 5 - RXD | RX (GPIO3) | Data (10K pullup to 3.3V recommended) |

![Standard Wiring](assets/esp8266_p1meter_bb.png)

### Powered by Meter (DSMR 5.0+)
If your meter supports DSMR 5.0, it can power the ESP8266 directly via Pin 1 (Max 250mA).

| P1 pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- |
| 1 - 5V out | 5V / VIN | Power from Meter |
| 2 - RTS | 3.3v | Request to Send |
| 3 - GND | GND | Ground |
| 5 - RXD | RX (GPIO3) | Data |
| 6 - GND | GND | Ground |

![Powered by Meter](assets/esp8266_p1meter_bb_PoweredByMeter.png)

---

## 📡 DIAGNOSTICS & TELEMETRY

The Pro Edition provides deep system visibility within Home Assistant:
- **IP & MAC Address:** Fast identification on your network.
- **Board Type:** Detects if you are running on a Wemos D1 Mini or NodeMCU.
- **MQTT Server:** Shows exactly which broker and port the device is currently connected to.
- **Reboot Count:** Monitors software stability since the last cold boot.
- **Uptime Stability:** LWT state machine transitions: `offline` ➔ `stabilizing` ➔ `online`.

---

## 🎮 INSTALLATION & BUILD

### Option 1: PlatformIO (Recommended)
1. Open the folder in **VS Code** with **PlatformIO**.
2. Click **Upload**. Dependencies are handled automatically.

### Option 2: Arduino IDE
1. Open `esp8266_p1meter/esp8266_p1meter.ino`.
2. Install libraries: `WiFiManager`, `PubSubClient`, `DoubleResetDetector`, `ArduinoJson` (**v7.0+**), `ElegantOTA` (**v3.1.0+**).
3. Connect your board and click **Upload**.

---

### 📜 VERSION HISTORY (MAJOR MILESTONES)
- **v1.6.3** - 2026-03-24: **The Reboot Tracker.** Added a persistent reboot counter in RTC memory to track soft-reboots and update cycles since the last cold boot.
- **v1.6.2** - 2026-03-24: **The Self-Healing Update.** Automatically resets UART if it detects a CRC failure streak or a stabilization stall.
- **v1.6.1** - 2026-03-24: **The Power Stability Patch.** Implemented `WIFI_NONE_SLEEP` and MQTT spreading (50ms).
- **v1.6.0** - 2026-03-24: **The Diagnostics Update.** Added MAC, Board Type, and MQTT Server info.
- **v1.5.8** - 2026-03-22: **Bulletproof Boot.** Implemented late-start UART and hardware buffer purging.
- **v1.5.7** - 2026-03-22: **Industrial Precision.** Migrated telemetry to a Scaled Integer architecture.

---

### ❤️ CREDITS
Original work by [Daniel Jong](https://github.com/daniel-jong). 2026 Pro enhancements by AciDCooL.

**"Stay static, stay stable."** ✌️💀
