/*

  RSTMS IOT MVP -- Garage Door Controller

  - Connects to MQTT server 
  - pulse garage door switch on command
  - publishes door sensor state
  - hardware reset on command

  subscribes:
   TOPIC/button
   TOPIC/reset
   TOPIC/state_enable

  publishes:
   TOPIC/startup (on init)
   TOPIC/button (returns to 0 after 200ms pulse when set to 1)
   TOPIC/state_enable (initializes to 1)
   TOPIC/state (transmitted every 2 seconds when state_enable==1)

  door-position-sensor
  --------------------
   Pins GPIO12 and GPIO14 are configured as inputs with a pull-up resistor.
   Each pin is connected through a magnetic switch to GND.
   A magnet attached to the door causes switch closure when the door is fully open or closed.
   A LOW input indicates the presence of the magnet.

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
byte state_enable = 1;
unsigned long publish_state_time = 0;
unsigned long startup_time = 0;
unsigned long reset_pending = 0;

// sensor state change flag
volatile byte sensor_changed = 0;

void setup() {

  // first set the reset pin HIGH so we don't reset ourselves
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, HIGH);

  // make sure the relay is de-energized
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  startup_time = millis();

  // setup sensor 0
  pinMode(SENSOR0_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR0_PIN), sensor_isr, CHANGE);

  // setup sensor 1 
  pinMode(SENSOR1_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR1_PIN), sensor_isr, CHANGE);

  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT); // Initialize the BUILTIN_LED pin as an output
  digitalWrite(LED_BUILTIN, LOW); // light LED - setup in progress

  setup_wifi();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);

  digitalWrite(LED_BUILTIN, HIGH); // turn LED off- setup done
}

void ICACHE_RAM_ATTR sensor_isr() {
  sensor_changed = 1;
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }

  // timeslice for MQTT client
  client.loop();

  // subroutines request future hard_reset by setting millis() value into reset_pending
  if(reset_pending) {
    if(millis() >= reset_pending)
    {
      publish_state("state", "Reset");
      hard_reset();
    }
  }

  if (sensor_changed)
  {
    Serial.println("Sensor Change Detected.");
  }
  
  if (state_enable && (millis() - publish_state_time > OUTPUT_INTERVAL))
  {
    Serial.println("Timed Sensor Output Triggered.");
    sensor_changed = 1;
  }

  if(sensor_changed)
  {
    byte sensor0 = digitalRead(SENSOR0_PIN);
    byte sensor1 = digitalRead(SENSOR1_PIN);
    const char *state = "Error";

    Serial.print("sensor0=");
    Serial.println(sensor0 ? "1" : "0");
    Serial.print("sensor1=");
    Serial.println(sensor1 ? "1" : "0");

    if (sensor0 && sensor1)
    {
      state = "Active";
    }
    else if (!sensor0 && sensor1)
    {
      state = "Open";
    }
    else if (sensor0 && !sensor1)
    {
      state = "Closed";
    }

    publish_state("state", state);
    sensor_changed = 0;
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
  for (unsigned int i = 0; i < length; i++) {
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
    if ((char)payload[0] == '1')
    {
      if(!reset_pending)
      {
        Serial.println("Reset Pending...");
        publish_state("state", "Reset");
        reset_pending = millis() + OUTPUT_INTERVAL;
      }
    }
    else if ((char)payload[0] == '0')
    {
        Serial.println("Reset cleared.");
    }
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

      // publish startup announcement...
      client.publish(MQTT_TOPIC("startup"), clientId.c_str());
      
      // publish to initialize the button state and state enable
      client.publish(topic_button_command.c_str(), "0");
      client.publish(topic_state_enable.c_str(), "1");
      client.publish(topic_reset.c_str(), "0");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      // if we've been running for longer than WATCHDOG_TIMEOUT without a connection, reboot
      if ((millis() - startup_time) > WATCHDOG_TIMEOUT) {
        Serial.println("Watchdog timer expired.");
        hard_reset();
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
  Serial.println("hard_reset: Geronimo!");
  digitalWrite(RESET_PIN, LOW);
  Serial.println("hard_reset: failure");
}
