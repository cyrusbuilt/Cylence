#ifndef ESP8266
	#error This firmware is only compatible with ESP8266 controllers.
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <FS.h>
#include <time.h>
#include <TZ.h>
#include "ArduinoJson.h"
#include "LED.h"
#include "PubSubClient.h"
#include "Relay.h"
#include "ResetManager.h"
#include "TaskScheduler.h"
#include "TelemetryHelper.h"
#include "config.h"

#define FIRMWARE_VERSION "1.0"

// Pin definitions
#define PIN_LED_ACTIVE 12
#define PIN_LED_NET 15
#define PIN_RELAY 14

// Forward declarations
void onRelayStateChange(RelayInfo *sender);
void onCheckWifi();
void onCheckMqtt();
void onSyncClock();
void onMqttMessage(char* topic, byte* payload, unsigned int length);

// Global vars
#ifdef ENABLE_MDNS
	#include <ESP8266mDNS.h>
	MDNSResponder mdns;
#endif
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Task tCheckWiFi(CHECK_WIFI_INTERVAL, TASK_FOREVER, &onCheckWifi);
Task tCheckMqtt(CHECK_MQTT_INTERVAL, TASK_FOREVER, &onCheckMqtt);
Task tClockSync(CLOCK_SYNC_INTERVAL, TASK_FOREVER, &onSyncClock);
Scheduler taskMan;
LED activationLED(PIN_LED_ACTIVE, NULL);
LED netLED(PIN_LED_NET, NULL);
Relay bellRelay(PIN_RELAY, onRelayStateChange, "Killswitch");
config_t config;
bool filesystemMounted = false;
volatile SystemState sysState = SystemState::BOOTING;

void setup() {

}

void loop() {

}
