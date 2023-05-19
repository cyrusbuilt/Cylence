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
#include "Console.h"
#include "ESPCrashMonitor.h"
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
void onCheckWiFi();
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
Task tCheckWiFi(CHECK_WIFI_INTERVAL, TASK_FOREVER, &onCheckWiFi);
Task tCheckMqtt(CHECK_MQTT_INTERVAL, TASK_FOREVER, &onCheckMqtt);
Task tClockSync(CLOCK_SYNC_INTERVAL, TASK_FOREVER, &onSyncClock);
Scheduler taskMan;
HAF_LED activationLED(PIN_LED_ACTIVE, NULL);
HAF_LED netLED(PIN_LED_NET, NULL);
Relay bellRelay(PIN_RELAY, onRelayStateChange, "Killswitch");
config_t config;
bool filesystemMounted = false;
volatile SystemState sysState = SystemState::BOOTING;
volatile bool isActive = false;

String getTimeInfo() {
	time_t now = time(nullptr);
	struct tm *timeinfo = localtime(&now);
	String result = String(asctime(timeinfo));
	result.replace("\n", "");
	return result;
}

void onSyncClock() {
	netLED.on();
	configTime(TZ_America_New_York, "pool.ntp.org");

	Serial.print(F("INIT: Waiting for NTP time sync... "));
	delay(500);
	while (!time(nullptr)) {
		netLED.off();
		ESPCrashMonitor.iAmAlive();
		Serial.print(F("."));
		delay(500);
		netLED.on();
	}

	netLED.off();
	Serial.println(F("DONE"));
	Serial.print(F("INFO: Current time: "));
	Serial.println(getTimeInfo());
}

void publishSystemState() {
	if (!mqttClient.connected()) {
		return;
	}

	netLED.on();

	DynamicJsonDocument doc(400);
	doc["clientId"] = config.hostname.c_str();
	doc["firmwareVersion"] = FIRMWARE_VERSION;
	doc["systemState"] = (uint8_t)sysState;
	doc["silencerState"] = isActive ? "ON" : "OFF";
	doc["lastUpdate"] = getTimeInfo();

	String jsonStr;
	size_t len = serializeJson(doc, jsonStr);
	Serial.print(F("INFO: Publishing system state: "));
	Serial.println(jsonStr);
	if (!mqttClient.publish(config.mqttTopicStatus.c_str(), jsonStr.c_str(), len)) {
		Serial.println(F("ERROR: Failed to publish message."));
	}

	doc.clear();
	netLED.off();
}

void publishDiscoveryPacket() {
	if (!mqttClient.connected()) {
		return;
	}

	DynamicJsonDocument doc(250);
	doc["name"] = config.hostname;
	doc["class"] = DEVICE_CLASS;
	doc["statusTopic"] = config.mqttTopicStatus;
	doc["controlTopic"] = config.mqttTopicControl;

	String jsonStr;
	size_t len = serializeJson(doc, jsonStr);
	Serial.print(F("INFO: Publishing discovery packet: "));
	Serial.println(jsonStr);
	if (!mqttClient.publish(config.mqttTopicDiscovery.c_str(), jsonStr.c_str(), len)) {
		Serial.println(F("ERROR: Failed to publish message."));
	}

	doc.clear();
	netLED.off();
}

void onRelayStateChange(RelayInfo *sender) {
	isActive = sender->state == RelayState::RelayClosed;
	activationLED.setState(isActive ? LEDState::LED_On : LEDState::LED_Off);
	publishSystemState();
}

void resumeNormal() {
	Serial.println(F("INFO: Resuming normal operation..."));
	taskMan.enableAll();
	netLED.off();
	sysState = SystemState::NORMAL;
	publishSystemState();
}

void printNetworkInfo() {
	Serial.print(F("INFO: Local IP: "));
	Serial.println(WiFi.localIP());
	Serial.print(F("INFO: Gateway: "));
	Serial.println(WiFi.gatewayIP());
	Serial.print(F("INFO: Subnet mask: "));
	Serial.println(WiFi.subnetMask());
	Serial.print(F("INFO: DNS server: "));
	Serial.println(WiFi.dnsIP());
	Serial.print(F("INFO: MAC address: "));
	Serial.println(WiFi.macAddress());
	#ifdef DEBUG
		WiFi.printDiag(Serial);
	#endif
}

void reboot() {
	Serial.println(F("INFO: Rebooting..."));
	Serial.flush();
	delay(1000);
	ResetManager.softReset();
}

void saveConfiguration() {
	Serial.print(F("INFO: Saving configuration to: "));
	Serial.print(CONFIG_FILE_PATH);
	Serial.println(F(" ... "));
	if (!filesystemMounted) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Filesystem not mounted."));
		return;
	}

	DynamicJsonDocument doc(400);
	doc["hostname"] = config.hostname;
	doc["useDhcp"] = config.useDhcp;
	IPAddress ipAddr = IPAddress(config.ip);
	doc["ip"] = ipAddr.toString();
	ipAddr = IPAddress(config.gw);
	doc["gateway"] = ipAddr.toString();
	ipAddr = IPAddress(config.sm);
	doc["subnetmask"] = ipAddr.toString();
	ipAddr = IPAddress(config.dns);
	doc["dnsServer"] = ipAddr.toString();
	doc["wifiSSID"] = config.ssid;
	doc["wifiPassword"] = config.password;
	doc["timezone"] = config.clockTimezone;
	doc["mqttBroker"] = config.mqttBroker;
	doc["mqttPort"] = config.mqttPort;
	doc["mqttControlTopic"] = config.mqttTopicControl;
	doc["mqttStatusTopic"] = config.mqttTopicStatus;
	doc["mqttDiscoveryTopic"] = config.mqttTopicDiscovery;
	doc["mqttUsername"] = config.mqttUsername;
	doc["mqttPassword"] = config.mqttPassword;
	#ifdef ENABLE_OTA
		doc["otaPort"] = config.otaPort;
		doc["otaPassword"] = config.otaPassword;
	#endif

	File configFile = SPIFFS.open(CONFIG_FILE_PATH, "w");
	if (!configFile) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Failed to open config file for writing."));
		doc.clear();
		return;
	}

	serializeJsonPretty(doc, configFile);
	doc.clear();
	configFile.flush();
	configFile.close();
	Serial.println(F("DONE"));
}

void printWarningAndContinue(const __FlashStringHelper *message) {
	Serial.println();
	Serial.println(message);
	Serial.println(F("INFO: Continuing... "));
}

void setConfigurationDefaults() {
	String chipId = String(ESP.getChipId(), HEX);
	String defHostname = String(DEVICE_NAME) + "_" + chipId;

	config.hostname = defHostname;
	config.ip = defaultIp;
	config.mqttBroker = MQTT_BROKER;
	config.mqttPassword = "";
	config.mqttPort = MQTT_PORT;
	config.mqttTopicControl = MQTT_TOPIC_CONTROL;
	config.mqttTopicStatus = MQTT_TOPIC_STATUS;
	config.mqttTopicDiscovery = MQTT_TOPIC_DISCOVERY;
	config.mqttUsername = "";
	config.password = DEFAULT_PASSWORD;
	config.sm = defaultSm;
	config.ssid = DEFAULT_SSID;
	config.useDhcp = false;
	config.clockTimezone = CLOCK_TIMEZONE;
	config.dns = defaultDns;
	config.gw = defaultGw;
	
	#ifdef ENABLE_OTA
		config.otaPort = config.otaPort;
		config.otaPassword = config.otaPassword;
	#endif
}

void loadConfiguration() {
	memset(&config, 0, sizeof(config));

	Serial.print(F("INFO: Loading config file "));
	Serial.print(CONFIG_FILE_PATH);
	Serial.print(F(" ... "));
	if (!filesystemMounted) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Filesystem not mounted."));
		return;
	}

	if (!SPIFFS.exists(CONFIG_FILE_PATH)) {
		Serial.println(F("FAIL"));
		Serial.println(F("WARN: Config file does not exist. Creating with default config... "));
		saveConfiguration();
		return;
	}

	File configFile = SPIFFS.open(CONFIG_FILE_PATH, "r");
	if (!configFile) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Unable to open config file. Using default config."));
		return;
	}

	size_t size = configFile.size();
	uint16_t freeMem = ESP.getMaxFreeBlockSize() - 512;
	if (size > freeMem) {
		Serial.println(F("FAIL"));
		Serial.print(F("ERROR: Not enough free memory to load document. Size = "));
		Serial.print(size);
		Serial.print(F(", Free = "));
		Serial.println(freeMem);
		configFile.close();
		return;
	}

	DynamicJsonDocument doc(freeMem);
	DeserializationError error = deserializeJson(doc, configFile);
	if (error) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Failed to parse config file to JSON. Using default config."));
		configFile.close();
		return;
	}

	doc.shrinkToFit();
	configFile.close();

	String chipId = String(ESP.getChipId(), HEX);
	String defHostname = String(DEVICE_NAME) + "_" + chipId;

	config.hostname = doc.containsKey("hostname") ? doc["hostname"].as<String>() : defHostname;
	config.useDhcp = doc.containsKey("isDhcp") ? doc["isDhcp"].as<bool>() : false;

	if (doc.containsKey("ip")) {
		if (!config.ip.fromString(doc["ip"].as<String>())) {
			printWarningAndContinue(F("WARN: Invalid IP in configuration. Falling back to factory default."));
		}
	}
	else {
		config.ip = defaultIp;
	}

	if (doc.containsKey("gateway")) {
		if (!config.gw.fromString(doc["gateway"].as<String>())) {
			printWarningAndContinue(F("WARN: Invalid gateway in configuration. Falling back to factory default."));
		}
	}
	else {
		config.gw = defaultGw;
	}

	if (doc.containsKey("subnetmask")) {
		if (!config.sm.fromString(doc["subnetmask"].as<String>())) {
			printWarningAndContinue(F("WARN: Invalid subnet mask in configuration. Falling back to factory default."));
		}
	}
	else {
		config.sm = defaultSm;
	}

	if (doc.containsKey("dnsServer")) {
		if (!config.dns.fromString(doc["dnsServer"].as<String>())) {
			printWarningAndContinue(F("WARN: Invalid DNS IP in configuration. Falling back to factory default."));
		}
	}
	else {
		config.dns = defaultDns;
	}

	config.ssid = doc.containsKey("wifiSSID") ? doc["wifiSSID"].as<String>() : DEFAULT_SSID;
	config.password = doc.containsKey("wifiPassword") ? doc["wifiPassword"].as<String>() : DEFAULT_PASSWORD;
	config.clockTimezone = doc.containsKey("timezone") ? doc["timezone"].as<uint8_t>() : CLOCK_TIMEZONE;
	config.mqttBroker = doc.containsKey("mqttBroker") ? doc["mqttBroker"].as<String>() : MQTT_BROKER;
	config.mqttPort = doc.containsKey("mqttPort") ? doc["mqttPort"].as<int>() : MQTT_PORT;
	config.mqttTopicControl = doc.containsKey("mqttControlTopic") ? doc["mqttControlTopic"].as<String>() : MQTT_TOPIC_CONTROL;
	config.mqttTopicStatus = doc.containsKey("mqttStatusTopic") ? doc["mqttStatusTopic"].as<String>() : MQTT_TOPIC_STATUS;
	config.mqttTopicDiscovery = doc.containsKey("mqttDiscoveryTopic") ? doc["mqttDiscoveryTopic"].as<String>() : MQTT_TOPIC_DISCOVERY;
	config.mqttUsername = doc.containsKey("mqttUsername") ? doc["mqttUsername"].as<String>() : "";
	config.mqttPassword = doc.containsKey("mqttPassword") ? doc["mqttPassword"].as<String>() : "";

	#ifdef ENABLE_OTA
		config.otaPort = doc.containsKey("otaPort") ? doc["otaPort"].as<uint16_t>() : OTA_HOST_PORT;
		config.otaPassword = doc.containsKey("otaPassword") ? doc["otaPassword"].as<String>() : OTA_PASSWORD;
	#endif

	doc.clear();
	Serial.println(F("DONE"));
}

void doFactoryRestore() {
	Serial.println();
	Serial.println(F("Are you sure you wish to restore to factory defaults? (Y/n)"));
	Console.waitForUserInput();

	String str = Console.getInputString();
	str.toLowerCase();
	if (str == "y") {
		Serial.print(F("INFO: Clearing current config... "));
		if (filesystemMounted) {
			if (SPIFFS.remove(CONFIG_FILE_PATH)) {
				Serial.println(F("DONE"));
				Serial.print(F("INFO: Removed file: "));
				Serial.println(CONFIG_FILE_PATH);

				Serial.print(F("INFO: Rebooting in "));
				for (uint8_t i = 5; i >= 1; i--) {
					Serial.print(i);
					Serial.print(F(" "));
					delay(1000);
				}

				reboot();
			}
			else {
				Serial.println(F("FAIL"));
				Serial.println(F("ERROR: Failed to delete configuration file."));
			}
		}
		else {
			Serial.println(F("FAIL"));
			Serial.println(F("ERROR: Filesystem not mounted."));
		}
	}

	Serial.println();
}

void printAvailableNetworks() {
	ESPCrashMonitor.defer();
	Serial.println(F("INFO: Scanning WiFi networks... "));
	int numNetworks = WiFi.scanNetworks();
	for (int i = 0; i < numNetworks; i++) {
		Serial.print(F("ID: "));
		Serial.print(i);
		Serial.print(F("\tNetwork name: "));
		Serial.print(WiFi.SSID());
		Serial.print(F("\tSignal strength: "));
		Serial.println(WiFi.RSSI());
	}

	Serial.println(F("----------------------------------"));
}

bool reconnectMqttClient() {
	if (mqttClient.connected()) {
		return true;
	}

	netLED.on();
	Serial.print(F("INFO: Attempting to establish MQTT connection to "));
	Serial.print(config.mqttBroker);
	Serial.print(F(" on port "));
	Serial.print(config.mqttPort);
	Serial.println(F(" ... "));
	
	bool didConnect = false;
	if (config.mqttUsername.length() > 0 && config.mqttPassword.length() > 0) {
		didConnect = mqttClient.connect(config.hostname.c_str(), config.mqttUsername.c_str(), config.mqttPassword.c_str());
	}
	else {
		didConnect = mqttClient.connect(config.hostname.c_str());
	}

	if (didConnect) {
		Serial.print(F("INFO: Subscribing to topic: "));
		Serial.println(config.mqttTopicControl);
		mqttClient.subscribe(config.mqttTopicControl.c_str());

		Serial.print(F("INFO: Publishing to topic: "));
		Serial.println(config.mqttTopicStatus);

		Serial.print(F("INFO: Discovery topic: "));
		Serial.println(config.mqttTopicDiscovery);
	}
	else {
		String failReason = TelemetryHelper::getMqttStateDesc(mqttClient.state());
		Serial.print(F("ERROR: Failed to connect to MQTT broker: "));
		Serial.println(failReason);
	}

	netLED.off();
	return didConnect;
}

void onCheckMqtt() {
	Serial.println(F("INFO: Checking MQTT connection status... "));
	if (reconnectMqttClient()) {
		Serial.println(F("INFO: Successfully reconnected to MQTT broker."));
		publishSystemState();
		publishDiscoveryPacket();
	}
	else {
		Serial.println(F("ERROR: MQTT connection lost and reconnect failed."));
		Serial.print(F("INFO: Retrying connection in "));
		Serial.print(CHECK_MQTT_INTERVAL % 1000);
		Serial.println(F(" seconds ..."));
	}
}

void activate() {
	Serial.println(F("INFO: Killswitch active."));
	bellRelay.close();
}

void deactivate() {
	Serial.println(F("INFO: Killswitch deactivated."));
	bellRelay.open();
}

void handleControlRequest(ControlCommand cmd) {
	if (sysState == SystemState::DISABLED && cmd != ControlCommand::ENABLE) {
		// THOU SHALT NOT PASS!!!
		// We can't process this command because we are disabled.
		Serial.print(F("WARN: Ignoring command "));
		Serial.print((uint8_t)cmd);
		Serial.println(F(" because the system is currently disabled."));
		return;
	}

	switch (cmd) {
		case ControlCommand::ENABLE:
			Serial.println(F("INFO: Enabling system."));
			sysState = SystemState::NORMAL;
			break;
		case ControlCommand::DISABLE:
			Serial.println(F("WARN: Disabling system."));
			sysState = SystemState::DISABLED;
			break;
		case ControlCommand::REBOOT:
			reboot();
			break;
		case ControlCommand::REQUEST_STATUS:
			break;
		case ControlCommand::ACTIVATE:
			isActive ? deactivate() : activate();
			break;
		default:
			Serial.print(F("WARN: Unknown command: "));
			Serial.println((uint8_t)cmd);
			break;
	}

	publishSystemState();
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
	Serial.print(F("INFO: [MQTT] Message arrived: ["));
	Serial.print(topic);
	Serial.print(F("] "));

	// It's a lot easier to deal with if we just convert the payload
	// to a string first.
	String msg;
	for (unsigned int i = 0; i < length; i++) {
		msg += (char)payload[i];
	}

	Serial.println(msg);

	DynamicJsonDocument doc(100);
	DeserializationError error = deserializeJson(doc, msg.c_str());
	if (error) {
		Serial.print(F("ERROR: Failed to parse MQTT message to JSON: "));
		Serial.println(error.c_str());
		doc.clear();
		return;
	}

	if (doc.containsKey("clientId")) {
		String id = doc["clientId"].as<String>();
		id.toUpperCase();
		if (!id.equals(config.hostname)) {
			Serial.println(F("WARN: Control message not intended for this host. Ignoring..."));
			doc.clear();
			return;
		}
	}
	else {
		Serial.println(F("WARN: MQTT message does not contain client ID. Ignoring..."));
		doc.clear();
		return;
	}

	if (!doc.containsKey("command")) {
		Serial.println(F("WARN: MQTT message does not contain a control command. Ignoring..."));
		doc.clear();
		return;
	}

	// When system is in the "disabled" state, the only command it will accept
	// is "enable". All other commands are ignored.
	ControlCommand cmd = (ControlCommand)doc["command"].as<uint8_t>();

	doc.clear();
	handleControlRequest(cmd);
}

void failSafe() {
	sysState = SystemState::DISABLED;
	publishSystemState();
	ESPCrashMonitor.defer();
	Serial.println();
	Serial.println(F("ERROR: Entering failsafe (config) mode..."));
	taskMan.disableAll();
	netLED.on();
	Console.enterCommandInterpreter();
}

void initMDNS() {
	#ifdef ENABLE_MDNS
		Serial.print(F("INIT: Starting MDNS responder..."));
		if (WiFi.status() == WL_CONNECTED) {
			ESPCrashMonitor.defer();
			delay(500);

			if (!mdns.begin(config.hostname)) {
				Serial.println(F(" FAILED"));
				return;
			}

			#ifdef ENABLE_OTA
				bool authUpload = config.otaPassword.length() > 0;
				mdns.enableArduino(config.otaPort, authUpload);
			#endif
			Serial.println(F(" DONE"));
		}
		else {
			Serial.println(F(" FAILED"));
		}
	#endif
}

void initFilesystem() {
	Serial.print(F("INIT: Initializing SPIFFS and mounting filesystem... "));
	if (!SPIFFS.begin()) {
		Serial.println(F("FAIL"));
		Serial.println(F("ERROR: Unable to mount filesystem."));
		return;
	}

	filesystemMounted = true;
	Serial.println(F("DONE"));
	setConfigurationDefaults();
	loadConfiguration();
}

void initMQTT() {
	Serial.print(F("INIT: Initializing MQTT client... "));
	mqttClient.setServer(config.mqttBroker.c_str(), config.mqttPort);
	mqttClient.setCallback(onMqttMessage);
	mqttClient.setBufferSize(500);
	Serial.println(F("DONE"));
	if (reconnectMqttClient()) {
		delay(500);
		publishSystemState();
	}
}

void connectWiFi() {
	if (config.hostname) {
		WiFi.hostname(config.hostname.c_str());
	}

	Serial.println(F("DEBUG: Setting mode..."));
	WiFi.mode(WIFI_STA);
	Serial.println(F("DEBUG: Disconnect and clear to prevent auto connect..."));
	WiFi.persistent(false);
	WiFi.disconnect(true);
	ESPCrashMonitor.defer();

	delay(1000);
	if (config.useDhcp) {
		WiFi.config(0U, 0U, 0U, 0U);
	}
	else {
		WiFi.config(config.ip, config.gw, config.sm, config.dns);
	}

	Serial.println(F("DEBUG: Beginning connection..."));
	WiFi.begin(config.ssid, config.password);
	Serial.println(F("DEBUG: Waiting for connection..."));

	const uint8_t maxTries = 20;
	uint8_t currentTry = 0;
	while ((WiFi.status() != WL_CONNECTED) && (currentTry < maxTries)) {
		ESPCrashMonitor.iAmAlive();
		currentTry++;
		netLED.blink(500);
		delay(500);
	}

	if (WiFi.status() != WL_CONNECTED) {
		// Connection failed. May the AP went down? Let's try again later.
		Serial.println(F("ERROR: Failed to connect to WiFi!"));
		Serial.println(F("WARN: Will attempt to reconnect at scheduled interval."));
	}
	else {
		printNetworkInfo();
	}
}

void initWiFi() {
	Serial.println(F("INIT: Initializing WiFi..."));
	printAvailableNetworks();

	Serial.print(F("INFO: Connecting to SSID: "));
	Serial.print(config.ssid);
	Serial.print(F("..."));

	connectWiFi();
}

void initOTA() {
	#ifdef ENABLE_OTA
		Serial.print(F("INIT: Starting OTA updater... "));
		if (WiFi.status() == WL_CONNECTED) {
			ArduinoOTA.setPort(config.otaPort);
			ArduinoOTA.setHostname(config.hostname.c_str());
			ArduinoOTA.setPassword(config.otaPassword.c_str());
			ArduinoOTA.onStart([]() {
				// Handle start of OTA update. Determines update type.
				String type;
				if (ArduinoOTA.getCommand() == U_FLASH) {
					type = "sketch";
				}
				else {
					type = "filesystem";
				}

				sysState = SystemState::UPDATING;
				publishSystemState();
				Serial.println("INFO: Starting OTA update (type: " + type + ") ...");
			});
			ArduinoOTA.onEnd([]() {
				// Handles update completion.
				Serial.println(F("INFO: OTA updater stopped."));
			});
			ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
				// Reports update progress.
				netLED.blink(100);
				ESPCrashMonitor.iAmAlive();
				Serial.printf("INFO: OTA Update Progress: %u%%\r", (progress / (total / 100)));
			});
			ArduinoOTA.onError([](ota_error_t error) {
				// Handles OTA update errors.
				Serial.printf("ERROR: OTA update error [%u]: ", error);
				switch (error) {
					case OTA_AUTH_ERROR:
						Serial.println(F("Auth failed."));
						break;
					case OTA_BEGIN_ERROR:
						Serial.println(F("Begin failed."));
						break;
					case OTA_CONNECT_ERROR:
						Serial.println(F("Connect failed."));
						break;
					case OTA_RECEIVE_ERROR:
						Serial.println(F("Receive failed."));
						break;
					case OTA_END_ERROR:
						Serial.println(F("End failed."));
						break;
					default:
						break;
				}
			});
			ArduinoOTA.begin();
			Serial.println(F("DONE"));
		}
		else {
			Serial.println(F("FAIL"));
		}
	#endif
}

void onCheckWiFi() {
	Serial.println(F("INFO: Checking WiFi connectivity..."));
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println(F("WARN: Lost connection. Attempting reconnect..."));
		connectWiFi();
		if (WiFi.status() == WL_CONNECTED) {
			initMDNS();
			initMQTT();
		}
	}
}

void initSerial() {
	Serial.begin(BAUD_RATE);
	#ifdef DEBUG
		const bool debug = true;
	#else
		const bool debug = false;
	#endif
	Serial.setDebugOutput(debug);
	Serial.println();
	Serial.print(F("Cylence v"));
	Serial.print(FIRMWARE_VERSION);
	Serial.println(F(" booting..."));
}

void initOutputs() {
	Serial.print(F("INIT: Initializing outputs... "));
	activationLED.init();
	activationLED.on();
	netLED.init();
	netLED.on();
	bellRelay.init();
	bellRelay.open();
	Serial.println(F("DONE"));
}

void handleNewHostname(const char* newHostname) {
	if (strcmp(newHostname, config.hostname.c_str()) == 0) {
		config.hostname = newHostname;
		initMDNS();
	}
}

void handleSwitchToDhcp() {
	if (config.useDhcp) {
		Serial.println(F("INFO: DHCP mode already set. Skipping..."));
		Serial.println();
	}
	else {
		config.useDhcp = true;
		Serial.println(F("INFO: Set DHCP mode."));
		WiFi.config(0U, 0U, 0U, 0U);
	}
}

void handleSwitchToStatic(IPAddress newIp, IPAddress newSm, IPAddress newGw, IPAddress newDns) {
	config.ip = newIp;
	config.sm = newSm;
	config.gw = newGw;
	config.dns = newDns;
	Serial.println(F("INFO: Set static network config."));
	WiFi.config(config.ip, config.gw, config.sm, config.dns);
}

void handleReconnectFromConsole() {
	onCheckWiFi();
	if (WiFi.status() == WL_CONNECTED) {
		printNetworkInfo();
		resumeNormal();
	}
	else {
		Serial.println(F("ERROR: Still no network connection."));
		Console.enterCommandInterpreter();
	}
}

void handleWiFiConfig(String newSsid, String newPassword) {
	if (config.ssid != newSsid || config.password != newPassword) {
		config.ssid = newSsid;
		config.password = newPassword;
		connectWiFi();
	}
}

void handleSaveConfig() {
	saveConfiguration();
	WiFi.disconnect(true);
	onCheckWiFi();
}

void handleMqttConfigCommand(String newBroker, int newPort, String newUsername, String newPassw, String newConTopic, String newStatTopic) {
	if (config.mqttBroker != newBroker || config.mqttPort != newPort
		|| config.mqttUsername != newUsername || config.mqttPassword != newPassw
		|| config.mqttTopicControl != newConTopic || config.mqttTopicStatus != newStatTopic) {
		mqttClient.unsubscribe(config.mqttTopicControl.c_str());
		mqttClient.disconnect();

		config.mqttBroker = newBroker;
		config.mqttPort = newPort;
		config.mqttUsername = newUsername;
		config.mqttPassword = newPassw;
		config.mqttTopicControl = newConTopic;
		config.mqttTopicStatus = newStatTopic;

		initMQTT();
		Serial.println();
	}
}

void initConsole() {
	Serial.print(F("INIT: Initializing console... "));
	Console.setHostname(config.hostname);
	Console.setMqttConfig(
		config.mqttBroker,
		config.mqttPort,
		config.mqttUsername,
		config.mqttPassword,
		config.mqttTopicControl,
		config.mqttTopicStatus
	);
	Console.onRebootCommand(reboot);
	Console.onScanNetworks(printAvailableNetworks);
	Console.onFactoryRestore(doFactoryRestore);
	Console.onHostnameChange(handleNewHostname);
	Console.onDhcpConfig(handleSwitchToDhcp);
	Console.onStaticConfig(handleSwitchToStatic);
	Console.onReconnectCommand(handleReconnectFromConsole);
	Console.onWifiConfigCommand(handleWiFiConfig);
	Console.onSaveConfigCommand(handleSaveConfig);
	Console.onMqttConfigCommand(handleMqttConfigCommand);
	Console.onConsoleInterrupt(failSafe);
	Console.onResumeCommand(resumeNormal);
	Serial.println(F("DONE"));
}

void initTaskManager() {
	Serial.print(F("INIT: Initializing task scheduler..."));

	taskMan.init();
	taskMan.addTask(tCheckWiFi);
	taskMan.addTask(tCheckMqtt);
	taskMan.addTask(tClockSync);
	
	tCheckWiFi.enableDelayed(30000);
	tCheckMqtt.enableDelayed(1000);
	tClockSync.enable();
	Serial.println(F("DONE"));
}

void initCrashMonitor() {
	Serial.print(F("INIT: Initializing crash monitor... "));
	ESPCrashMonitor.disableWatchdog();
	Serial.println(F("DONE"));
	ESPCrashMonitor.dump(Serial);
	delay(100);
}

void setup() {
	initSerial();
	initCrashMonitor();
	initOutputs();
	initFilesystem();
	initWiFi();
	initMDNS();
	initMQTT();
	initTaskManager();
	initConsole();
	Serial.println(F("INFO: Boot sequence complete."));
	sysState = SystemState::NORMAL;
	netLED.off();
	activationLED.off();
	ESPCrashMonitor.enableWatchdog(ESPCrashMonitorClass::ETimeout::Timeout_2s);
}

void loop() {
	ESPCrashMonitor.iAmAlive();
	Console.checkInterrupt();
	taskMan.execute();
	#ifdef ENABLE_MDNS
		mdns.update();
	#endif
	#ifdef ENABLE_OTA
		ArduinoOTA.handle();
	#endif
	mqttClient.loop();
}
