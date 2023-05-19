#ifndef _CONFIG_H
#define _CONFIG_H

#include <IPAddress.h>

#define DEBUG
#define DEVICE_NAME "CYLENCE"
#define DEVICE_CLASS "cylence"
#define BAUD_RATE 115200
#define ENABLE_OTA
#define ENABLE_MDNS
#define CONFIG_FILE_PATH "/config.json"
#define DEFAULT_SSID "your_ssid_here"
#define DEFAULT_PASSWORD "your_wifi_password"
#define CLOCK_TIMEZONE -4
#define CHECK_WIFI_INTERVAL 30000
#define CLOCK_SYNC_INTERVAL 3600000
#define CHECK_MQTT_INTERVAL 60000 * 5
#define MQTT_TOPIC_STATUS "cylence/status"
#define MQTT_TOPIC_CONTROL "cylence/control"
#define MQTT_TOPIC_DISCOVERY "redqueen/config"
#define MQTT_BROKER "your_mqtt_broker_ip"
#define MQTT_PORT 8883
#ifdef ENABLE_OTA
	#include <ArduinoOTA.h>
	#define OTA_HOST_PORT 8266
	#define OTA_PASSWORD "your_ota_password_here"
#endif
const IPAddress defaultIp(192, 168, 0, 238);
const IPAddress defaultGw(192, 168, 0, 1);
const IPAddress defaultSm(255, 255, 255, 0);
const IPAddress defaultDns(192, 168, 0, 1);

typedef struct {
	// Network stuff
	String hostname;
	String ssid;
	String password;
	IPAddress ip;
	IPAddress gw;
	IPAddress sm;
	IPAddress dns;
	bool useDhcp;

	uint8_t clockTimezone;

	// MQTT stuff
	String mqttTopicStatus;
	String mqttTopicControl;
	String mqttTopicDiscovery;
	String mqttBroker;
	String mqttUsername;
	String mqttPassword;
	uint16_t mqttPort;

	// OTA stuff
	uint16_t otaPort;
	String otaPassword;
} config_t;

#endif
