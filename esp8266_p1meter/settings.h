// **********************************
// * Settings                       *
// **********************************

// * Home Assistant Branding (Hardcoded)
#define HA_DEVICE_NAME "P1Meter"
#define HA_MANUFACTURER "AciDCooL Labs"
#define HA_MODEL "ESP8266-P1"

//Reset wifi settings:
#define DRD_TIMEOUT 10

// *RTC Memory Address
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
#define DRD_ADDRESS 1
#elif ARDUINO_ESP8266_NODEMCU
#define DRD_ADDRESS 0
#else 
#define DRD_ADDRESS 1
#endif

#define UPDATE_INTERVAL 1000 
#define BAUD_RATE 115200
#define P1_MAXLINELENGTH 2050 
#define MQTT_BUFFER_SIZE 1024 
#define HOSTNAME "p1meter"
#define WIFI_TIMEOUT 30000
#define MQTT_MAX_RECONNECT_TRIES 10
#define MQTT_ROOT_TOPIC "sensors/power/p1meter"
#define HA_DISCOVERY_PREFIX "homeassistant"

bool HA_AUTO_DISCOVERY = true; 
long LAST_RECONNECT_ATTEMPT = 0;
long LAST_UPDATE_SENT = 0;

// * To be filled with EEPROM data
char MQTT_HOST[65] = "192.168.1.100";
char MQTT_PORT[7] = "1883";
char MQTT_USER[33] = "mqtt_user";
char MQTT_PASS[33] = "mqtt_pass";
char OTA_PASS[33] = "admin"; // Default OTA password if none is set

char telegram[P1_MAXLINELENGTH];

// * Initialized data values
long CONSUMPTION_LOW_TARIF = 0;
long CONSUMPTION_HIGH_TARIF = 0;
long RETURNDELIVERY_LOW_TARIF = 0;
long RETURNDELIVERY_HIGH_TARIF = 0;
long ACTUAL_CONSUMPTION = 0;
long ACTUAL_RETURNDELIVERY = 0;
long GAS_METER_M3 = 0;
long L1_INSTANT_POWER_USAGE = 0;
long L2_INSTANT_POWER_USAGE = 0;
long L3_INSTANT_POWER_USAGE = 0;
long L1_INSTANT_POWER_RETURNDELIVERY = 0;
long L2_INSTANT_POWER_RETURNDELIVERY = 0;
long L3_INSTANT_POWER_RETURNDELIVERY = 0;
long L1_INSTANT_POWER_CURRENT = 0;
long L2_INSTANT_POWER_CURRENT = 0;
long L3_INSTANT_POWER_CURRENT = 0;
long L1_VOLTAGE = 0;
long L2_VOLTAGE = 0;
long L3_VOLTAGE = 0;
long FREQUENCY = 0;
long ACTUAL_TARIF = 0;
long SHORT_POWER_OUTAGES = 0;
long LONG_POWER_OUTAGES = 0;
long SHORT_POWER_DROPS = 0;
long SHORT_POWER_PEAKS = 0;
long mActualAverage15mPeak = 0;
long mMax15mPeakThisMonth = 0;
long mAverage15mPeakLast13months = 0;

JsonDocument Last13MonthsPeaks_json; 
unsigned int currentCRC = 0;
unsigned long lastReconnectAttempt = 0;
bool mqttReconnectPending = false;
