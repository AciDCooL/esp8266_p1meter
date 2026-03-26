  _____ ____  ____   ___  ____   __      ____  _      __  __ _____ _____ _____ ____  
 | ____/ ___||  _ \ ( _ )(  _ \ / /_    |  _ \/ |    |  \/  | ____|_   _| ____|  _ \ 
 |  _| \___ \| |_) |/ _ \/ _ \ | '_ \   | |_) | |    | |\/| |  _|   | | |  _| | |_) |
 | |___ ___) |  __/| (_)  (_) || (_) |  |  __/| |    | |  | | |___  | | | |___|  _ < 
 |_____|____/|_|    \___/\___/ \___/   |_|   |_|    |_|  |_|_____| |_| |_____|_| \_\

# ⚡ ESP8266 P1-METER ⚡ [v1.6.0 - 2026 PRO EDITION]

![WebUI Preview](assets/webui_preview.svg)

> "Industrial-grade stability for the modern smart home." 🛠️💊

This is a high-performance, ultra-stable P1 Meter firmware for the **Wemos D1 Mini / ESP8266**. Re-engineered from the ground up to eliminate common ESP8266 pitfalls like heap fragmentation, buffer overflows, and serial timing drifts.

---

## 🚀 KEY FEATURES

*   **🎯 INDUSTRIAL PRECISION (v1.5.7+):** Uses high-speed **Scaled Integers (x1000)** instead of slow software floats. Provides 100% accurate Voltage, Current, and Gas readings without the CPU overhead that causes data loss.
*   **🛡️ BULLETPROOF BOOT (v1.5.8+):** Delayed UART initialization and automated buffer purging ensure the hardware starts with a clean slate, preventing the "Stabilizing" lock caused by early boot noise.
*   **🧠 ANTI-FRAG ENGINE:** Zero dynamic memory allocations (`String`/`vector`) in the main loop. Prevents "Swiss cheese" heap fragmentation, allowing for months of continuous uptime.
*   **🌐 24/7 BACKGROUND OTA:** Powered by **ElegantOTA v3**. The update portal at `/update` is always online and protected by HTTP Basic Auth, running smoothly alongside the P1 parser.
*   **🏠 PRO HOME ASSISTANT INTEGRATION:** 
    *   **Dynamic Auto-Discovery:** Only registers the sensors your specific meter actually broadcasts.
    *   **Energy Dashboard Ready:** Pre-configured with the correct `state_class` and `device_class` for native Home Assistant support.
    *   **Advanced Diagnostics (v1.6.0):** Native reporting of MAC Address, Board Type, Firmware Version, and current MQTT Broker connection info.
*   **💾 RESILIENT STATE TRACKING:** 
    *   **RTC Persistence:** Critical totals (kWh/Gas) are stored in SRAM to survive soft-reboots.
    *   **Sanity Shield:** Discards physically impossible spikes (>10kWh/s) to keep your history clean.
    *   **Post-MortEM Reports:** Captures reset reasons and boot milestones for easy debugging over MQTT.

---

## 🔌 HARDWARE WIRING

Connect your ESP8266 to an RJ11 cable following these diagrams. 

**Note:** When using a 4-pin RJ11 connector (instead of 6-pin), pin 1 and 6 are simply missing. The first physical pin is pin 2 and the last is pin 5.

### Standard Wiring (External Power)
Recommended for most setups. A 10K resistor is often required between 3.3V and the Data (RXD) pin for signal stability.

| P1 pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- |
| 2 - RTS | 3.3v | Request to Send (Activates Port) |
| 3 - GND | GND | Ground |
| 5 - RXD | RX (GPIO3) | Data (10K pullup to 3.3V recommended) |

![Standard Wiring](assets/esp8266_p1meter_bb.png)

### Powered by Meter (DSMR 5.0+)
If your meter supports DSMR 5.0, it can power the ESP8266 directly via Pin 1.

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

In addition to energy metrics, the Pro Edition provides deep system visibility:
- **IP & MAC Address:** Fast identification on your network.
- **Board Type:** Detects if you are running on a Wemos D1 Mini or NodeMCU.
- **MQTT Server:** Shows exactly which broker and port the device is currently connected to.
- **Uptime Stability:** LWT state machine transitions: `offline` ➔ `stabilizing` ➔ `online`.

---

## 🎮 INSTALLATION & BUILD

### Option 1: PlatformIO (Recommended)
1. Open the project folder in **VS Code** with the **PlatformIO** extension.
2. Edit `esp8266_p1meter/settings.h` if you wish to change hardcoded branding.
3. Click **Upload**. PlatformIO handles all library dependencies automatically.

### Option 2: Arduino IDE
1. Open `esp8266_p1meter/esp8266_p1meter.ino`.
2. Install the following libraries via **Tools > Manage Libraries**:
   - `WiFiManager` by tzapu (v2.0+)
   - `PubSubClient` by Nick O'Leary
   - `DoubleResetDetector` by Stephen Denne
   - `ArduinoJson` by Benoit Blanchon (**v7.0+**)
   - `ElegantOTA` by Ayush Sharma (**v3.1.0+**)
3. Connect your board and click **Upload**.

### First Boot Setup
1. Connect to the `p1meter` WiFi Access Point.
2. Enter your WiFi and MQTT credentials in the neon "Hacker UI".
3. Set your custom **WebUI / OTA Password** to secure the device.
4. Click **Save**. Home Assistant will automatically discover the meter within 30 seconds.

---

### 📜 VERSION HISTORY (MAJOR MILESTONES)
- **v1.6.0** - 2026-03-24: **The Diagnostics Update.** Added native HA sensors for MAC Address, Board Type, Firmware Version, and MQTT Server info. Unified sensor categorization.
- **v1.5.8** - 2026-03-22: **Bulletproof Boot.** Implemented late-start UART and hardware buffer purging to resolve race conditions during OTA reboots.
- **v1.5.7** - 2026-03-22: **Industrial Precision.** Migrated telemetry to a Scaled Integer architecture, eliminating slow software float math.
- **v1.5.4** - 2026-03-22: Improved ElegantOTA reliability and graceful MQTT disconnects.
- **v1.5.1** - 2026-03-22: Added dynamic OTA password support to the WebUI.
- **v1.5.0** - 2026-03-22: Initial Pro-Series release with ElegantOTA v3 and Anti-Frag Engine.

---

### ❤️ CREDITS
Based on the original work by [Daniel Jong](https://github.com/daniel-jong). 2026 Pro enhancements by AciDCooL.

**"Stay static, stay stable."** ✌️💀
