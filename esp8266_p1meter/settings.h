// **********************************
// * Settings                       *
// **********************************

// * Home Assistant Branding (Hardcoded - No UI option)
#define HA_DEVICE_NAME "P1Meter"
#define HA_MANUFACTURER "AciDCooL Labs"
#define HA_MODEL "ESP8266-P1"

//Reset wifi settings:
// *Number of seconds after reset during which a
// *subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// *RTC Memory Address for the DoubleResetDetector to use
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI // WeMos mini and D1 R2
#define DRD_ADDRESS 1
#elif ARDUINO_ESP8266_NODEMCU // Wio Link and NodeMCU 1.0 (also 0.9), use for ESP12
#define DRD_ADDRESS 0 // FLASH BUTTON
#else 
#define DRD_ADDRESS 1
#endif

// Update treshold in milliseconds, messages will only be sent on this interval
#define UPDATE_INTERVAL 1000 // 1s for near-instant updates

// * Baud rate for both hardware and software
#define BAUD_RATE 115200
//#define BAUD_RATE 9600

// The used serial pins, note that this can only be UART0, as other serial port doesn't support inversion
// By default the UART0 serial will be used. These settings displayed here just as a reference.
// #define SERIAL_RX RX
// #define SERIAL_TX TX

// * Max telegram length
#define P1_MAXLINELENGTH 2050 // Increased to 2050 to allow for safe termination
#define MQTT_BUFFER_SIZE 1024 // Increased for large peak JSON messages

// * The hostname of our little creature
#define HOSTNAME "p1meter"

// * The password used for OTA
#define OTA_PASSWORD "rXqEqAY7D8L9n2"

// * Wifi timeout in milliseconds
#define WIFI_TIMEOUT 30000

// * MQTT network settings
#define MQTT_MAX_RECONNECT_TRIES 10

// * MQTT root topic
#define MQTT_ROOT_TOPIC "sensors/power/p1meter"

// * Home Assistant Auto Discovery
#define HA_DISCOVERY_PREFIX "homeassistant"
bool HA_AUTO_DISCOVERY = true; // Default to true

// * MQTT Last reconnection counter
long LAST_RECONNECT_ATTEMPT = 0;

long LAST_UPDATE_SENT = 0;

// * To be filled with EEPROM data
char MQTT_HOST[65] = "192.168.1.100";
char MQTT_PORT[7] = "1883";
char MQTT_USER[33] = "mqtt_user";
char MQTT_PASS[33] = "mqtt_pass";

// * Set to store received telegram
char telegram[P1_MAXLINELENGTH];

// * Set to store the data values read
long CONSUMPTION_LOW_TARIF;
long CONSUMPTION_HIGH_TARIF;

long RETURNDELIVERY_LOW_TARIF;
long RETURNDELIVERY_HIGH_TARIF;

long ACTUAL_CONSUMPTION;
long ACTUAL_RETURNDELIVERY;
long GAS_METER_M3;

long L1_INSTANT_POWER_USAGE;
long L2_INSTANT_POWER_USAGE;
long L3_INSTANT_POWER_USAGE;
long L1_INSTANT_POWER_RETURNDELIVERY;
long L2_INSTANT_POWER_RETURNDELIVERY;
long L3_INSTANT_POWER_RETURNDELIVERY;
long L1_INSTANT_POWER_CURRENT;
long L2_INSTANT_POWER_CURRENT;
long L3_INSTANT_POWER_CURRENT;
long L1_VOLTAGE;
long L2_VOLTAGE;
long L3_VOLTAGE;
long FREQUENCY;

// Set to store data counters read
long ACTUAL_TARIF;
long SHORT_POWER_OUTAGES;
long LONG_POWER_OUTAGES;
long SHORT_POWER_DROPS;
long SHORT_POWER_PEAKS;

long mActualAverage15mPeak = 0;
long mMax15mPeakThisMonth = 0;
long mAverage15mPeakLast13months = 0;

JsonDocument Last13MonthsPeaks_json; // In ArduinoJson 7, JsonDocument handles its own memory.

// * Set during CRC checking
unsigned int currentCRC = 0;

// Reconnection state
unsigned long lastReconnectAttempt = 0;
bool mqttReconnectPending = false;
