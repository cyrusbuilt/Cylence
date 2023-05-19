#include "Console.h"
#include "ESPCrashMonitor.h"

ConsoleClass::ConsoleClass() {}

void ConsoleClass::onRebootCommand(void (*rebootHandler)()) {
	this->rebootHandler = rebootHandler;
}

void ConsoleClass::onScanNetworks(void (*scanHandler)()) {
	this->scanHandler = scanHandler;
}

void ConsoleClass::setHostname(String hostname) {
	_hostname = hostname;
}

void ConsoleClass::onHostnameChange(void (*hostnameChangeHandler)(const char* newHostname)) {
	this->hostnameChangeHandler = hostnameChangeHandler;
}

void ConsoleClass::onDhcpConfig(void (*dhcpHandler)()) {
	this->dhcpHandler = dhcpHandler;
}

void ConsoleClass::onStaticConfig(void (*staticHandler)(IPAddress ip, IPAddress sm, IPAddress gw, IPAddress dns)) {
	this->staticHandler = staticHandler;
}

void ConsoleClass::onReconnectCommand(void (*reconnectHandler)()) {
	this->reconnectHandler = reconnectHandler;
}

void ConsoleClass::onWifiConfigCommand(void (*wifiConfigHandler)(String ssid, String password)) {
	this->wifiConfigHandler = wifiConfigHandler;
}

void ConsoleClass::onResumeCommand(void (*resumeHandler)()) {
	this->resumeHandler = resumeHandler;
}

void ConsoleClass::onGetNetInfoCommand(void (*netInfoHandler)()) {
	this->netInfoHandler = netInfoHandler;
}

void ConsoleClass::onSaveConfigCommand(void (*saveConfigHandler)()) {
	this->saveConfigHandler = saveConfigHandler;
}

void ConsoleClass::onMqttConfigCommand(void (*mqttChangeHandler)(String newBroker, int newPort, String newUsername, String newPassword, String newConTopic, String newStatTopic)) {
	this->mqttChangeHandler = mqttChangeHandler;
}

void ConsoleClass::onConsoleInterrupt(void (*interruptHandler)()) {
	this->interruptHandler = interruptHandler;
}

void ConsoleClass::onFactoryRestore(void (*factoryRestoreHandler)()) {
	this->factoryRestoreHandler = factoryRestoreHandler;
}

void ConsoleClass::setMqttConfig(String broker, int port, String username, String password, String conTopic, String statTopic) {
	_mqttBroker = broker;
	_mqttPort = port;
	_mqttUsername = username;
	_mqttPassword = password;
	_mqttControlChannel = conTopic;
	_mqttStatusChannel = statTopic;
}

IPAddress ConsoleClass::getIPFromString(String value) {
	unsigned int ip[4];
	unsigned char buf[value.length()];
	value.getBytes(buf, value.length());
	const char* ipBuf = (const char*)buf;
	sscanf(ipBuf, "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]);
	return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

void ConsoleClass::waitForUserInput() {
	while (Serial.available() < 1) {
		ESPCrashMonitor.iAmAlive();
		delay(5);
	}
}

String ConsoleClass::getInputString(bool isPassword) {
	char c;
	String result = "";
	bool gotEndMarker = false;
	while (!gotEndMarker) {
		ESPCrashMonitor.iAmAlive();
		if (Serial.available() > 0) {
			c = Serial.read();
			if (c == '\n') {
				gotEndMarker = true;
				break;
			}

			Serial.print(isPassword ? '*' : c);
			result += c;
		}
	}

	return result;
}

void ConsoleClass::configureStaticIP() {
	Serial.println(F("Enter IP address: "));
	waitForUserInput();
	IPAddress ip = getIPFromString(getInputString());
	Serial.print(F("New IP: "));
	Serial.println(ip);

	Serial.println(F("Enter gateway: "));
	waitForUserInput();
	IPAddress gw = getIPFromString(getInputString());
	Serial.print(F("New gateway: "));
	Serial.println(gw);

	Serial.println(F("Enter subnet mask: "));
	waitForUserInput();
	IPAddress sm = getIPFromString(getInputString());
	Serial.print(F("New subnet mask: "));
	Serial.println(sm);

	Serial.println(F("Enter DNS server: "));
	waitForUserInput();
	IPAddress dns = getIPFromString(getInputString());
	Serial.print(F("New DNS server: "));
	Serial.println(dns);

	if (staticHandler != NULL) {
		staticHandler(ip, sm, gw, dns);
	}
}

void ConsoleClass::configureWiFiNetwork() {
	Serial.println(F("Enter new SSID: "));
	waitForUserInput();
	String ssid = getInputString();
	Serial.print(F("SSID = "));
	Serial.println(ssid);

	Serial.println(F("Enter new password: "));
	waitForUserInput();
	String password = getInputString();
	Serial.print(F("Password = "));
	Serial.println(password);

	if (wifiConfigHandler != NULL) {
		wifiConfigHandler(ssid, password);
	}
}

void ConsoleClass::configMQTT() {
	Serial.print(F("Current MQTT broker = "));
	Serial.println(_mqttBroker);
	Serial.println(F("Enter MQTT broker address: "));
	waitForUserInput();
	_mqttBroker = getInputString();
	Serial.println();
	Serial.print(F("New broker = "));
	Serial.println(_mqttBroker);

	Serial.print(F("Current port = "));
    Serial.println(_mqttPort);
    Serial.println(F("Enter MQTT broker port:"));
    waitForUserInput();
    String str = getInputString();
    _mqttPort = str.toInt();
    Serial.println();
    Serial.print(F("New port = "));
    Serial.println(_mqttPort);

	Serial.print(F("Current control topic = "));
    Serial.println(_mqttControlChannel);
    Serial.println(F("Enter MQTT control topic:"));
    waitForUserInput();
    _mqttControlChannel = getInputString();
    Serial.println();
    Serial.print(F("New control topic = "));
    Serial.println(_mqttControlChannel);

	Serial.print(F("Current status topic = "));
    Serial.println(_mqttStatusChannel);
    Serial.println(F("Enter MQTT status topic:"));
    waitForUserInput();
    _mqttStatusChannel = getInputString();
    Serial.println();
    Serial.print(F("New status topic = "));
    Serial.println(_mqttStatusChannel);

	Serial.print(F("Current username: "));
    Serial.println(_mqttUsername);
    Serial.println(F("Enter new username, or just press enter to clear:"));
    waitForUserInput();
    _mqttUsername = getInputString();
    Serial.print(F("New MQTT username = "));
    Serial.println(_mqttUsername);

	Serial.print(F("Current password: "));
    for (uint8_t i = 0; i < _mqttPassword.length(); i++) {
        Serial.print(F("*"));
    }

    Serial.println();
    Serial.print(F("Enter new password, or just press enter to clear"));
    waitForUserInput();
    _mqttPassword = this->getInputString(true);

	if (mqttChangeHandler != NULL) {
        mqttChangeHandler(
            _mqttBroker, _mqttPort,
            _mqttUsername,
            _mqttPassword,
            _mqttControlChannel,
            _mqttStatusChannel
        );
    }
}

void ConsoleClass::displayMenu() {
	Serial.println();
    Serial.println(F("=============================="));
    Serial.println(F("= Command menu:              ="));
    Serial.println(F("=                            ="));
    Serial.println(F("= r: Reboot                  ="));
    Serial.println(F("= c: Configure network       ="));
    Serial.println(F("= m: Configure MQTT settings ="));
    Serial.println(F("= s: Scan wireless networks  ="));
    Serial.println(F("= n: Connect to new network  ="));
    Serial.println(F("= w: Reconnect to WiFi       ="));
    Serial.println(F("= e: Resume normal operation ="));
    Serial.println(F("= g: Get network info        ="));
    Serial.println(F("= f: Save config changes     ="));
    Serial.println(F("= z: Restore default config  ="));
    Serial.println(F("=                            ="));
    Serial.println(F("=============================="));
    Serial.println();
    Serial.println(F("Enter command choice (r/c/m/s/n/w/e/g/f/z): "));
    waitForUserInput();
}

void ConsoleClass::enterCommandInterpreter() {
    displayMenu();
    checkCommand();
}

void ConsoleClass::checkCommand() {
    String str = "";
    char incomingByte = Serial.read();
    switch (incomingByte) {
        case 'r':
            // Reset the controller.
            if (rebootHandler != NULL) {
                rebootHandler();
            }
            break;
        case 's':
            // Scan for available networks.
            if (scanHandler != NULL) {
                scanHandler();
            }
            
            enterCommandInterpreter();
            break;
        case 'c':
            // Set hostname.
            Serial.print(F("Current host name: "));
            Serial.println(_hostname);
            Serial.println(F("Set new host name: "));
            waitForUserInput();

            str = getInputString();
            if (hostnameChangeHandler != NULL) {
                hostnameChangeHandler(str.c_str());
            }

            // Change network mode.
            _hostname = str;
            Serial.println(F("Choose network mode (d = DHCP, t = Static):"));
            waitForUserInput();
            checkCommand();
            break;
        case 'd':
            if (dhcpHandler != NULL) {
                dhcpHandler();
            }

            enterCommandInterpreter();
            break;
        case 't':
            // Switch to static IP mode. Request IP settings.
            configureStaticIP();
            enterCommandInterpreter();
            break;
        case 'w':
            if (reconnectHandler != NULL) {
                reconnectHandler();
            }
            break;
        case 'n':
            configureWiFiNetwork();
            enterCommandInterpreter();
            break;
        case 'e':
            if (resumeHandler != NULL) {
                resumeHandler();
            }
            break;
        case 'g':
            if (netInfoHandler != NULL) {
                netInfoHandler();
            }

            enterCommandInterpreter();
            break;
        case 'f':
            if (saveConfigHandler != NULL) {
                saveConfigHandler();
            }

            enterCommandInterpreter();
            break;
        case 'm':
            configMQTT();
            enterCommandInterpreter();
            break;
        case 'z':
            if (factoryRestoreHandler != NULL) {
                factoryRestoreHandler();
            }
            break;
        default:
            // Specified command is invalid.
            Serial.println(F("WARN: Unrecognized command."));
            enterCommandInterpreter();
            break;
    }
}

void ConsoleClass::checkInterrupt() {
    if (Serial.available() > 0 && Serial.read() == 'i') {
        if (interruptHandler != NULL) {
            interruptHandler();
        }
    }
}

ConsoleClass Console;