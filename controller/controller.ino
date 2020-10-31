/*
  Adapted from PubSub Library Basic ESP8266 MQTT example

  To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

  Connects to MQTT server, pulses garage door switch, publishes door closed sensor state

  subscribes:
   TOPIC/button
   TOPIC/state_enable

  publishes:
   TOPIC/startup (on init)
   TOPIC/button (returns to 0 after 200ms pulse when set to 1)
   TOPIC/state_enable (initializes to 1)
   TOPIC/state (transmitted every 2 seconds when state_enable==1)


  serial-port-sensor
  ------------------
   Disable Serial Port I/O - instead use serial port to sense door switch
   (connect door switch between GND and RX pins)
   set RX as INPUT_PULLUP
   read RX value as status - 0 == RX is shorted to GND

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "config.h"

// Globals
WiFiClient espClient;
PubSubClient client(espClient);
String topic_button_command(MQTT_TOPIC("button"));
String topic_state_enable(MQTT_TOPIC("state_enable"));
String topic_reset(MQTT_TOPIC("reset"));
int state_enable = 1;
long publish_state_time = 0;
long startup_time = 0;


void setup() {

  // first set the reset pin high so we don't reset ourselves
  digitalWrite(RESET_PIN, HIGH);
  pinMode(RESET_PIN, OUTPUT);

  // make sure the relay is de-energized
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  
  startup_time = millis();

  pinMode(SENSOR0_PIN, INPUT_PULLUP);
  pinMode(SENSOR1_PIN, INPUT_PULLUP);

  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT); // Initialize the BUILTIN_LED pin as an output
  digitalWrite(LED_BUILTIN, LOW); // light LED - setup in progress

  setup_wifi();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);

  digitalWrite(LED_BUILTIN, HIGH); // turn LED off- setup done
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }

  // timeslice for MQTT client
  client.loop();

  if (state_enable && (millis() - publish_state_time > OUTPUT_INTERVAL)) {
    int sensor0 = digitalRead(SENSOR0_PIN);
    int sensor1 = digitalRead(SENSOR1_PIN);
    const char *state = "Active";

    Serial.print("sensor0=");
    Serial.println(sensor0 ? "1" : "0");
    Serial.print("sensor1=");
    Serial.println(sensor1 ? "1" : "0");

    if (sensor0==0) {
      state = "Open";
    }

    if (sensor1==0) {
      state = "Closed";
    }

    publish_state("state", state);
  }
}

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("[");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if (!topic_button_command.compareTo(topic)) {
    // Pulse the relay if an 1 was received as first character
    if ((char)payload[0] == '1') {
      Serial.print("Pressing Button...");
      digitalWrite(RELAY_PIN, HIGH);   // energize the relay
      // stay high for BUTTON_DWELL_MS, then go low (momentary close of relay across doorbell button)
      delay(BUTTON_DWELL_MS);
      client.publish(topic_button_command.c_str(), "0");
      publish_state("state", "Active");
    }
    digitalWrite(RELAY_PIN, LOW);   // de-energize the relay
    Serial.println("Button Released.");
  }
  else if (!topic_state_enable.compareTo(topic)) {
    state_enable = (char)payload[0] == '1';
  }
  else if (!topic_reset.compareTo(topic)) {
    publish_state("reset", "0");
    publish_state("state", "Reset");
    delay(OUTPUT_INTERVAL);
    hard_reset();
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "rstms-garage-door-controller-nodemcu-";
    clientId += String(ESP.getChipId(), HEX);

    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");

      // Once connected, subscribe to command variable state channels...
      client.subscribe(topic_button_command.c_str());
      client.subscribe(topic_state_enable.c_str());
      client.subscribe(topic_reset.c_str());

      // publish an announcement...
      client.publish(MQTT_TOPIC("startup"), clientId.c_str());
      
      // publish to initialize the button state and state enable
      client.publish(topic_button_command.c_str(), "0");
      client.publish(topic_state_enable.c_str(), "1");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      // if we've been running for longer than WATCHDOG_TIMEOUT without a connection, reboot
      if ((millis() - startup_time) > WATCHDOG_TIMEOUT) {
        Serial.println("Watchdog timer expired.  Geronimo!");
        hard_reset();
        Serial.println("reset failed");
      } else {
        // Wait 5 seconds before retrying
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    }
  }
}

void publish_state(const char *topic, const char *state) {
  String msg_topic(MQTT_TOPIC_PREFIX);
  msg_topic += topic;
  Serial.print("publish: ");
  Serial.print(msg_topic);
  Serial.print(" ");
  Serial.println(state);
  client.publish(msg_topic.c_str(), state);
  publish_state_time = millis();
}

void hard_reset() {      
  digitalWrite(RESET_PIN, LOW);
  delay(3000);
}
