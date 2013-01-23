#include <SoftwareSerial.h>
#include <WiFlyHQ.h>
#include <ByteBuffer.h>
#include <ContinuousServo.h>
#include <EEPROM.h>
#include <Wire.h>

#include "pinout.h"
#include "config.h"
#include "network.h"

#define CALIBRATION_NONE		0 // not in calibration mode
#define CALIBRATION_READY		1 // waiting
#define CALIBRATION_STEP1		2 // moving down
#define CALIBRATION_RESETTING	3 // moving down

#define NUDGE_SIZE				1 // steps

WiFly wifly;
SoftwareSerial wifi(WIFI_RX, WIFI_TX);

uint32_t connectTime = 0;
char tmpBuffer[TMP_BUFFER_SIZE + 1];
ByteBuffer buffer;
byte rainValues[12];

byte calibrationServo;
byte calibrationMode;

int pulseWidths[24] = {
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793,
	564, 1793
};
ContinuousServo *servos[12]; // Array of pointers to ContinuousServo objects
int totalSteps[12];
int targetSteps[12];
int rotations[12];
int currentServoIndex;

void setup()
{
	Serial.begin(57600);
	Serial.print(F("Initializing I2C on address "));
	Serial.println(I2C_ADDRESS);

	// Be ready for I2C initial state data
	Wire.begin(I2C_ADDRESS);
	Wire.onReceive(i2cHandler);

	// Configure pins
	pinMode(DEBUG1, OUTPUT);
	pinMode(DEBUG2, OUTPUT);
	pinMode(WIFI_RX, INPUT);
	pinMode(WIFI_TX, OUTPUT);

	for (unsigned int i = 0; i < 12; i++)
	{
		pinMode(i, INPUT);
		servos[i] = new ContinuousServo(i + 2, pulseWidths[i * 2], pulseWidths[i * 2 + 1]);
	}

	buffer.init(BUFFER_SIZE);

	digitalWrite(DEBUG1, HIGH);
	digitalWrite(DEBUG2, HIGH);
	delay(100);
	digitalWrite(DEBUG1, LOW);
	digitalWrite(DEBUG2, LOW);

	Serial.println(F("Reading EEPROM"));
	for (unsigned int i = 0; i < 12; i++)
	{
		ContinuousServo *servo = servos[i];
		totalSteps[i]	= readIntFromEEPROM(i * 6 + 0);
		servo->storeSteps(readIntFromEEPROM(i * 6 + 2));
	}
	outputDebugInfo();

	Serial.println(F("Ready"));

	initWifi();
}

void initWifi()
{
	Serial.println(F("Initializing wifi"));
	wifi.begin(9600); // todo: try faster baud rate?

	if (!wifly.begin(&wifi, &Serial))
	{
		Serial.println(F("Failed to initialize wifi"));
		for (int i = 0; i < 10; i++)
		{
			digitalWrite(DEBUG1, HIGH);
			digitalWrite(DEBUG2, LOW);
			delay(200);
			digitalWrite(DEBUG1, HIGH);
			digitalWrite(DEBUG2, LOW);
		}
		
		rebootWifly();
		initWifi();
		return;
	}

	/* Join wifi network if not already associated */
	if (!wifly.isAssociated()) {
		delay(1000);
		/* Setup the WiFly to connect to a wifi network */
		Serial.print(F("Joining network '"));
		Serial.print(ssid);
		Serial.println(F("'"));

		wifly.enableDHCP();

		if (wifly.join(ssid, password, true, WIFLY_MODE_WPA, 50000)) {
			Serial.println(F("Joined wifi network"));
		} else {
			Serial.println(F("Failed to join wifi network"));
			for (int i = 0; i < 5; i++)
			{
				digitalWrite(DEBUG1, HIGH);
				digitalWrite(DEBUG2, HIGH);
				delay(200);
				digitalWrite(DEBUG1, LOW);
				digitalWrite(DEBUG2, LOW);
			}

			rebootWifly();
			initWifi();
			return;
		}
	} else {
		Serial.println(F("Already joined network"));
	}

	char buf[32];

	Serial.print("SSID: ");
	Serial.println(wifly.getSSID(buf, sizeof(buf)));

	Serial.println(F("Setting wifi device id"));
	wifly.setDeviceID("Weather Balloon");
	Serial.println(F("Setting wifi protocol: TCP"));
	wifly.setIpProtocol(WIFLY_PROTOCOL_TCP);

	Serial.println(F("Wifi initialized"));

	if (wifly.isConnected()) {
		Serial.println(F("Closing old connection"));
		wifly.close();
	}

	Serial.print(F("Pinging "));
	Serial.print(server);
	Serial.println(F(".."));
	boolean ping = wifly.ping(server);
	Serial.print(F("Ping: "));
	Serial.println(ping ? "ok!" : "failed");
	if (!ping)
	{
		Serial.println(F("Pinging google.."));
		ping = wifly.ping("google.com");
		Serial.print(F("Ping: "));
		Serial.println(ping ? "ok!" : "failed");
	}

	delay(1000);
}

void rebootWifly()
{
	Serial.println(F("Rebooting wifly"));
	wifly.reboot();

	digitalWrite(DEBUG1, HIGH);
	digitalWrite(DEBUG2, HIGH);
	delay(10000);
	digitalWrite(DEBUG1, LOW);
	digitalWrite(DEBUG2, LOW);
}

void loop()
{
	int available;

	if (!wifly.isConnected())
	{
		Serial.println("Connecting..");
		if (wifly.open(server, port))
		{
			Serial.println(F("Connected!"));
			connectTime = millis();
		}
		else
		{
			Serial.println("Connection failed. Retrying in 1sec");
			delay(1000);
		}
	}
	else
	{
		available = wifly.available();
		if (available < 0)
		{
			Serial.println(F("Disconnected"));
		}
		else if (available > 0)
		{
			ContinuousServo *firstServo = servos[0];

			bool readSuccess = wifly.gets(tmpBuffer, TMP_BUFFER_SIZE);

			if (readSuccess) {
				Serial.print(F("Got some data: "));
				Serial.println(tmpBuffer);
				buffer.putCharArray(tmpBuffer);
				processBuffer();
			}
		}
	}
}

void processBuffer()
{
	char c = buffer.get();

	byte index;
	byte angle;
	int servoIndex = 0;

	ContinuousServo *servo = servos[calibrationServo];

	switch (c)
	{
		case 'u': // example: u:24|24|7e|1f|1f|7e|1f|1f|3e|1a|1a|12 - values in base 16, 0 - 255
			buffer.get(); // skip :
			while ((c = buffer.get()) != 0)
			{
				if (c == '|') // values separated by |
				{
					servoIndex++;
				}
				else
				{
					char valArr[3];
					valArr[2] = 0;
					valArr[0] = c;
					valArr[1] = buffer.get();

					char* end;
					byte val = strtol(valArr, &end, 16);
					if (*end) val = 0;
					rainValues[servoIndex] = val;
				}
			}
			updateServos();
			break;
		case 'c':
			if (calibrationMode == CALIBRATION_NONE)
			{
				// Start calibration
				calibrationServo = buffer.get() - '0';
				calibrationMode = CALIBRATION_READY;
				Serial.print(F("Ready to calibrate "));
				Serial.println(calibrationServo);
				Serial.println(F("'d' to move down and define bottom"));
			}
			else
			{
				Serial.println(F("Already in calibration mode"));
			}
			break;
		case 'f': // freeze
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_STEP1)
			{
				servo->stop();
			} else Serial.println(F("Not in calibration mode"));
			break;
		case '<': // nudge up
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_STEP1)
			{
				Serial.print(F("Nudging "));
				Serial.println(-NUDGE_SIZE);
				servo->step(-NUDGE_SIZE);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case '>': // nudge down
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_STEP1)
			{
				Serial.print(F("Nudging "));
				Serial.println(NUDGE_SIZE);
				servo->step(NUDGE_SIZE);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 'd': // bottom
			if (calibrationMode == CALIBRATION_READY)
			{
				Serial.println(F("Moving down. 'f' to freeze, '< and '>' to nudge up/down"));
				servo->step(10000); // Start moving down
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 's':
			switch (calibrationMode)
			{
				case CALIBRATION_READY:
					// start moving up at full speed
					Serial.println(F("Finding top position.."));
					delay(1000);
					servo->storeSteps(0);
					servo->step(-25000);
					break;
				case CALIBRATION_STEP1:
					// should be all the way up now
					servo->stop();
					totalSteps[calibrationServo] = abs(servo->getSteps());

					Serial.print(F("Total: "));
					Serial.print(totalSteps[calibrationServo]);
					Serial.println(F(" steps."));

					Serial.println(F("Resetting to bottom.."));

					servo->stepTo(0, calibrationFinished);
					calibrationMode = CALIBRATION_RESETTING;
					break;
				default:
					return;
					break;
			}

			// proceed
			if (calibrationMode != CALIBRATION_RESETTING)
			{
				calibrationMode++;
			}
			break;
		case 'o':
			outputDebugInfo();
			break;
		default:
			Serial.print("Ignoring ");
			Serial.println(c);
			break;
	}	
}

// void i2cHandler(int howMany)
// {
//   Serial.println(howMany);
//   char c = Wire.read();
//   Serial.print(c);
//   Serial.print(F(" "));
//   byte a = Wire.read() - '0';
//   Serial.print(a);
//   Serial.println(Wire.read());
// }

void i2cHandler(int dataSize)
{
	char command = Wire.read();

	switch (command)
	{
		case 'i': // initial
			// Format: i110100101100 - i -> on full rotation
			break;
		case 'u': // update
			byte index = Wire.read() - '0'; // address is a char, 0-255
			bool active = Wire.read();

			Serial.print(index);
			Serial.print(F(" -> "));
			Serial.println(active);
			break;
	}
}

void calibrationFinished()
{
	Serial.print(F("Calibration for "));
	Serial.print(calibrationServo);
	Serial.println(F(" completed. Saving to EEPROM."));

	writeIntToEEPROM(calibrationServo * 4, totalSteps[calibrationServo]);
	writeIntToEEPROM(calibrationServo * 4 + 2, 0);

	calibrationMode = CALIBRATION_NONE;
	calibrationServo = -1;
}

void outputDebugInfo()
{
	Serial.println(F("Timing values (up / down):"));
	for (int i = 0; i < 12; i++)
	{
		ContinuousServo *servo = servos[i];
		Serial.print(F("  "));
		Serial.print(i);
		Serial.print(F(": "));
		Serial.print(totalSteps[i]);
		Serial.print(F(" @ "));
		Serial.println(servo->getSteps());
	}
}

void writeIntToEEPROM(unsigned int address, int value)
{
	byte lowByte = ((value >> 0) & 0xFF);
	byte highByte = ((value >> 8) & 0xFF);

	EEPROM.write(address, lowByte);
	EEPROM.write(address + 1, highByte);
}

int readIntFromEEPROM(unsigned int address)
{
	byte lowByte = EEPROM.read(address);
	byte highByte = EEPROM.read(address + 1);

	return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}

void updateServos()
{
	for (int i = 0; i < 12; i++)
	{
		if (totalSteps[i] > 0)
		{
			targetSteps[i] = (float)rainValues[i] / 255.0f * totalSteps[i];
			Serial.print(rainValues[i]);
			Serial.print(F(" / 255.0f = "));
			Serial.print((float)rainValues[i] / 255.0f);
			Serial.print(F(", "));
			Serial.print(targetSteps[i]);
			Serial.println(F(" steps"));
		}
	}

	currentServoIndex = -1;
	updateNextServo();
}

void updateNextServo()
{
	if (currentServoIndex >= 0)
	{
		// Previous servo completed
		ContinuousServo *servo = servos[currentServoIndex];
		writeIntToEEPROM(currentServoIndex * 4 + 2, servo->getSteps());
	}
	if (++currentServoIndex >= 12) return;
	ContinuousServo *servo = servos[currentServoIndex];
	if (targetSteps[currentServoIndex] != servo->getSteps())
	{
		servo->stepTo(targetSteps[currentServoIndex], updateNextServo);
	}
	else
	{
		updateNextServo();
	}
}