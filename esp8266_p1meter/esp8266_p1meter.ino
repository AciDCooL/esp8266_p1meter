/* 
 * ESP8266 P1 Meter
 * Version Management:
 * 1.0.0 - Original version
 * 1.1.0 - Fixed buffer overflow, increased MQTT buffer, added crash reporting & milestones
 * 1.2.0 - Added change detection, heap protection (no String objects), static MQTT buffer, and 1s updates.
 * 1.2.1 - Hotfix for MQTT TCP buffer overflow and Client ID collisions.
 * 1.2.2 - Hotfix for HA Device Grouping (Topic structure) and Mobile UI scaling.
 * 1.2.3 - Hotfix for WebUI Save button visibility and dynamic EEPROM defaults.
 */
#define VERSION "1.2.3"

#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <DoubleResetDetector.h>
#include <ArduinoJson.h>

// * Include settings
#include "settings.h"

// * Initiate Double resetDetector
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

// * Initiate led blinker library
Ticker ticker;

// * Initiate WIFI client
WiFiClient espClient;

// * Initiate MQTT client
PubSubClient mqtt_client(espClient);

// * Reset and Milestone tracking
char last_reset_info[128] = "";
struct {
    uint32_t marker; // To verify RTC memory is valid
    uint32_t milestone;
} rtc_data;

#define RTC_MARKER 0xDEADBEEF
#define RTC_BASE_ADDR 64 // Offset from DRD to avoid collision

void set_milestone(uint32_t m) {
    rtc_data.milestone = m;
    rtc_data.marker = RTC_MARKER;
    ESP.rtcUserMemoryWrite(RTC_BASE_ADDR, (uint32_t*)&rtc_data, sizeof(rtc_data));
}

// **********************************
// * WIFI                           *
// **********************************

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());

    // * If you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, tick);
}

// **********************************
// * Ticker (System LED Blinker)    *
// **********************************

// * Blink on-board Led
void tick()
{
    // * Toggle state
    int state = digitalRead(LED_BUILTIN); // * Get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state);    // * Set pin to the opposite state
}

// **********************************
// * MQTT                           *
// **********************************

// * Send a message to a broker topic
void send_mqtt_message(const char *topic, char *payload)
{
    bool result = mqtt_client.publish(topic, payload, false);

    if (!result)
    {
        Serial.printf("MQTT publish to topic %s failed\n", topic);
    }
}

// * send a Json message to a broker topic
void send_mqtt_message(const char *topic, JsonDocument& doc)
{
    static char static_mqtt_buffer[MQTT_BUFFER_SIZE]; 
    size_t n = serializeJson(doc, static_mqtt_buffer, sizeof(static_mqtt_buffer));
    mqtt_client.publish(topic, (uint8_t*)static_mqtt_buffer, n);
    doc.clear(); 
}

// **********************************
// * Home Assistant Auto-Discovery  *
// **********************************

struct HASensor {
    const char* id;
    const char* name;
    const char* unit;
    const char* device_class;
    const char* state_class;
    const char* icon;
};

void publish_ha_discovery() {
    if (!HA_AUTO_DISCOVERY) return;
    
    Serial.println(F("Publishing HA Auto-Discovery..."));

    const HASensor sensors[] = {
        {"consumption_low_tarif", "Consumption Low Tarif", "Wh", "energy", "total_increasing", ""},
        {"consumption_high_tarif", "Consumption High Tarif", "Wh", "energy", "total_increasing", ""},
        {"returndelivery_low_tarif", "Return Low Tarif", "Wh", "energy", "total_increasing", ""},
        {"returndelivery_high_tarif", "Return High Tarif", "Wh", "energy", "total_increasing", ""},
        {"actual_consumption", "Actual Consumption", "W", "power", "measurement", ""},
        {"actual_returndelivery", "Actual Return", "W", "power", "measurement", ""},
        {"l1_instant_power_usage", "L1 Power Usage", "W", "power", "measurement", ""},
        {"l2_instant_power_usage", "L2 Power Usage", "W", "power", "measurement", ""},
        {"l3_instant_power_usage", "L3 Power Usage", "W", "power", "measurement", ""},
        {"l1_instant_power_returndelivery", "L1 Power Return", "W", "power", "measurement", ""},
        {"l2_instant_power_returndelivery", "L2 Power Return", "W", "power", "measurement", ""},
        {"l3_instant_power_returndelivery", "L3 Power Return", "W", "power", "measurement", ""},
        {"l1_instant_power_current", "L1 Current", "A", "current", "measurement", ""},
        {"l2_instant_power_current", "L2 Current", "A", "current", "measurement", ""},
        {"l3_instant_power_current", "L3 Current", "A", "current", "measurement", ""},
        {"l1_voltage", "L1 Voltage", "V", "voltage", "measurement", ""},
        {"l2_voltage", "L2 Voltage", "V", "voltage", "measurement", ""},
        {"l3_voltage", "L3 Voltage", "V", "voltage", "measurement", ""},
        {"gas_meter_m3", "Gas Meter", "m³", "gas", "total_increasing", ""},
        {"actual_average_15m_peak", "15m Average Peak", "W", "power", "measurement", ""},
        {"thismonth_max_15m_peak", "This Month Max Peak", "W", "power", "measurement", ""},
        {"last13months_average_15m_peak", "13 Months Avg Peak", "W", "power", "measurement", ""},
        // Diagnostic sensors
        {"wifi_rssi", "WiFi Signal", "dBm", "signal_strength", "measurement", "mdi:wifi"},
        {"ip_address", "IP Address", "", "", "", "mdi:network"}
    };

    char topic[128];
    char payload[600];
    
    char status_topic[128];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);

    // Declare the document once to reuse the same memory block for all 24 sensors
    JsonDocument doc;

    for (const auto& sensor : sensors) {
        doc.clear();
        
        snprintf(topic, sizeof(topic), "%s/sensor/p1meter/%s/config", HA_DISCOVERY_PREFIX, sensor.id);
        
        doc["name"] = sensor.name;
        
        char uid[64];
        snprintf(uid, sizeof(uid), "p1meter_%s", sensor.id);
        doc["unique_id"] = uid;
        
        char state_topic[128];
        snprintf(state_topic, sizeof(state_topic), "%s/%s", MQTT_ROOT_TOPIC, sensor.id);
        doc["state_topic"] = state_topic;
        
        // Availability linked to LWT
        doc["availability_topic"] = status_topic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        
        if (strlen(sensor.unit) > 0) doc["unit_of_measurement"] = sensor.unit;
        if (strlen(sensor.device_class) > 0) doc["device_class"] = sensor.device_class;
        if (strlen(sensor.state_class) > 0) doc["state_class"] = sensor.state_class;
        if (strlen(sensor.icon) > 0) doc["icon"] = sensor.icon;
        
        // If it's a diagnostic sensor, put it in the "diagnostic" category in HA
        if (strcmp(sensor.id, "wifi_rssi") == 0 || strcmp(sensor.id, "ip_address") == 0) {
            doc["entity_category"] = "diagnostic";
        }
        
        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0] = "esp8266_p1meter";
        device["name"] = "ESP8266 P1 Meter";
        device["manufacturer"] = "Custom";
        device["sw_version"] = VERSION;
        
        serializeJson(doc, payload, sizeof(payload));
        mqtt_client.publish(topic, payload, true); // Retained message
        
        // Process background tasks and MQTT ACKs to prevent TCP buffer overflow
        mqtt_client.loop();
        yield();
        delay(30); 
    }
    Serial.println(F("HA Auto-Discovery published."));
}

// * Reconnect to MQTT server and subscribe to in and out topics
bool mqtt_reconnect()
{
    Serial.println(F("Attempting MQTT connection..."));
    
    char status_topic[128];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);

    char client_id[64];
    snprintf(client_id, sizeof(client_id), "%s-%06X", HOSTNAME, ESP.getChipId());

    // * Attempt to connect with LWT (Last Will and Testament)
    // If the ESP drops off the network, the broker will publish "offline" to the status topic
    if (mqtt_client.connect(client_id, MQTT_USER, MQTT_PASS, status_topic, 1, true, "offline"))
    {
        Serial.println(F("MQTT connected!"));

        // * Once connected, publish online status
        mqtt_client.publish(status_topic, "online", true);

        // Publish the last reset report for debugging
        if (strlen(last_reset_info) > 0) {
            char report[200];
            snprintf(report, sizeof(report), "Version: %s | %s", VERSION, last_reset_info);
            char topic[128];
            snprintf(topic, sizeof(topic), "%s/last_reset", MQTT_ROOT_TOPIC);
            mqtt_client.publish(topic, report, true); // Retain this message
        }

        publish_ha_discovery();
        Serial.printf("MQTT root topic: %s\n", MQTT_ROOT_TOPIC);
        return true;
    }
    else
    {
        Serial.print(F("MQTT Connection failed: rc="));
        Serial.println(mqtt_client.state());
        return false;
    }
}

// Global storage for "Last Sent" values to enable change detection
long LAST_CON_LOW = -1, LAST_CON_HIGH = -1, LAST_RET_LOW = -1, LAST_RET_HIGH = -1;
long LAST_ACT_CON = -1, LAST_ACT_RET = -1, LAST_GAS = -1;
long LAST_L1_P = -1, LAST_L2_P = -1, LAST_L3_P = -1, LAST_L1_C = -1, LAST_L2_C = -1, LAST_L3_C = -1, LAST_L1_V = -1, LAST_L2_V = -1, LAST_L3_V = -1;
long LAST_L1_R = -1, LAST_L2_R = -1, LAST_L3_R = -1;
long LAST_TARIF = -1, LAST_S_OUT = -1, LAST_L_OUT = -1, LAST_S_DROP = -1, LAST_S_PEAK = -1;
long LAST_AVG_15M = -1, LAST_MAX_15M = -1, LAST_AVG_13MO = -1;
unsigned long LAST_HEARTBEAT = 0;

void send_metric(const char* name, long metric, long& last_value)
{
    // Heartbeat: Send every 20s even if no change, OR if value changed
    if (metric != last_value || (millis() - LAST_HEARTBEAT > 20000)) {
        char topic[128];
        char payload[16];
        
        // Build topic efficiently without using String objects
        snprintf(topic, sizeof(topic), "%s/%s", MQTT_ROOT_TOPIC, name);
        ltoa(metric, payload, 10);
        
        if (mqtt_client.publish(topic, payload, false)) {
            last_value = metric; 
        }
    }
}

void send_data_to_broker()
{
    set_milestone(5); // Milestone 5: Sending MQTT
    
    send_metric("consumption_low_tarif", CONSUMPTION_LOW_TARIF, LAST_CON_LOW);
    send_metric("consumption_high_tarif", CONSUMPTION_HIGH_TARIF, LAST_CON_HIGH);
    send_metric("returndelivery_low_tarif", RETURNDELIVERY_LOW_TARIF, LAST_RET_LOW);
    send_metric("returndelivery_high_tarif", RETURNDELIVERY_HIGH_TARIF, LAST_RET_HIGH);
    send_metric("actual_consumption", ACTUAL_CONSUMPTION, LAST_ACT_CON);
    send_metric("actual_returndelivery", ACTUAL_RETURNDELIVERY, LAST_ACT_RET);

    send_metric("l1_instant_power_usage", L1_INSTANT_POWER_USAGE, LAST_L1_P);
    send_metric("l2_instant_power_usage", L2_INSTANT_POWER_USAGE, LAST_L2_P);
    send_metric("l3_instant_power_usage", L3_INSTANT_POWER_USAGE, LAST_L3_P);
    send_metric("l1_instant_power_returndelivery", L1_INSTANT_POWER_RETURNDELIVERY, LAST_L1_R);
    send_metric("l2_instant_power_returndelivery", L2_INSTANT_POWER_RETURNDELIVERY, LAST_L2_R);
    send_metric("l3_instant_power_returndelivery", L3_INSTANT_POWER_RETURNDELIVERY, LAST_L3_R);
    send_metric("l1_instant_power_current", L1_INSTANT_POWER_CURRENT, LAST_L1_C);
    send_metric("l2_instant_power_current", L2_INSTANT_POWER_CURRENT, LAST_L2_C);
    send_metric("l3_instant_power_current", L3_INSTANT_POWER_CURRENT, LAST_L3_C);
    send_metric("l1_voltage", L1_VOLTAGE, LAST_L1_V);
    send_metric("l2_voltage", L2_VOLTAGE, LAST_L2_V);
    send_metric("l3_voltage", L3_VOLTAGE, LAST_L3_V);

    send_metric("gas_meter_m3", GAS_METER_M3, LAST_GAS);

    send_metric("actual_tarif_group", ACTUAL_TARIF, LAST_TARIF);
    send_metric("short_power_outages", SHORT_POWER_OUTAGES, LAST_S_OUT);
    send_metric("long_power_outages", LONG_POWER_OUTAGES, LAST_L_OUT);
    send_metric("short_power_drops", SHORT_POWER_DROPS, LAST_S_DROP);
    send_metric("short_power_peaks", SHORT_POWER_PEAKS, LAST_S_PEAK);

    send_metric("actual_average_15m_peak", mActualAverage15mPeak, LAST_AVG_15M);
    send_metric("thismonth_max_15m_peak", mMax15mPeakThisMonth, LAST_MAX_15M);
    send_metric("last13months_average_15m_peak", mAverage15mPeakLast13months, LAST_AVG_13MO);

    if (millis() - LAST_HEARTBEAT > 20000) {
        // Send Diagnostic Data
        char topic[128];
        char payload[32];
        
        snprintf(topic, sizeof(topic), "%s/wifi_rssi", MQTT_ROOT_TOPIC);
        ltoa(WiFi.RSSI(), payload, 10);
        mqtt_client.publish(topic, payload, false);

        snprintf(topic, sizeof(topic), "%s/ip_address", MQTT_ROOT_TOPIC);
        mqtt_client.publish(topic, WiFi.localIP().toString().c_str(), false);

        LAST_HEARTBEAT = millis();
    }
    
    // For the large JSON, we always send it if there is data, as change detection is complex for JSON
    if (!Last13MonthsPeaks_json.isNull()) {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/last13months_peaks_json", MQTT_ROOT_TOPIC);
        send_mqtt_message(topic, Last13MonthsPeaks_json); 
    }
    
    Last13MonthsPeaks_json.clear(); 
    set_milestone(3); // Milestone 3: Running (Back to normal loop)
}

// **********************************
// * P1                             *
// **********************************

unsigned int CRC16(unsigned int crc, unsigned char *buf, int len)
{
    for (int pos = 0; pos < len; pos++)
    {
        crc ^= (unsigned int)buf[pos]; // * XOR byte into least sig. byte of crc

        for (int i = 8; i != 0; i--) // * Loop over each bit
        {
            // * If the LSB is set
            if ((crc & 0x0001) != 0)
            {
                // * Shift right and XOR 0xA001
                crc >>= 1;
                crc ^= 0xA001;
                // crc ^= 0x8005;
            }
            // * Else LSB is not set
            else
                // * Just shift right
                crc >>= 1;
        }
    }
    return crc;
}

bool isNumber(const char *res, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (((res[i] < '0') || (res[i] > '9')) && (res[i] != '.' && res[i] != 0))
            return false;
    }
    return true;
}

int FindCharInArrayRev(const char array[], char c, int len)
{
    for (int i = len - 1; i >= 0; i--)
    {
        if (array[i] == c)
            return i;
    }
    return -1;
}

long getValue(const char *buffer, int maxlen, char startchar, char endchar) // should have more than 4 chars.
{
    int s = FindCharInArrayRev(buffer, startchar, maxlen - 2);
    if (s < 0) return 0;
    int l = FindCharInArrayRev(buffer, endchar, maxlen - 2) - s - 1;
    if (l <= 0 || l >= 16) return 0;

    char res[16];
    memset(res, 0, sizeof(res));

    if (strncpy(res, buffer + s + 1, l))
    {
        if (endchar == '*')
        {
            if (isNumber(res, l))
                // * Lazy convert float to long
                return (1000 * atof(res));
        }
        else if (endchar == ')')
        {
            if (isNumber(res, l))
                return atof(res);
        }
    }
    return 0;
}

bool decode_telegram(int len)
{
    int startChar = FindCharInArrayRev(telegram, '/', len);
    int endChar = FindCharInArrayRev(telegram, '!', len);
    bool validCRCFound = false;

    // debug
    // Serial.printf("\nstartchar = %d; endchar = %d; \n", startChar, endChar);

    Serial.print("telegram = __");
    for (int cnt = 0; cnt < len; cnt++)
    {
        Serial.print(telegram[cnt]);
    }
    // Serial.print("__\n");

    if (startChar >= 0)
    {
        // debug
        // Serial.println("Branch 1");

        // * Start found. Reset CRC calculation
        currentCRC = CRC16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
    }
    else if (endChar >= 0)
    {
        // debug
        Serial.println("Branch 2");

        // * Add to crc calc
        currentCRC = CRC16(currentCRC, (unsigned char *)telegram + endChar, 1);

        char messageCRC[5];
        strncpy(messageCRC, telegram + endChar + 1, 4);

        messageCRC[4] = 0; // * Thanks to HarmOtten (issue 5)

        // debug
        Serial.printf("\nmessageCRC = %s", messageCRC);

        validCRCFound = ((unsigned int)strtol(messageCRC, NULL, 16) == currentCRC);

        // debug
        Serial.printf("\ncurrentCRC = %d", currentCRC);

        if (validCRCFound)
            Serial.println(F("CRC Valid!"));
        else
            Serial.println(F("CRC Invalid!"));

        currentCRC = 0;
    }
    else
    {
        // debug
        // Serial.println("Branch 3");

        currentCRC = CRC16(currentCRC, (unsigned char *)telegram, len);
    }

    // debug
    // Serial.printf("\ncurrentCRC = %d", currentCRC);

    // 1-0:1.8.1(000992.992*kWh)
    // 1-0:1.8.1 = Elektra verbruik DAG  tarief (DSMR v4.0)
    if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0)
    {
        CONSUMPTION_HIGH_TARIF = getValue(telegram, len, '(', '*');
    }

    // 1-0:1.8.2(000560.157*kWh)
    // 1-0:1.8.2 = Elektra verbruik NACHT tarief (DSMR v4.0)
    if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0)
    {
        CONSUMPTION_LOW_TARIF = getValue(telegram, len, '(', '*');
    }

    // 1-0:2.8.1(000560.157*kWh)
    // 1-0:2.8.1 = Elektra opbrengst dagtarief (Fluvius) - Totale injectie van energie in kWh dagtarief
    if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0)
    {
        RETURNDELIVERY_HIGH_TARIF = getValue(telegram, len, '(', '*');
    }

    // 1-0:2.8.2(000560.157*kWh)
    // 1-0:2.8.2 = Elektra opbrengst nachttarief (Fluvius) - Totale injectie van energie in kWh nachttarief
    if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0)
    {
        RETURNDELIVERY_LOW_TARIF = getValue(telegram, len, '(', '*');
    }

    // 1-0:1.7.0(00.424*kW) Actueel verbruik (Fluvius) - Afgenomen ogenblikkelijk vermogen in kW
    // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
    if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0)
    {
        ACTUAL_CONSUMPTION = getValue(telegram, len, '(', '*');
    }

    // 1-0:2.7.0(00.000*kW) Actuele teruglevering (Fluvius) - Geïnjecteerd ogenblikkelijk vermogen in kW
    if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
    {
        ACTUAL_RETURNDELIVERY = getValue(telegram, len, '(', '*');
    }

    // 1-0:21.7.0(00.378*kW)
    // 1-0:21.7.0 = Instantaan vermogen Elektriciteit levering L1
    if (strncmp(telegram, "1-0:21.7.0", strlen("1-0:21.7.0")) == 0)
    {
        L1_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*');
    }

    // 1-0:41.7.0(00.378*kW)
    // 1-0:41.7.0 = Instantaan vermogen Elektriciteit levering L2
    if (strncmp(telegram, "1-0:41.7.0", strlen("1-0:41.7.0")) == 0)
    {
        L2_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*');
    }

    // 1-0:61.7.0(00.378*kW)
    // 1-0:61.7.0 = Instantaan vermogen Elektriciteit levering L3
    if (strncmp(telegram, "1-0:61.7.0", strlen("1-0:61.7.0")) == 0)
    {
        L3_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*');
    }

    // 1-0:22.7.0(00.378*kW)
    // 1-0:22.7.0 = Instantaan vermogen Elektriciteit teruglevering L1
    if (strncmp(telegram, "1-0:22.7.0", strlen("1-0:22.7.0")) == 0)
    {
        L1_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*');
    }

    // 1-0:42.7.0(00.378*kW)
    // 1-0:42.7.0 = Instantaan vermogen Elektriciteit teruglevering L2
    if (strncmp(telegram, "1-0:42.7.0", strlen("1-0:42.7.0")) == 0)
    {
        L2_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*');
    }

    // 1-0:62.7.0(00.378*kW)
    // 1-0:62.7.0 = Instantaan vermogen Elektriciteit teruglevering L3
    if (strncmp(telegram, "1-0:62.7.0", strlen("1-0:62.7.0")) == 0)
    {
        L3_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*');
    }
    // 1-0:31.7.0(002*A)
    // 1-0:31.7.0 = Instantant stroom Elektriciteit L1
    if (strncmp(telegram, "1-0:31.7.0", strlen("1-0:31.7.0")) == 0)
    {
        L1_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*');
    }
    // 1-0:51.7.0(002*A)
    // 1-0:51.7.0 = Instantant stroom Elektriciteit L2
    if (strncmp(telegram, "1-0:51.7.0", strlen("1-0:51.7.0")) == 0)
    {
        L2_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*');
    }
    // 1-0:71.7.0(002*A)
    // 1-0:71.7.0 = Instantant stroom Elektriciteit L3
    if (strncmp(telegram, "1-0:71.7.0", strlen("1-0:71.7.0")) == 0)
    {
        L3_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*');
    }

    // 1-0:32.7.0(232.0*V)
    // 1-0:32.7.0 = Voltage L1
    if (strncmp(telegram, "1-0:32.7.0", strlen("1-0:32.7.0")) == 0)
    {
        L1_VOLTAGE = getValue(telegram, len, '(', '*');
    }
    // 1-0:52.7.0(232.0*V)
    // 1-0:52.7.0 = Voltage L2
    if (strncmp(telegram, "1-0:52.7.0", strlen("1-0:52.7.0")) == 0)
    {
        L2_VOLTAGE = getValue(telegram, len, '(', '*');
    }
    // 1-0:72.7.0(232.0*V)
    // 1-0:72.7.0 = Voltage L3
    if (strncmp(telegram, "1-0:72.7.0", strlen("1-0:72.7.0")) == 0)
    {
        L3_VOLTAGE = getValue(telegram, len, '(', '*');
    }

    // 0-1:24.2.3(150531200000S)(00811.923*m3)
    // 0-1:24.2.3 = Gas (DSMR v5.0 - fluvius)
    if (strncmp(telegram, "0-1:24.2.3", strlen("0-1:24.2.3")) == 0)
    {
        GAS_METER_M3 = getValue(telegram, len, '(', '*');
    }

    // 0-0:96.14.0(0001)
    // 0-0:96.14.0 = Actual Tarif
    if (strncmp(telegram, "0-0:96.14.0", strlen("0-0:96.14.0")) == 0)
    {
        ACTUAL_TARIF = getValue(telegram, len, '(', ')');
    }

    // 0-0:96.7.21(00003)
    // 0-0:96.7.21 = Aantal onderbrekingen Elektriciteit
    if (strncmp(telegram, "0-0:96.7.21", strlen("0-0:96.7.21")) == 0)
    {
        SHORT_POWER_OUTAGES = getValue(telegram, len, '(', ')');
    }

    // 0-0:96.7.9(00001)
    // 0-0:96.7.9 = Aantal lange onderbrekingen Elektriciteit
    if (strncmp(telegram, "0-0:96.7.9", strlen("0-0:96.7.9")) == 0)
    {
        LONG_POWER_OUTAGES = getValue(telegram, len, '(', ')');
    }

    // 1-0:32.32.0(00000)
    // 1-0:32.32.0 = Aantal korte spanningsdalingen Elektriciteit in fase 1
    if (strncmp(telegram, "1-0:32.32.0", strlen("1-0:32.32.0")) == 0)
    {
        SHORT_POWER_DROPS = getValue(telegram, len, '(', ')');
    }

    // 1-0:32.36.0(00000)
    // 1-0:32.36.0 = Aantal korte spanningsstijgingen Elektriciteit in fase 1
    if (strncmp(telegram, "1-0:32.36.0", strlen("1-0:32.36.0")) == 0)
    {
        SHORT_POWER_PEAKS = getValue(telegram, len, '(', ')');
    }

#pragma region UPDATE 1.7.1 PEAK TARRIFF

    // 1-0:1.4.0(02.351*kW)
    // 1-0:1.4.0 = quart_hourly_current_average_peak_consumption kW - Current rolling avg of the last 15 minutes
    if (strncmp(telegram, "1-0:1.4.0", strlen("1-0:1.4.0")) == 0)
        mActualAverage15mPeak = getValue(telegram, len, '(', '*');

    // 1-0:1.6.0(200509134558S)(02.589*kW)
    // 1-0:1.6.0 = quart_hourly_max_peak_this_month kW
    if (strncmp(telegram, "1-0:1.6.0", strlen("1-0:1.6.0")) == 0)
        mMax15mPeakThisMonth = getValue(telegram, len, '(', '*');

    if (strncmp(telegram, "0-0:98.1.0", strlen("0-0:98.1.0")) == 0)
    {
        Last13MonthsPeaks_json.clear(); // Ensure document is empty before populating

        char* ptr = strchr(telegram, '(');
        if (ptr)
        {
            unsigned long count = strtol(ptr + 1, NULL, 10);
            
            Last13MonthsPeaks_json["count"] = count;
            Last13MonthsPeaks_json["unit"] = "W";
            JsonArray peakvalues = Last13MonthsPeaks_json["values"].to<JsonArray>();

            // Skip the next two blocks, e.g., (1-0:1.6.0)(1-0:1.6.0)
            for (int i = 0; i < 2 && ptr; i++) {
                ptr = strchr(ptr + 1, '(');
            }

            long sum = 0;
            int valid_values = 0;

            // Now read groups of 3 blocks: (timestamp1)(timestamp2)(value*kW)
            while (ptr) {
                ptr = strchr(ptr + 1, '('); // block 1 (timestamp1)
                if (!ptr) break;
                ptr = strchr(ptr + 1, '('); // block 2 (timestamp2)
                if (!ptr) break;
                ptr = strchr(ptr + 1, '('); // block 3 (value*kW)
                if (!ptr) break;

                // ptr points to '('. The value is between '(' and '*'
                float val_kW = atof(ptr + 1);
                long val_W = (long)(val_kW * 1000.0);
                
                peakvalues.add(val_W);
                sum += val_W;
                valid_values++;
                
                // Advance ptr to the end of this block ')'
                ptr = strchr(ptr, ')');
            }

            if (valid_values > 0 && count == (unsigned long)valid_values) {
                mAverage15mPeakLast13months = sum / valid_values;
            } else {
                mAverage15mPeakLast13months = -1; // detect error value over MQTT.
            }
        }
    }

#pragma endregion

    return validCRCFound;
}

void read_p1_hardwareserial()
{
    if (Serial.available())
    {
        set_milestone(4); // Milestone 4: Reading P1
        memset(telegram, 0, sizeof(telegram));

        while (Serial.available())
        {
            // Read until newline, but leave room for the terminator in our buffer
            int len = Serial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH - 2);

            if (len > 0) {
                processLine(len);
            }

            memset(telegram, 0, sizeof(telegram));
        }
    }
}

void processLine(int len)
{
    // Bounds check to ensure we don't overflow the buffer
    if (len >= P1_MAXLINELENGTH - 2) {
        len = P1_MAXLINELENGTH - 3; 
    }
    
    telegram[len] = '\n';
    telegram[len + 1] = 0;
    yield();

    // DEBUG
    bool result = decode_telegram(len + 1);

    if (result)
    {
        if (millis() - LAST_UPDATE_SENT > UPDATE_INTERVAL)
        {
            send_data_to_broker();
            LAST_UPDATE_SENT = millis();
        }
    }
}

// **********************************
// * EEPROM helpers                 *
// **********************************

void read_eeprom(int offset, int len, char* buffer)
{
    Serial.print(F("read_eeprom()"));

    for (int i = 0; i < len; ++i)
    {
        buffer[i] = char(EEPROM.read(i + offset));
    }
    buffer[len] = '\0'; // Null-terminate the string
}

void write_eeprom(int offset, int len, const char* value)
{
    Serial.println(F("write_eeprom()"));
    size_t val_len = strlen(value);
    
    for (int i = 0; i < len; ++i)
    {
        char charToWrite = ((unsigned)i < val_len) ? value[i] : 0;
        if (EEPROM.read(i + offset) != charToWrite)
        {
            EEPROM.write(i + offset, charToWrite);
        }
    }
}

// ******************************************
// * Callback for saving WIFI config        *
// ******************************************

bool shouldSaveConfig = false;

// * Callback notifying us of the need to save config
void save_wifi_config_callback()
{
    Serial.println(F("Should save config"));
    shouldSaveConfig = true;
}

// ******************************************
// * Callback for resetting Wifi settings   *
// ******************************************
void resetWifi()
{
    Serial.println("RST was pushed twice...");
    Serial.println("Erasing stored WiFi credentials.");

    // clear WiFi creds.
    WiFiManager wifiManager;
    wifiManager.resetSettings();

    Serial.println("Restarting...");
    ESP.restart(); // builtin, safely restarts the ESP.
}

// **********************************
// * Setup OTA                      *
// **********************************

void setup_ota()
{
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()
    {
        Serial.println(F("Arduino OTA: Start"));
    });

    ArduinoOTA.onEnd([]()
    {
        Serial.println(F("Arduino OTA: End (Running reboot)"));
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed"));
    });

    ArduinoOTA.begin();
    Serial.println(F("Arduino OTA finished"));
}

// **********************************
// * Setup MDNS discovery service   *
// **********************************

void setup_mdns()
{
    Serial.println(F("Starting MDNS responder service"));

    bool mdns_result = MDNS.begin(HOSTNAME);
    if (mdns_result)
    {
        MDNS.addService("http", "tcp", 80);
    }
}

// **********************************
// * Setup Main                     *
// **********************************

void setup()
{
    // * Configure EEPROM
    EEPROM.begin(512);

    // Setup a hw serial connection for communication with the P1 meter and logging (not using inversion)
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_FULL);
    Serial.setTimeout(50); // Shorter timeout for faster line reading
    Serial.println("");

    // Capture the reset reason and milestone
    ESP.rtcUserMemoryRead(RTC_BASE_ADDR, (uint32_t*)&rtc_data, sizeof(rtc_data));
    const char* milestone_name = "Unknown";
    if (rtc_data.marker == RTC_MARKER) {
        switch(rtc_data.milestone) {
            case 1: milestone_name = "Booting"; break;
            case 2: milestone_name = "WiFi Connecting"; break;
            case 3: milestone_name = "Running"; break;
            case 4: milestone_name = "Reading P1"; break;
            case 5: milestone_name = "Sending MQTT"; break;
        }
    }
    
    snprintf(last_reset_info, sizeof(last_reset_info), "Reason: %s | Info: %s | Milestone: %s", ESP.getResetReason().c_str(), ESP.getResetInfo().c_str(), milestone_name);
    Serial.println(last_reset_info);

    // Reset milestone for the current session
    set_milestone(1); // Milestone 1: Booting

    Serial.println("Swapping UART0 RX to inverted");
    Serial.flush();

    // Invert the RX serialport by setting a register value, this way the TX might continue normally allowing the serial monitor to read println's
    USC0(UART0) = USC0(UART0) | BIT(UCRXI);
    Serial.println("Serial port is ready to recieve.");

    // * Set led pin as output
    pinMode(LED_BUILTIN, OUTPUT);

    // * Disable persistent WiFi settings to save flash wear
    WiFi.persistent(false);

    // * Setup Double reset detection
    if (drd.detectDoubleReset())
    {
        Serial.println("DRD: Double Reset Detected");
        Serial.println("DRD: RESET WIFI Initiated");
        resetWifi();
    }
    else
    {
        Serial.println("DRD: No Double Reset Detected");
    }

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, tick);

    // * Get MQTT Server settings
    char settings_available[2] = "";
    read_eeprom(134, 1, settings_available);
    char ha_val_str[2] = "1";

    if (settings_available[0] == '1')
    {
        read_eeprom(0, 64, MQTT_HOST);   // * 0-63
        read_eeprom(64, 6, MQTT_PORT);    // * 64-69
        read_eeprom(70, 32, MQTT_USER);  // * 70-101
        read_eeprom(102, 32, MQTT_PASS); // * 102-133
        read_eeprom(135, 1, ha_val_str);  // * 135
        HA_AUTO_DISCOVERY = (ha_val_str[0] == '1');
    }

    WiFiManagerParameter CUSTOM_MQTT_HOST("host", "MQTT hostname", MQTT_HOST, 64);
    WiFiManagerParameter CUSTOM_MQTT_PORT("port", "MQTT port", MQTT_PORT, 6);
    WiFiManagerParameter CUSTOM_MQTT_USER("user", "MQTT user", MQTT_USER, 32);
    WiFiManagerParameter CUSTOM_MQTT_PASS("pass", "MQTT pass", MQTT_PASS, 32);

    char customhtml[200];
    snprintf(customhtml, sizeof(customhtml), "type='hidden' id='ha_val_hidden'><label style='color:#0f0;cursor:pointer;display:block;margin:10px 0;'><input type='checkbox' %s onchange=\"document.getElementById('ha_val_hidden').value=this.checked?'1':'0';\"> Enable HA Discovery</label>", HA_AUTO_DISCOVERY ? "checked" : "");
    WiFiManagerParameter CUSTOM_HA_DISCOVERY("ha_val", "", ha_val_str, 2, customhtml);

    // * WiFiManager local initialization
    WiFiManager wifiManager;

    // Hacker Style 2026 UI (Clean & Stable)
    const char* custom_css = "<style>"
                             "body{background:#0a0a0a;color:#0f0;font-family:monospace;}"
                             ".wrap{max-width:450px;margin:20px auto;border:1px solid #0f0;padding:20px;box-shadow:0 0 10px #0f0;}"
                             "input[type='text'],input[type='password']{background:#111;color:#0f0;border:1px solid #0f0;padding:10px;width:100%;box-sizing:border-box;margin-bottom:10px;}"
                             "input[type='submit'],button{background:#0f0;color:#000;border:none;padding:15px;width:100%;font-weight:bold;cursor:pointer;margin-top:10px;}"
                             "div,label,a{color:#0f0 !important;}"
                             "h1{text-align:center;text-shadow:0 0 5px #0f0;}"
                             "</style>";
    wifiManager.setCustomHeadElement(custom_css);

    // * Reset settings - uncomment for testing
    // wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Add all your parameters here
    wifiManager.addParameter(&CUSTOM_MQTT_HOST);
    wifiManager.addParameter(&CUSTOM_MQTT_PORT);
    wifiManager.addParameter(&CUSTOM_MQTT_USER);
    wifiManager.addParameter(&CUSTOM_MQTT_PASS);
    wifiManager.addParameter(&CUSTOM_HA_DISCOVERY);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    set_milestone(2); // Milestone 2: WiFi Connecting
    if (!wifiManager.autoConnect())
    {
        Serial.println(F("Failed to connect to WIFI and hit timeout. Restarting..."));

        // * Reset and try again, or maybe put it to deep sleep
        ESP.restart();
        delay(5000);
    }

    // * Read updated parameters
    strcpy(MQTT_HOST, CUSTOM_MQTT_HOST.getValue());
    strcpy(MQTT_PORT, CUSTOM_MQTT_PORT.getValue());
    strcpy(MQTT_USER, CUSTOM_MQTT_USER.getValue());
    strcpy(MQTT_PASS, CUSTOM_MQTT_PASS.getValue());
    
    // Process HA Auto-Discovery parameter safely
    if (CUSTOM_HA_DISCOVERY.getValue()[0] == '1' || CUSTOM_HA_DISCOVERY.getValue()[0] == '0') {
        HA_AUTO_DISCOVERY = (CUSTOM_HA_DISCOVERY.getValue()[0] == '1');
    }

    // * Save the custom parameters to EEPROM
    if (shouldSaveConfig)
    {
        Serial.println(F("Saving WiFiManager config"));

        write_eeprom(0, 64, MQTT_HOST);   // * 0-63
        write_eeprom(64, 6, MQTT_PORT);   // * 64-69
        write_eeprom(70, 32, MQTT_USER);  // * 70-101
        write_eeprom(102, 32, MQTT_PASS); // * 102-133
        write_eeprom(134, 1, "1");        // * 134 --> always "1"
        write_eeprom(135, 1, HA_AUTO_DISCOVERY ? "1" : "0"); // * 135
        EEPROM.commit();
    }

    // * If you get here you have connected to the WiFi
    Serial.println(F("Connected to WIFI..."));
    set_milestone(3); // Milestone 3: Running

    // * Keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    // * Configure OTA
    setup_ota();

    // * Startup MDNS Service
    setup_mdns();

    // * Setup MQTT
    Serial.printf("MQTT connecting to: %s:%s\n", MQTT_HOST, MQTT_PORT);

    // Increase MQTT buffer size for peak JSON data
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
}

// **********************************
// * Loop                           *
// **********************************

void loop()
{
    ArduinoOTA.handle();
    unsigned long now = millis();

    // Check WiFi Status
    if (WiFi.status() != WL_CONNECTED)
    {
        if (now - lastReconnectAttempt > 30000)
        {
            Serial.println(F("WiFi disconnected! Reconnecting..."));
            WiFi.reconnect();
            lastReconnectAttempt = now;
        }
        return; // Don't try MQTT if WiFi is down
    }

    // MQTT Reconnection logic (non-blocking)
    if (!mqtt_client.connected())
    {
        if (now - lastReconnectAttempt > 5000)
        {
            lastReconnectAttempt = now;
            if (mqtt_reconnect())
            {
                lastReconnectAttempt = 0;
            }
        }
    }
    else
    {
        mqtt_client.loop();
    }

    // Read P1 Data
    // We read continuously to avoid serial buffer overflow
    if (Serial.available())
    {
        read_p1_hardwareserial();
    }

    // Periodic Heap Logging
    static unsigned long lastHeapLog = 0;
    if (now - lastHeapLog > 60000)
    {
        lastHeapLog = now;
        Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    }

    // Call the double reset detector loop
    drd.loop();
}
