/*
 * Configuration Variables
 */

// WIFI config
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASS "WIFI_PASSWORD"

// MQTT config
#define MQTT_HOST "mqtthost.example.com"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_username"
#define MQTT_PASS "mqtt_password"
#define MQTT_TOPIC_PREFIX "mqtt/topic/prefix"
#define MQTT_TOPIC(s) (MQTT_TOPIC_PREFIX s)

// door_open & door_closed sensors connect D5/GPIO14 or D6/GPIO12 to GND
#define SENSOR0_PIN 14
#define SENSOR1_PIN 12

// relay is energized when D1/GPIO5 is HIGH
#define RELAY_PIN 5

// hardware reset is acheived by driving D2/GPIO4 LOW
#define RESET_PIN 4

// Button press relay close duration in milliseconds
#define BUTTON_DWELL_MS 300

// output publish interval
#define OUTPUT_INTERVAL 1000

// Force reset if this timer expires without a connection
#define WATCHDOG_TIMEOUT 10000
