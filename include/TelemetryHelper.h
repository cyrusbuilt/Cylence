#ifndef _TELEMETRYHELPER_H
#define _TELEMETRYHELPER_H

#include <Arduino.h>

enum class SystemState: uint8_t {
	BOOTING = 0,
	NORMAL = 1,
	UPDATING = 2,
	DISABLED = 3
};

enum class ControlCommand: uint8_t {
	DISABLE = 0,
	ENABLE = 1,
	REBOOT = 2,
	REQUEST_STATUS = 3
};

class TelemetryHelper
{
public:
	static String getMqttStateDesc(int state);
};

#endif