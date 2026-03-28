#define VERSION "1.6.9"

#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
#define BOARD_NAME "Wemos D1 Mini"
#elif defined(ARDUINO_ESP8266_NODEMCU)
#define BOARD_NAME "NodeMCU"
#else
#define BOARD_NAME "Generic ESP8266"
#endif

// * Libraries
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <ESP8266WebServer.h>
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
void send_metric(const char* name, long metric, long& last_value);
void send_metric_scaled(const char* name, long metric, long& last_value);
void send_data_to_broker();
bool decode_telegram(int len);
void processLine(int len);
void read_p1_hardwareserial();
void init_p1_serial();
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
    uint32_t reboot_count; // Tracks reboots since last hard power loss
} rtc_persistent;

#define RTC_MARKER 0xDEADBEEF
#define RTC_BASE_ADDR 64

bool system_verified = false;
int valid_telegram_count = 0;
bool ota_in_progress = false;
unsigned long last_p1_received = 0; 
unsigned long stabilization_started_at = 0;
int crc_fail_streak = 0;

#define STABILIZATION_THRESHOLD 3
#define MAX_ENERGY_DELTA 10000 // Max 10kWh jump in 1s
#define SERIAL_WEDGE_TIMEOUT 60000 // 60 seconds without data = re-init
#define STABILIZATION_TIMEOUT 300000 // 5 minutes stuck stabilizing = re-init
#define MAX_CRC_FAIL_STREAK 10 // 10 bad telegrams = re-init

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
    if (current <= 0) return true;
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
    const char* category;
};

const HASensor sensors[] = {
    {"consumption_low_tarif", "Consumption Low Tarif", "Wh", "energy", "total_increasing", "", ""},
    {"consumption_high_tarif", "Consumption High Tarif", "Wh", "energy", "total_increasing", "", ""},
    {"returndelivery_low_tarif", "Return Low Tarif", "Wh", "energy", "total_increasing", "", ""},
    {"returndelivery_high_tarif", "Return High Tarif", "Wh", "energy", "total_increasing", "", ""},
    {"actual_consumption", "Actual Consumption", "W", "power", "measurement", "", ""},
    {"actual_returndelivery", "Actual Return", "W", "power", "measurement", "", ""},
    {"l1_instant_power_usage", "L1 Power Usage", "W", "power", "measurement", "", ""},
    {"l2_instant_power_usage", "L2 Power Usage", "W", "power", "measurement", "", ""},
    {"l3_instant_power_usage", "L3 Power Usage", "W", "power", "measurement", "", ""},
    {"l1_instant_power_returndelivery", "L1 Power Return", "W", "power", "measurement", "", ""},
    {"l2_instant_power_returndelivery", "L2 Power Return", "W", "power", "measurement", "", ""},
    {"l3_instant_power_returndelivery", "L3 Power Return", "W", "power", "measurement", "", ""},
    {"l1_instant_power_current", "L1 Current", "A", "current", "measurement", "", ""},
    {"l2_instant_power_current", "L2 Current", "A", "current", "measurement", "", ""},
    {"l3_instant_power_current", "L3 Current", "A", "current", "measurement", "", ""},
    {"l1_voltage", "L1 Voltage", "V", "voltage", "measurement", "", ""},
    {"l2_voltage", "L2 Voltage", "V", "voltage", "measurement", "", ""},
    {"l3_voltage", "L3 Voltage", "V", "voltage", "measurement", "", ""},
    {"frequency", "Line Frequency", "Hz", "frequency", "measurement", "", ""},
    {"gas_meter_m3", "Gas Meter", "m\u00b3", "gas", "total_increasing", "", ""},
    {"actual_average_15m_peak", "Peak 15m Average", "W", "power", "measurement", "", ""},
    {"thismonth_max_15m_peak", "Peak Max This Month", "W", "power", "measurement", "", ""},
    {"last13months_average_15m_peak", "Peak 13 Months Avg", "W", "power", "measurement", "", ""},
    {"wifi_rssi", "WiFi Signal", "dBm", "signal_strength", "measurement", "mdi:wifi", "diagnostic"},
    {"ip_address", "IP Address", "", "", "", "mdi:network", "diagnostic"},
    {"mac_address", "MAC Address", "", "", "", "mdi:fingerprint", "diagnostic"},
    {"board_type", "Board Type", "", "", "", "mdi:chip", "diagnostic"},
    {"firmware_version", "Firmware Version", "", "", "", "mdi:xml", "diagnostic"},
    {"mqtt_server", "MQTT Server", "", "", "", "mdi:server", "diagnostic"},
    {"reboot_count", "Reboot Count", "", "", "", "mdi:counter", "diagnostic"}
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
    char topic[128], status_topic[128];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
    char dev_name[64], dev_id[32];
    snprintf(dev_name, sizeof(dev_name), "%s-%06X", HA_DEVICE_NAME, ESP.getChipId());
    snprintf(dev_id, sizeof(dev_id), "p1meter_%06X", ESP.getChipId());

    JsonDocument doc;
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        const HASensor& s = sensors[i];
        if (strcmp(s.category, "diagnostic") != 0 && !seen_metrics[i]) continue;
        
        doc.clear();
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
        if (strlen(s.category) > 0) doc["entity_category"] = s.category;
        
        JsonObject dev = doc["device"].to<JsonObject>();
        dev["identifiers"][0] = dev_id;
        dev["name"] = dev_name;
        dev["manufacturer"] = HA_MANUFACTURER;
        dev["model"] = HA_MODEL;
        dev["sw_version"] = VERSION;
        
        char buffer[600];
        serializeJson(doc, buffer, sizeof(buffer));
        mqtt_client.publish(topic, buffer, true);
        mqtt_client.loop(); yield(); delay(50); // Spread power draw
    }
    discovery_published = true;
}

bool mqtt_reconnect() {
    char status_topic[128], client_id[64];
    snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
    snprintf(client_id, sizeof(client_id), "%s-%06X", HOSTNAME, ESP.getChipId());
    
    if (mqtt_client.connect(client_id, MQTT_USER, MQTT_PASS, status_topic, 1, true, "offline")) {
        if (system_verified) mqtt_client.publish(status_topic, "online", true);
        else mqtt_client.publish(status_topic, "stabilizing", true);

        if (strlen(last_reset_info) > 0) {
            char r_topic[128], r_payload[200];
            snprintf(r_topic, sizeof(r_topic), "%s/last_reset", MQTT_ROOT_TOPIC);
            snprintf(r_payload, sizeof(r_payload), "Version: %s | %s", VERSION, last_reset_info);
            mqtt_client.publish(r_topic, r_payload, true);
        }
        
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
    mqtt_client.loop(); yield(); delay(50);
}

long LAST_CON_LOW = -1, LAST_CON_HIGH = -1, LAST_RET_LOW = -1, LAST_RET_HIGH = -1;
long LAST_ACT_CON = -1, LAST_ACT_RET = -1, LAST_GAS = -1;
long LAST_L1_P = -1, LAST_L2_P = -1, LAST_L3_P = -1, LAST_L1_C = -1, LAST_L2_C = -1, LAST_L3_C = -1, LAST_L1_V = -1, LAST_L2_V = -1, LAST_L3_V = -1;
long LAST_L1_R = -1, LAST_L2_R = -1, LAST_L3_R = -1;
long LAST_TARIF = -1, LAST_S_OUT = -1, LAST_L_OUT = -1, LAST_S_DROP = -1, LAST_S_PEAK = -1;
long LAST_AVG_15M = -1, LAST_MAX_15M = -1, LAST_AVG_13MO = -1, LAST_FREQ = -1;
long LAST_REBOOT_COUNT = -1;
unsigned long LAST_HEARTBEAT = 0;

void send_metric(const char* name, long metric, long& last_value) {
    bool seen = false;
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        if (strcmp(sensors[i].id, name) == 0) { if (!seen_metrics[i] && strcmp(sensors[i].category, "diagnostic") != 0) return; seen = true; break; }
    }
    if (!seen) return;
    if (metric != last_value || (millis() - LAST_HEARTBEAT > 20000)) {
        char topic[128], payload[32];
        snprintf(topic, sizeof(topic), "%s/%s", MQTT_ROOT_TOPIC, name);
        ltoa(metric, payload, 10);
        if (mqtt_client.publish(topic, payload, false)) last_value = metric;
        mqtt_client.loop(); yield(); delay(50); 
    }
}

void send_metric_scaled(const char* name, long metric, long& last_value) {
    bool seen = false;
    for (size_t i = 0; i < SENSOR_COUNT; i++) {
        if (strcmp(sensors[i].id, name) == 0) { if (!seen_metrics[i] && strcmp(sensors[i].category, "diagnostic") != 0) return; seen = true; break; }
    }
    if (!seen) return;
    if (metric != last_value || (millis() - LAST_HEARTBEAT > 20000)) {
        char topic[128], payload[32];
        snprintf(topic, sizeof(topic), "%s/%s", MQTT_ROOT_TOPIC, name);
        long integer_part = metric / 1000;
        int fractional_part = abs((int)(metric % 1000));
        snprintf(payload, sizeof(payload), "%ld.%03d", integer_part, fractional_part);
        if (mqtt_client.publish(topic, payload, false)) last_value = metric;
        mqtt_client.loop(); yield(); delay(50); 
    }
}

void send_data_to_broker() {
    set_milestone(5); // Milestone 5 = Sending Data
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
    send_metric_scaled("l1_instant_power_current", L1_INSTANT_POWER_CURRENT, LAST_L1_C);
    send_metric_scaled("l2_instant_power_current", L2_INSTANT_POWER_CURRENT, LAST_L2_C);
    send_metric_scaled("l3_instant_power_current", L3_INSTANT_POWER_CURRENT, LAST_L3_C);
    send_metric_scaled("l1_voltage", L1_VOLTAGE, LAST_L1_V);
    send_metric_scaled("l2_voltage", L2_VOLTAGE, LAST_L2_V);
    send_metric_scaled("l3_voltage", L3_VOLTAGE, LAST_L3_V);
    send_metric_scaled("frequency", FREQUENCY, LAST_FREQ);
    send_metric_scaled("gas_meter_m3", GAS_METER_M3, LAST_GAS);
    send_metric("actual_tarif_group", ACTUAL_TARIF, LAST_TARIF);
    send_metric("short_power_outages", SHORT_POWER_OUTAGES, LAST_S_OUT);
    send_metric("long_power_outages", LONG_POWER_OUTAGES, LAST_L_OUT);
    send_metric("short_power_drops", SHORT_POWER_DROPS, LAST_S_DROP);
    send_metric("short_power_peaks", SHORT_POWER_PEAKS, LAST_S_PEAK);
    send_metric("actual_average_15m_peak", mActualAverage15mPeak, LAST_AVG_15M);
    send_metric("thismonth_max_15m_peak", mMax15mPeakThisMonth, LAST_MAX_15M);
    send_metric("last13months_average_15m_peak", mAverage15mPeakLast13months, LAST_AVG_13MO);
    
    if (millis() - LAST_HEARTBEAT > 20000) {
        char t[128], p[128]; 
        snprintf(t, sizeof(t), "%s/wifi_rssi", MQTT_ROOT_TOPIC);
        ltoa(WiFi.RSSI(), p, 10); mqtt_client.publish(t, p, false);
        snprintf(t, sizeof(t), "%s/ip_address", MQTT_ROOT_TOPIC);
        mqtt_client.publish(t, WiFi.localIP().toString().c_str(), false);
        snprintf(t, sizeof(t), "%s/mac_address", MQTT_ROOT_TOPIC);
        mqtt_client.publish(t, WiFi.macAddress().c_str(), false);
        snprintf(t, sizeof(t), "%s/board_type", MQTT_ROOT_TOPIC);
        mqtt_client.publish(t, BOARD_NAME, false);
        snprintf(t, sizeof(t), "%s/firmware_version", MQTT_ROOT_TOPIC);
        mqtt_client.publish(t, VERSION, false);
        snprintf(t, sizeof(t), "%s/mqtt_server", MQTT_ROOT_TOPIC);
        snprintf(p, sizeof(p), "%s:%s", MQTT_HOST, MQTT_PORT);
        mqtt_client.publish(t, p, false);
        
        send_metric("reboot_count", (long)rtc_persistent.reboot_count, LAST_REBOOT_COUNT);
        
        LAST_HEARTBEAT = millis();
    }
    
    if (!Last13MonthsPeaks_json.isNull()) {
        char t[128]; snprintf(t, sizeof(t), "%s/last13months_peaks_json", MQTT_ROOT_TOPIC);
        send_mqtt_json(t, Last13MonthsPeaks_json); 
    }
    update_rtc_totals(); set_milestone(3); // Back to Running
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
        if (isNumber(res, l)) { if (endchar == '*') return (long)(atof(res) * 1000.0); return (long)atof(res); }
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
            crc_fail_streak++;
            char debug_topic[128]; snprintf(debug_topic, sizeof(debug_topic), "%s/debug_crc", MQTT_ROOT_TOPIC);
            char debug_msg[128]; snprintf(debug_msg, sizeof(debug_msg), "CRC FAIL [%d]: Calc=%04X, Meter=%s", crc_fail_streak, currentCRC, messageCRC);
            mqtt_client.publish(debug_topic, debug_msg, false);
        } else {
            crc_fail_streak = 0;
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
                ptr = strchr(ptr + 1, '('); if (!ptr) break; // Move to (date)
                ptr = strchr(ptr + 1, '('); if (!ptr) break; // Move to (value)
                float val_kW = atof(ptr + 1); long val_W = (long)(val_kW * 1000.0);
                peakvalues.add(val_W); sum += val_W; valid_values++;
                ptr = strchr(ptr, ')'); // Move to end of value
            }
            if (valid_values > 0 && count == (unsigned long)valid_values) mAverage15mPeakLast13months = sum / valid_values;
            else mAverage15mPeakLast13months = -1;
        }
    }
    return validCRCFound;
}

void processLine(int len) {
    if (len >= P1_MAXLINELENGTH - 2) len = P1_MAXLINELENGTH - 3; 
    telegram[len] = 0; 
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
    if (ota_in_progress) return; 
    static int pos = 0;
    static bool waiting_for_start = true; 
    if (Serial.available()) last_p1_received = millis();
    while (Serial.available()) {
        char c = Serial.read();
        if (waiting_for_start) {
            if (c == '/') { waiting_for_start = false; pos = 0; telegram[pos++] = c; }
            continue; 
        }
        if (pos < P1_MAXLINELENGTH - 2) {
            telegram[pos++] = c;
            if (c == '\n') { 
                telegram[pos] = 0;
                if (telegram[0] == '!') waiting_for_start = true;
                processLine(pos); 
                pos = 0; 
                memset(telegram, 0, sizeof(telegram)); 
            }
        } else { pos = 0; waiting_for_start = true; memset(telegram, 0, sizeof(telegram)); }
    }
}

void init_p1_serial() {
    Serial.end(); delay(100);
    Serial.setRxBufferSize(2048);
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_FULL);
    USC0(UART0) = USC0(UART0) | BIT(UCRXI);
    while(Serial.available()) Serial.read();
    last_p1_received = millis();
    crc_fail_streak = 0;
    valid_telegram_count = 0;
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
    ESP.rtcUserMemoryRead(RTC_BASE_ADDR, (uint32_t*)&rtc_persistent, sizeof(rtc_persistent));
    const char* m_name = "Unknown";
    
    if (rtc_persistent.marker == RTC_MARKER) {
        rtc_persistent.reboot_count++; // Increment soft reboot counter
        switch(rtc_persistent.milestone) {
            case 1: m_name = "Booting"; break;
            case 2: m_name = "WiFi Connecting"; break;
            case 3: m_name = "Running"; break;
            case 4: m_name = "Reading P1"; break;
            case 5: m_name = "Sending MQTT"; break;
        }
    } else {
        rtc_persistent.marker = RTC_MARKER;
        rtc_persistent.reboot_count = 0; // Fresh power on
    }
    
    snprintf(last_reset_info, sizeof(last_reset_info), "Reason: %s | Milestone: %s", ESP.getResetReason().c_str(), m_name);
    set_milestone(1);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    pinMode(LED_BUILTIN, OUTPUT);
    WiFi.persistent(false);
    if (drd.detectDoubleReset()) {
        if (ESP.getResetReason() == "External System") {
            resetWifi();
        } else {
            drd.stop(); // Clear DRD flag if it was a crash or soft reboot
        }
    }
    ticker.attach(0.6, tick);
    char settings_available[2] = ""; read_eeprom(134, 1, settings_available);
    char ha_val_str[2] = "1";
    if (settings_available[0] == '1') {
        read_eeprom(0, 64, MQTT_HOST); read_eeprom(64, 6, MQTT_PORT);
        read_eeprom(70, 32, MQTT_USER); read_eeprom(102, 32, MQTT_PASS);
        read_eeprom(135, 1, ha_val_str);
        HA_AUTO_DISCOVERY = (ha_val_str[0] == '1');
        read_eeprom(136, 32, OTA_PASS); 
    }
    WiFiManagerParameter c_host("host", "MQTT hostname", MQTT_HOST, 64);
    WiFiManagerParameter c_port("port", "MQTT port", MQTT_PORT, 6);
    WiFiManagerParameter c_user("user", "MQTT user", MQTT_USER, 32);
    WiFiManagerParameter c_pass("pass", "MQTT pass", MQTT_PASS, 32);
    WiFiManagerParameter c_ota_pass("ota_pass", "WebUI Password (Username: admin)", OTA_PASS, 32);
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
    wifiManager.addParameter(&c_ota_pass);
    wifiManager.addParameter(&c_ha);
    set_milestone(2);
    if (!wifiManager.autoConnect()) ESP.restart();
    drd.stop(); // Success: Close the double-reset window immediately
    strcpy(MQTT_HOST, c_host.getValue()); strcpy(MQTT_PORT, c_port.getValue());
    strcpy(MQTT_USER, c_user.getValue()); strcpy(MQTT_PASS, c_pass.getValue());
    strcpy(OTA_PASS, c_ota_pass.getValue());
    if (shouldSaveConfig) {
        write_eeprom(0, 64, MQTT_HOST); write_eeprom(64, 6, MQTT_PORT);
        write_eeprom(70, 32, MQTT_USER); write_eeprom(102, 32, MQTT_PASS);
        write_eeprom(134, 1, "1"); write_eeprom(135, 1, HA_AUTO_DISCOVERY ? "1" : "0");
        write_eeprom(136, 32, OTA_PASS);
        EEPROM.commit();
    }
    ticker.detach(); digitalWrite(LED_BUILTIN, LOW);
    setup_mdns();

    // * Custom Firmware-Only OTA Handlers
    server.on("/update", HTTP_GET, []() {
        if (!server.authenticate("admin", OTA_PASS)) return server.requestAuthentication();
        
        // 1. Send Header & CSS (Chunked to save RAM)
        server.sendHeader("Cache-Control", "no-cache");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", ""); 
        
        server.sendContent("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>P1-METER</title>");
        server.sendContent("<style>body{background:#0a0a0a;color:#0f0;font-family:monospace;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;}");
        server.sendContent(".wrap{border:1px solid #0f0;padding:20px;box-shadow:0 0 15px #0f0;text-align:center;max-width:400px;width:90%;margin:20px;}");
        server.sendContent(".stats{text-align:left;background:#111;padding:15px;margin:20px 0;border-left:3px solid #0f0;font-size:0.9em;}");
        server.sendContent("p{margin:5px 0;border-bottom:1px solid #222;padding-bottom:2px;} .val{color:#fff;float:right;} h1{text-shadow:0 0 5px #0f0;}");
        server.sendContent("input[type='file']{background:#111;color:#0f0;border:1px solid #0f0;padding:10px;width:100%;box-sizing:border-box;margin-bottom:20px;}");
        server.sendContent("input[type='submit']{background:#0f0;color:#000;border:none;padding:15px;width:100%;font-weight:bold;cursor:pointer;text-transform:uppercase;}");
        server.sendContent(".btn-reboot{background:#333;color:#0f0;border:1px solid #0f0;padding:10px;width:100%;margin-top:10px;font-weight:bold;cursor:pointer;display:block;text-decoration:none;}");
        server.sendContent("@keyframes blink{0%{opacity:1;}50%{opacity:0.3;}100%{opacity:1;}} .blink{animation:blink 1s infinite;}</style></head><body>");
        server.sendContent("<div class='wrap'><h1>SYSTEM TELEMETRY</h1><div class='stats'>");

        // 2. Generate and Send Stats (Zero-allocation path)
        char buf[128];
        unsigned long s = millis() / 1000;
        int d = s / 86400; int h = (s % 86400) / 3600; int m = (s % 3600) / 60;
        uint32_t free_ram = ESP.getFreeHeap();
        int ram_pct = (free_ram * 100) / 81920; 
        IPAddress ip = WiFi.localIP();

        snprintf(buf, sizeof(buf), "<p>VERSION <span class='val'>v%s</span></p>", VERSION); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>UPTIME <span class='val'>%dd %dh %dm</span></p>", d, h, m); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>IP <span class='val'>%d.%d.%d.%d</span></p>", ip[0], ip[1], ip[2], ip[3]); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>MAC <span class='val'>%s</span></p>", WiFi.macAddress().c_str()); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>MQTT <span class='val'>%s</span></p>", mqtt_client.connected() ? "CONNECTED" : "DISCONNECTED"); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>SIGNAL <span class='val'>%d dBm</span></p>", WiFi.RSSI()); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>P1 STATE <span class='val'>%s</span></p>", system_verified ? "VERIFIED" : "STABILIZING"); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>LAST DATA <span class='val'>%lu sec ago</span></p>", (millis() - last_p1_received) / 1000); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>FREE RAM <span class='val'>%u bytes (%d%%)</span></p>", free_ram, ram_pct); server.sendContent(buf);
        snprintf(buf, sizeof(buf), "<p>REBOOTS <span class='val'>%u</span></p>", rtc_persistent.reboot_count); server.sendContent(buf);

        // 3. Send Form & Footer
        server.sendContent("</div><form method='POST' action='/update' enctype='multipart/form-data' onsubmit='document.getElementById(\"upd\").style.display=\"none\";document.getElementById(\"prg\").style.display=\"block\";'>");
        server.sendContent("<div id='upd'><input type='file' name='update' accept='.bin'><input type='submit' value='Flash Firmware'></div>");
        server.sendContent("<div id='prg' style='display:none;'><h2 class='blink'>FLASHING...</h2><p>DO NOT CLOSE THIS PAGE</p></div></form>");
        server.sendContent("<button onclick=\"if(confirm('Are you sure you want to reboot?')) location.href='/reboot'\" class='btn-reboot'>REBOOT DEVICE</button></div></body></html>");
        server.sendContent(""); // End of stream
    });

    server.on("/reboot", HTTP_GET, []() {
        if (!server.authenticate("admin", OTA_PASS)) return server.requestAuthentication();
        server.send(200, "text/html", "<!DOCTYPE html><html><head><style>body{background:#0a0a0a;color:#0f0;font-family:monospace;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}.wrap{border:1px solid #0f0;padding:30px;box-shadow:0 0 15px #0f0;text-align:center;}h1{text-shadow:0 0 5px #0f0;}</style></head><body><div class='wrap'><h1>REBOOTING</h1><p>The device is restarting...</p><p>Redirecting in <span id='c'>35</span>s</p></div><script>var s=35;var x=setInterval(function(){s--;document.getElementById(\"c\").textContent=s;if(s<=0){clearInterval(x);location.href=\"/update\";}},1000);</script></body></html>");
        delay(1000);
        ESP.restart();
    });

    server.on("/update", HTTP_POST, []() {
        if (!server.authenticate("admin", OTA_PASS)) return;
        server.sendHeader("Connection", "close");
        if (Update.hasError()) {
            server.send(200, "text/html", "<!DOCTYPE html><html><body style='background:#0a0a0a;color:red;font-family:monospace;text-align:center;padding:50px;'><h1>UPDATE FAILED</h1><a href='/update' style='color:#0f0;'>[ BACK ]</a></body></html>");
        } else {
            server.send(200, "text/html", "<!DOCTYPE html><html><head><style>body{background:#0a0a0a;color:#0f0;font-family:monospace;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}.wrap{border:1px solid #0f0;padding:30px;box-shadow:0 0 15px #0f0;text-align:center;}h1{text-shadow:0 0 5px #0f0;}</style></head><body><div class='wrap'><h1>SUCCESS</h1><p>FIRMWARE INSTALLED</p><p>REBOOTING...</p><p>Redirecting in <span id='c'>35</span>s</p></div><script>var s=35;var x=setInterval(function(){s--;document.getElementById(\"c\").textContent=s;if(s<=0){clearInterval(x);location.href=\"/update\";}},1000);</script></body></html>");
        }
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            ota_in_progress = true;
            Serial.printf("Update Start: %s\n", upload.filename.c_str());
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace)) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
                if (mqtt_client.connected()) {
                    char status_topic[128]; snprintf(status_topic, sizeof(status_topic), "%s/status", MQTT_ROOT_TOPIC);
                    mqtt_client.publish(status_topic, "offline", true);
                    mqtt_client.loop(); mqtt_client.disconnect();
                }
                update_rtc_totals();
            } else Update.printError(Serial);
        }
    });

    server.on("/", []() { server.sendHeader("Location", "/update"); server.send(302, "text/plain", ""); });
    server.begin();
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
    
    init_p1_serial();
    boot_time = millis();
    stabilization_started_at = millis();
    set_milestone(3);
}

void loop() {
    server.handleClient();
    unsigned long now = millis();
    
    // SELF-HEALING Logic
    bool serial_needs_reset = false;
    if (!ota_in_progress) {
        if (now - last_p1_received > SERIAL_WEDGE_TIMEOUT) serial_needs_reset = true;
        if (crc_fail_streak >= MAX_CRC_FAIL_STREAK) serial_needs_reset = true;
        if (!system_verified && (now - stabilization_started_at > STABILIZATION_TIMEOUT)) serial_needs_reset = true;
    }

    if (serial_needs_reset) {
        init_p1_serial();
        stabilization_started_at = now;
    }

    if (WiFi.status() != WL_CONNECTED) { if (now - lastReconnectAttempt > 30000) { WiFi.reconnect(); lastReconnectAttempt = now; } return; }
    if (!mqtt_client.connected()) { if (now - lastReconnectAttempt > 5000) { lastReconnectAttempt = now; if (mqtt_reconnect()) lastReconnectAttempt = 0; } } 
    else mqtt_client.loop();
    read_p1_hardwareserial();
    drd.loop();
    if (!discovery_published && (now - boot_time > 30000)) { if (mqtt_client.connected()) publish_ha_discovery(); }
}
