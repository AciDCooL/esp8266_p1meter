/* 
 * ESP8266 P1 Meter - v1.4.9
 * Re-engineered for maximum stability, zero heap fragmentation, 
 * and native Home Assistant Auto-Discovery.
 */
#define VERSION "1.4.9"

// * Libraries
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <ESP8266WebServer.h>
#include <ElegantOTA.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <DoubleResetDetector.h>
#include <ArduinoJson.h>

// * Function Prototypes
void tick();
void set_milestone(uint32_t m);
void publish_ha_discovery();
void mark_seen(const char* id);
bool mqtt_reconnect();
void send_mqtt_json(const char *topic, JsonDocument& doc);
void send_metric(const char* name, long metric, long& last_value, int divisor = 1);
void send_data_to_broker();
bool decode_telegram(int len);
void processLine(int len);
void read_p1_hardwareserial();
void read_eeprom(int offset, int len, char* buffer);
void write_eeprom(int offset, int len, const char* value);
void save_wifi_config_callback();
void resetWifi();
void setup_mdns();

// * Include settings
#include "settings.h"

// * Initiate components
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);
Ticker ticker;
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
ESP8266WebServer server(80);

// * Reset and Milestone tracking
struct {
    uint32_t marker; 
    uint32_t milestone;
    long last_con_high;
    long last_con_low;
    long last_ret_high;
    long last_ret_low;
    long last_gas;
} rtc_persistent;

#define RTC_MARKER 0xDEADBEEF
#define RTC_BASE_ADDR 64

bool system_verified = false;
int valid_telegram_count = 0;
#define STABILIZATION_THRESHOLD 3
#define MAX_ENERGY_DELTA 10000 // Max 10kWh jump in 1s

void set_milestone(uint32_t m) {
    rtc_persistent.milestone = m;
    rtc_persistent.marker = RTC_MARKER;
    ESP.rtcUserMemoryWrite(RTC_BASE_ADDR, (uint32_t*)&rtc_persistent, sizeof(rtc_persistent));
}

void update_rtc_totals() {
    if (CONSUMPTION_HIGH_TARIF > 0) rtc_persistent.last_con_high = CONSUMPTION_HIGH_TARIF;
    if (CONSUMPTION_LOW_TARIF > 0) rtc_persistent.last_con_low = CONSUMPTION_LOW_TARIF;
    if (RETURNDELIVERY_HIGH_TARIF > 0) rtc_persistent.last_ret_high = RETURNDELIVERY_HIGH_TARIF;
    if (RETURNDELIVERY_LOW_TARIF > 0) rtc_persistent.last_ret_low = RETURNDELIVERY_LOW_TARIF;
    if (GAS_METER_M3 > 0) rtc_persistent.last_gas = GAS_METER_M3;
    rtc_persistent.marker = RTC_MARKER;
    ESP.rtcUserMemoryWrite(RTC_BASE_ADDR, (uint32_t*)&rtc_persistent, sizeof(rtc_persistent));
}

bool is_data_sane(long current, long last, long max_delta, const char* label) {
    // If metric is 0 (hasn't arrived yet, or no return power), bypass check to prevent deadlock
    if (current <= 0) return true;
    
    // If RTC has no history for this metric, accept to seed it
    if (last <= 0 || rtc_persistent.marker != RTC_MARKER) return true; 
    
    long delta = current - last;
    if (delta >= 0 && delta < max_delta) return true;
    
    Serial.printf("SANITY FAIL [%s]: Current=%ld, Last=%ld, Delta=%ld (Max=%ld)\n", label, current, last, delta, max_delta);
    return false;
}

void tick() { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); }

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
    {"frequency", "Line Frequency", "Hz", "frequency", "measurement", ""},
    {"gas_meter_m3", "Gas Meter", "m³", "gas", "total_increasing", ""},
    {"actual_average_15m_peak", "Peak 15m Average", "W", "power", "measurement", ""},
    {"thismonth_max_15m_peak", "Peak Max This Month", "W", "power", "measurement", ""},
    {"last13months_average_15m_peak", "Peak 13 Months Avg", "W", "power", "measurement", ""},
    {"wifi_rssi", "WiFi Signal", "dBm", "signal_strength", "measurement", "mdi:wifi"},
    {"ip_address", "IP Address", "", "", "", "mdi:network"}
};

#define SENSOR_COUNT (sizeof(sensors) / sizeof(HASensor))
bool seen_metrics[SENSOR_COUNT] = {false};
bool discovery_published = false;
unsigned long boot_time = 0;
char last_reset_info[128] = "";

void mark_seen(const char* id) {
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        if (strcmp(sensors[i].id, id) == 0) { seen_metrics[i] = true; return; }
    }
}

void publish_ha_discovery() {
    if (!HA_AUTO_DISCOVERY || !mqtt_client.connected()) return;
    char topic[128], payload[600], status_topic[128];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
    char dev_name[64], dev_id[32];
    snprintf(dev_name, sizeof(dev_name), "%s-%06X", HA_DEVICE_NAME, ESP.getChipId());
    snprintf(dev_id, sizeof(dev_id), "p1meter_%06X", ESP.getChipId());

    JsonDocument doc;
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        if (!seen_metrics[i] && strcmp(sensors[i].id, "wifi_rssi") != 0 && strcmp(sensors[i].id, "ip_address") != 0) continue;
        doc.clear();
        const HASensor& s = sensors[i];
        snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", HA_DISCOVERY_PREFIX, dev_id, s.id);
        doc["name"] = s.name;
        char uid[64]; snprintf(uid, sizeof(uid), "%s_%s", dev_id, s.id);
        doc["unique_id"] = uid;
        char st_topic[128]; snprintf(st_topic, sizeof(st_topic), "%s/%s", MQTT_ROOT_TOPIC, s.id);
        doc["state_topic"] = st_topic;
        doc["availability_topic"] = status_topic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        if (strlen(s.unit) > 0) doc["unit_of_measurement"] = s.unit;
        if (strlen(s.device_class) > 0) doc["device_class"] = s.device_class;
        if (strlen(s.state_class) > 0) doc["state_class"] = s.state_class;
        if (strlen(s.icon) > 0) doc["icon"] = s.icon;
        if (strcmp(s.id, "wifi_rssi") == 0 || strcmp(s.id, "ip_address") == 0) doc["entity_category"] = "diagnostic";
        JsonObject dev = doc["device"].to<JsonObject>();
        dev["identifiers"][0] = dev_id;
        dev["name"] = dev_name;
        dev["manufacturer"] = HA_MANUFACTURER;
        dev["model"] = HA_MODEL;
        dev["sw_version"] = VERSION;
        serializeJson(doc, payload, sizeof(payload));
        mqtt_client.publish(topic, payload, true);
        mqtt_client.loop(); yield(); delay(20); 
    }
    discovery_published = true;
}

bool mqtt_reconnect() {
    char status_topic[128], client_id[64];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
    snprintf(client_id, sizeof(client_id), "%s-%06X", HOSTNAME, ESP.getChipId());
    if (mqtt_client.connect(client_id, MQTT_USER, MQTT_PASS, status_topic, 1, true, "offline")) {
        mqtt_client.publish(status_topic, "stabilizing", true);
        if (strlen(last_reset_info) > 0) {
            char r_topic[128], r_payload[200];
            snprintf(r_topic, sizeof(r_topic), "%s/last_reset", MQTT_ROOT_TOPIC);
            snprintf(r_payload, sizeof(r_payload), "Version: %s | %s", VERSION, last_reset_info);
            mqtt_client.publish(r_topic, r_payload, true);
        }
        if (system_verified) mqtt_client.publish(status_topic, "online", true);
        if (discovery_published) publish_ha_discovery();
        return true;
    }
    return false;
}

void send_mqtt_json(const char *topic, JsonDocument& doc) {
    static char buffer[MQTT_BUFFER_SIZE];
    size_t n = serializeJson(doc, buffer, sizeof(buffer));
    mqtt_client.publish(topic, (uint8_t*)buffer, n);
    doc.clear();
}

long LAST_CON_LOW = -1, LAST_CON_HIGH = -1, LAST_RET_LOW = -1, LAST_RET_HIGH = -1;
long LAST_ACT_CON = -1, LAST_ACT_RET = -1, LAST_GAS = -1;
long LAST_L1_P = -1, LAST_L2_P = -1, LAST_L3_P = -1, LAST_L1_C = -1, LAST_L2_C = -1, LAST_L3_C = -1, LAST_L1_V = -1, LAST_L2_V = -1, LAST_L3_V = -1;
long LAST_L1_R = -1, LAST_L2_R = -1, LAST_L3_R = -1;
long LAST_TARIF = -1, LAST_S_OUT = -1, LAST_L_OUT = -1, LAST_S_DROP = -1, LAST_S_PEAK = -1;
long LAST_AVG_15M = -1, LAST_MAX_15M = -1, LAST_AVG_13MO = -1, LAST_FREQ = -1;
unsigned long LAST_HEARTBEAT = 0;

void send_metric(const char* name, long metric, long& last_value, int divisor) {
    bool seen = false;
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        if (strcmp(sensors[i].id, name) == 0) { if (!seen_metrics[i]) return; seen = true; break; }
    }
    if (!seen) return;
    if (metric != last_value || (millis() - LAST_HEARTBEAT > 20000)) {
        char topic[128], payload[32];
        snprintf(topic, sizeof(topic), "%s/%s", MQTT_ROOT_TOPIC, name);
        if (divisor == 1) ltoa(metric, payload, 10);
        else dtostrf(metric / (float)divisor, 1, 3, payload);
        if (mqtt_client.publish(topic, payload, false)) last_value = metric;
    }
}

void send_data_to_broker() {
    set_milestone(5); 
    bool sane = true;
    if (rtc_persistent.marker == RTC_MARKER) {
        if (!is_data_sane(CONSUMPTION_HIGH_TARIF, rtc_persistent.last_con_high, MAX_ENERGY_DELTA, "CON_HIGH")) sane = false;
        if (!is_data_sane(CONSUMPTION_LOW_TARIF, rtc_persistent.last_con_low, MAX_ENERGY_DELTA, "CON_LOW")) sane = false;
        if (!is_data_sane(RETURNDELIVERY_HIGH_TARIF, rtc_persistent.last_ret_high, MAX_ENERGY_DELTA, "RET_HIGH")) sane = false;
        if (!is_data_sane(RETURNDELIVERY_LOW_TARIF, rtc_persistent.last_ret_low, MAX_ENERGY_DELTA, "RET_LOW")) sane = false;
        if (!is_data_sane(GAS_METER_M3, rtc_persistent.last_gas, MAX_ENERGY_DELTA, "GAS")) sane = false;
    }
    if (!sane) return;
    if (!system_verified) {
        valid_telegram_count++;
        if (valid_telegram_count >= STABILIZATION_THRESHOLD) {
            system_verified = true;
            char status_topic[128]; snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
            mqtt_client.publish(status_topic, "online", true);
            Serial.println(F("SYSTEM VERIFIED: Online."));
        } else return;
    }
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
    send_metric("l1_instant_power_current", L1_INSTANT_POWER_CURRENT, LAST_L1_C, 1000);
    send_metric("l2_instant_power_current", L2_INSTANT_POWER_CURRENT, LAST_L2_C, 1000);
    send_metric("l3_instant_power_current", L3_INSTANT_POWER_CURRENT, LAST_L3_C, 1000);
    send_metric("l1_voltage", L1_VOLTAGE, LAST_L1_V, 1000);
    send_metric("l2_voltage", L2_VOLTAGE, LAST_L2_V, 1000);
    send_metric("l3_voltage", L3_VOLTAGE, LAST_L3_V, 1000);
    send_metric("frequency", FREQUENCY, LAST_FREQ, 1000);
    send_metric("gas_meter_m3", GAS_METER_M3, LAST_GAS, 1000);
    send_metric("actual_tarif_group", ACTUAL_TARIF, LAST_TARIF);
    send_metric("short_power_outages", SHORT_POWER_OUTAGES, LAST_S_OUT);
    send_metric("long_power_outages", LONG_POWER_OUTAGES, LAST_L_OUT);
    send_metric("short_power_drops", SHORT_POWER_DROPS, LAST_S_DROP);
    send_metric("short_power_peaks", SHORT_POWER_PEAKS, LAST_S_PEAK);
    send_metric("actual_average_15m_peak", mActualAverage15mPeak, LAST_AVG_15M);
    send_metric("thismonth_max_15m_peak", mMax15mPeakThisMonth, LAST_MAX_15M);
    send_metric("last13months_average_15m_peak", mAverage15mPeakLast13months, LAST_AVG_13MO);
    if (millis() - LAST_HEARTBEAT > 20000) {
        char t[128], p[32]; snprintf(t, sizeof(t), "%s/wifi_rssi", MQTT_ROOT_TOPIC);
        ltoa(WiFi.RSSI(), p, 10); mqtt_client.publish(t, p, false);
        snprintf(t, sizeof(t), "%s/ip_address", MQTT_ROOT_TOPIC);
        mqtt_client.publish(t, WiFi.localIP().toString().c_str(), false);
        LAST_HEARTBEAT = millis();
    }
    if (!Last13MonthsPeaks_json.isNull()) {
        char t[128]; snprintf(t, sizeof(t), "%s/last13months_peaks_json", MQTT_ROOT_TOPIC);
        send_mqtt_json(t, Last13MonthsPeaks_json); 
    }
    update_rtc_totals(); set_milestone(3);
}

unsigned int CRC16(unsigned int crc, unsigned char *buf, int len) {
    for (int pos = 0; pos < len; pos++) {
        crc ^= (unsigned int)buf[pos];
        for (int i = 8; i != 0; i--) { if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; } else crc >>= 1; }
    }
    return crc;
}

bool isNumber(const char *res, int len) {
    for (int i = 0; i < len; i++) { if (((res[i] < '0') || (res[i] > '9')) && (res[i] != '.' && res[i] != ',' && res[i] != 0)) return false; }
    return true;
}

int FindCharInArrayRev(const char array[], char c, int len) {
    for (int i = len - 1; i >= 0; i--) { if (array[i] == c) return i; }
    return -1;
}

long getValue(const char *buffer, int maxlen, char startchar, char endchar) {
    int e = FindCharInArrayRev(buffer, endchar, maxlen); if (e < 0) return 0;
    int s = FindCharInArrayRev(buffer, startchar, e); if (s < 0) return 0;
    int l = e - s - 1; if (l <= 0 || l >= 16) return 0;
    char res[16]; memset(res, 0, sizeof(res));
    if (strncpy(res, buffer + s + 1, l)) {
        for(int i=0; i<l; i++) if(res[i] == ',') res[i] = '.';
        if (isNumber(res, l)) { if (endchar == '*') return (1000 * atof(res)); return atof(res); }
    }
    return 0;
}

bool decode_telegram(int len) {
    int startChar = FindCharInArrayRev(telegram, '/', len);
    int endChar = FindCharInArrayRev(telegram, '!', len);
    bool validCRCFound = false;
    if (startChar >= 0) currentCRC = CRC16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
    else if (endChar >= 0) {
        currentCRC = CRC16(currentCRC, (unsigned char *)telegram + endChar, 1);
        char messageCRC[5]; strncpy(messageCRC, telegram + endChar + 1, 4); messageCRC[4] = 0;
        validCRCFound = ((unsigned int)strtol(messageCRC, NULL, 16) == currentCRC);
        if (!validCRCFound) {
            char debug_topic[128]; snprintf(debug_topic, sizeof(debug_topic), "%s/debug_crc", MQTT_ROOT_TOPIC);
            char debug_msg[128]; snprintf(debug_msg, sizeof(debug_msg), "CRC FAIL: Calc=%04X, Meter=%s", currentCRC, messageCRC);
            mqtt_client.publish(debug_topic, debug_msg, false);
            Serial.printf("CRC FAIL: Calc=%04X, Meter=%s\n", currentCRC, messageCRC);
        }
        currentCRC = 0;
    } else currentCRC = CRC16(currentCRC, (unsigned char *)telegram, len);

    if (strncmp(telegram, "1-0:1.8.1", 9) == 0) { CONSUMPTION_HIGH_TARIF = getValue(telegram, len, '(', '*'); mark_seen("consumption_high_tarif"); }
    if (strncmp(telegram, "1-0:1.8.2", 9) == 0) { CONSUMPTION_LOW_TARIF = getValue(telegram, len, '(', '*'); mark_seen("consumption_low_tarif"); }
    if (strncmp(telegram, "1-0:2.8.1", 9) == 0) { RETURNDELIVERY_HIGH_TARIF = getValue(telegram, len, '(', '*'); mark_seen("returndelivery_high_tarif"); }
    if (strncmp(telegram, "1-0:2.8.2", 9) == 0) { RETURNDELIVERY_LOW_TARIF = getValue(telegram, len, '(', '*'); mark_seen("returndelivery_low_tarif"); }
    if (strncmp(telegram, "1-0:1.7.0", 9) == 0) { ACTUAL_CONSUMPTION = getValue(telegram, len, '(', '*'); mark_seen("actual_consumption"); }
    if (strncmp(telegram, "1-0:2.7.0", 9) == 0) { ACTUAL_RETURNDELIVERY = getValue(telegram, len, '(', '*'); mark_seen("actual_returndelivery"); }
    if (strncmp(telegram, "1-0:21.7.0", 10) == 0) { L1_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*'); mark_seen("l1_instant_power_usage"); }
    if (strncmp(telegram, "1-0:41.7.0", 10) == 0) { L2_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*'); mark_seen("l2_instant_power_usage"); }
    if (strncmp(telegram, "1-0:61.7.0", 10) == 0) { L3_INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*'); mark_seen("l3_instant_power_usage"); }
    if (strncmp(telegram, "1-0:22.7.0", 10) == 0) { L1_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*'); mark_seen("l1_instant_power_returndelivery"); }
    if (strncmp(telegram, "1-0:42.7.0", 10) == 0) { L2_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*'); mark_seen("l2_instant_power_returndelivery"); }
    if (strncmp(telegram, "1-0:62.7.0", 10) == 0) { L3_INSTANT_POWER_RETURNDELIVERY = getValue(telegram, len, '(', '*'); mark_seen("l3_instant_power_returndelivery"); }
    if (strncmp(telegram, "1-0:31.7.0", 10) == 0) { L1_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*'); mark_seen("l1_instant_power_current"); }
    if (strncmp(telegram, "1-0:51.7.0", 10) == 0) { L2_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*'); mark_seen("l2_instant_power_current"); }
    if (strncmp(telegram, "1-0:71.7.0", 10) == 0) { L3_INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*'); mark_seen("l3_instant_power_current"); }
    if (strncmp(telegram, "1-0:32.7.0", 10) == 0) { L1_VOLTAGE = getValue(telegram, len, '(', '*'); mark_seen("l1_voltage"); }
    if (strncmp(telegram, "1-0:52.7.0", 10) == 0) { L2_VOLTAGE = getValue(telegram, len, '(', '*'); mark_seen("l2_voltage"); }
    if (strncmp(telegram, "1-0:72.7.0", 10) == 0) { L3_VOLTAGE = getValue(telegram, len, '(', '*'); mark_seen("l3_voltage"); }
    if (strncmp(telegram, "0-0:14.7.0", 10) == 0) { FREQUENCY = getValue(telegram, len, '(', '*'); mark_seen("frequency"); }
    if (strstr(telegram, "24.2.1") || strstr(telegram, "24.2.3")) { GAS_METER_M3 = getValue(telegram, len, '(', '*'); mark_seen("gas_meter_m3"); }
    if (strncmp(telegram, "0-0:96.14.0", 11) == 0) { ACTUAL_TARIF = getValue(telegram, len, '(', ')'); mark_seen("actual_tarif_group"); }
    if (strncmp(telegram, "0-0:96.7.21", 11) == 0) { SHORT_POWER_OUTAGES = getValue(telegram, len, '(', ')'); mark_seen("short_power_outages"); }
    if (strncmp(telegram, "0-0:96.7.9", 10) == 0) { LONG_POWER_OUTAGES = getValue(telegram, len, '(', ')'); mark_seen("long_power_outages"); }
    if (strncmp(telegram, "1-0:32.32.0", 11) == 0) { SHORT_POWER_DROPS = getValue(telegram, len, '(', ')'); mark_seen("short_power_drops"); }
    if (strncmp(telegram, "1-0:32.36.0", 11) == 0) { SHORT_POWER_PEAKS = getValue(telegram, len, '(', ')'); mark_seen("short_power_peaks"); }
    if (strncmp(telegram, "1-0:1.4.0", 9) == 0) { mActualAverage15mPeak = getValue(telegram, len, '(', '*'); mark_seen("actual_average_15m_peak"); }
    if (strncmp(telegram, "1-0:1.6.0", 9) == 0) { mMax15mPeakThisMonth = getValue(telegram, len, '(', '*'); mark_seen("thismonth_max_15m_peak"); }
    if (strncmp(telegram, "0-0:98.1.0", 10) == 0) {
        Last13MonthsPeaks_json.clear(); mark_seen("last13months_average_15m_peak");
        char* ptr = strchr(telegram, '(');
        if (ptr) {
            unsigned long count = strtol(ptr + 1, NULL, 10);
            Last13MonthsPeaks_json["count"] = count; Last13MonthsPeaks_json["unit"] = "W";
            JsonArray peakvalues = Last13MonthsPeaks_json["values"].to<JsonArray>();
            for (int i = 0; i < 2 && ptr; i++) ptr = strchr(ptr + 1, '(');
            long sum = 0; int valid_values = 0;
            while (ptr) {
                ptr = strchr(ptr + 1, '('); if (!ptr) break;
                ptr = strchr(ptr + 1, '('); if (!ptr) break;
                ptr = strchr(ptr + 1, '('); if (!ptr) break;
                float val_kW = atof(ptr + 1); long val_W = (long)(val_kW * 1000.0);
                peakvalues.add(val_W); sum += val_W; valid_values++;
                ptr = strchr(ptr, ')');
            }
            if (valid_values > 0 && count == (unsigned long)valid_values) mAverage15mPeakLast13months = sum / valid_values;
            else mAverage15mPeakLast13months = -1;
        }
    }
    return validCRCFound;
}

void processLine(int len) {
    if (len >= P1_MAXLINELENGTH - 2) len = P1_MAXLINELENGTH - 3; 
    telegram[len] = 0; // Null terminate
    
    // Remote Debugging: Publish the first few lines to MQTT to verify baud/inversion
    if (!system_verified && valid_telegram_count == 0) {
        static int debug_lines = 0;
        if (debug_lines < 5) {
            char debug_topic[128]; snprintf(debug_topic, sizeof(debug_topic), "%s/debug_raw", MQTT_ROOT_TOPIC);
            char debug_payload[256]; snprintf(debug_payload, sizeof(debug_payload), "LEN: %d | DATA: %s", len, telegram);
            mqtt_client.publish(debug_topic, debug_payload, false);
            debug_lines++;
        }
    }

    if (decode_telegram(len)) {
        if (millis() - LAST_UPDATE_SENT > UPDATE_INTERVAL) { send_data_to_broker(); LAST_UPDATE_SENT = millis(); }
    }
}

void read_p1_hardwareserial() {
    static int pos = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (pos < P1_MAXLINELENGTH - 2) {
            telegram[pos++] = c;
            if (c == '\n') { processLine(pos); pos = 0; memset(telegram, 0, sizeof(telegram)); }
        } else { pos = 0; memset(telegram, 0, sizeof(telegram)); }
    }
}

void read_eeprom(int offset, int len, char* buffer) { for (int i = 0; i < len; ++i) buffer[i] = char(EEPROM.read(i + offset)); buffer[len] = '\0'; }
void write_eeprom(int offset, int len, const char* value) {
    size_t val_len = strlen(value);
    for (int i = 0; i < len; ++i) {
        char charToWrite = ((unsigned)i < val_len) ? value[i] : 0;
        if (EEPROM.read(i + offset) != charToWrite) EEPROM.write(i + offset, charToWrite);
    }
}

bool shouldSaveConfig = false;
void save_wifi_config_callback() { shouldSaveConfig = true; }
void resetWifi() { WiFiManager wifiManager; wifiManager.resetSettings(); ESP.restart(); }
void setup_mdns() { if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80); }

void setup() {
    EEPROM.begin(512);
    Serial.setRxBufferSize(2048); // Increase RX buffer to 2048 to prevent drops during WebUI/MQTT tasks
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_FULL);
    ESP.rtcUserMemoryRead(RTC_BASE_ADDR, (uint32_t*)&rtc_persistent, sizeof(rtc_persistent));
    const char* m_name = "Unknown";
    if (rtc_persistent.marker == RTC_MARKER) {
        switch(rtc_persistent.milestone) {
            case 1: m_name = "Booting"; break;
            case 2: m_name = "WiFi Connecting"; break;
            case 3: m_name = "Running"; break;
            case 4: m_name = "Reading P1"; break;
            case 5: m_name = "Sending MQTT"; break;
        }
    }
    snprintf(last_reset_info, sizeof(last_reset_info), "Reason: %s | Milestone: %s", ESP.getResetReason().c_str(), m_name);
    set_milestone(1);
    USC0(UART0) = USC0(UART0) | BIT(UCRXI); // Invert RX
    pinMode(LED_BUILTIN, OUTPUT);
    WiFi.persistent(false);
    if (drd.detectDoubleReset()) resetWifi();
    ticker.attach(0.6, tick);
    char settings_available[2] = ""; read_eeprom(134, 1, settings_available);
    char ha_val_str[2] = "1";
    if (settings_available[0] == '1') {
        read_eeprom(0, 64, MQTT_HOST); read_eeprom(64, 6, MQTT_PORT);
        read_eeprom(70, 32, MQTT_USER); read_eeprom(102, 32, MQTT_PASS);
        read_eeprom(135, 1, ha_val_str);
        HA_AUTO_DISCOVERY = (ha_val_str[0] == '1');
    }
    WiFiManagerParameter c_host("host", "MQTT hostname", MQTT_HOST, 64);
    WiFiManagerParameter c_port("port", "MQTT port", MQTT_PORT, 6);
    WiFiManagerParameter c_user("user", "MQTT user", MQTT_USER, 32);
    WiFiManagerParameter c_pass("pass", "MQTT pass", MQTT_PASS, 32);
    char ch[200]; snprintf(ch, sizeof(ch), "type='hidden' id='ha_val'><label style='color:#0f0;cursor:pointer;'><input type='checkbox' %s onchange=\"document.getElementById('ha_val').value=this.checked?'1':'0';\"> Enable HA Discovery</label>", HA_AUTO_DISCOVERY ? "checked" : "");
    WiFiManagerParameter c_ha("ha_val", "", ha_val_str, 2, ch);
    WiFiManager wifiManager;
    const char* css = "<style>body{background:#0a0a0a;color:#0f0;font-family:monospace;}.wrap{max-width:450px;margin:20px auto;border:1px solid #0f0;padding:20px;box-shadow:0 0 10px #0f0;}input[type='text'],input[type='password']{background:#111;color:#0f0;border:1px solid #0f0;padding:10px;width:100%;box-sizing:border-box;margin-bottom:10px;}input[type='submit'],button{background:#0f0 !important;color:#000 !important;border:none !important;padding:15px !important;width:100% !important;font-weight:bold !important;cursor:pointer !important;display:block !important;}h1{text-align:center;text-shadow:0 0 5px #0f0;}div,label,a{color:#0f0;}</style>";
    wifiManager.setCustomHeadElement(css);
    wifiManager.setAPCallback([](WiFiManager *m){ ticker.attach(0.2, tick); });
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);
    wifiManager.addParameter(&c_host); wifiManager.addParameter(&c_port);
    wifiManager.addParameter(&c_user); wifiManager.addParameter(&c_pass);
    wifiManager.addParameter(&c_ha);
    set_milestone(2);
    if (!wifiManager.autoConnect()) ESP.restart();
    strcpy(MQTT_HOST, c_host.getValue()); strcpy(MQTT_PORT, c_port.getValue());
    strcpy(MQTT_USER, c_user.getValue()); strcpy(MQTT_PASS, c_pass.getValue());
    if (c_ha.getValue()[0] == '1' || c_ha.getValue()[0] == '0') HA_AUTO_DISCOVERY = (c_ha.getValue()[0] == '1');
    if (shouldSaveConfig) {
        write_eeprom(0, 64, MQTT_HOST); write_eeprom(64, 6, MQTT_PORT);
        write_eeprom(70, 32, MQTT_USER); write_eeprom(102, 32, MQTT_PASS);
        write_eeprom(134, 1, "1"); write_eeprom(135, 1, HA_AUTO_DISCOVERY ? "1" : "0");
        EEPROM.commit();
    }
    ticker.detach(); digitalWrite(LED_BUILTIN, LOW);
    setup_mdns();
    ElegantOTA.begin(&server);
    server.on("/", []() {
        server.sendHeader("Location", "/update");
        server.send(302, "text/plain", "");
    });
    server.begin();
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
    boot_time = millis();
}

void loop() {
    server.handleClient();
    ElegantOTA.loop();
    
    unsigned long now = millis();
    if (WiFi.status() != WL_CONNECTED) { if (now - lastReconnectAttempt > 30000) { WiFi.reconnect(); lastReconnectAttempt = now; } return; }
    if (!mqtt_client.connected()) { if (now - lastReconnectAttempt > 5000) { lastReconnectAttempt = now; if (mqtt_reconnect()) lastReconnectAttempt = 0; } } 
    else mqtt_client.loop();
    read_p1_hardwareserial();
    drd.loop();
    if (!discovery_published && (now - boot_time > 30000)) { if (mqtt_client.connected()) publish_ha_discovery(); }
}
