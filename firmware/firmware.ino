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
#define CALIBRATION_FINDING_TOP		2 // moving down
#define CALIBRATION_RESETTING	3 // moving down

#define NUDGE_SIZE				1 // steps

WiFly wifly;
SoftwareSerial wifi(WIFI_RX, WIFI_TX);

uint32_t connectTime = 0;
char tmpBuffer[TMP_BUFFER_SIZE + 1];
ByteBuffer buffer;
byte rainValues[12];

volatile byte calibrationServo;
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
int totalRotations[12]; // + steps
int totalSteps[12];

int currentRotations[12]; // current steps in servo
int targetRotations[12]; // + steps
int targetSteps[12];

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
		// format (all ints)
		// [rotations][steps][current rotations][current steps]
		ContinuousServo *servo = servos[i];
		totalRotations[i]	= 	readIntFromEEPROM(i * 8 + 0);
		totalSteps[i]		= 	readIntFromEEPROM(i * 8 + 2);
		currentRotations[i]	= 	readIntFromEEPROM(i * 8 + 4);
		servo->storeSteps(		readIntFromEEPROM(i * 8 + 6));
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
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_FINDING_TOP)
			{
				servo->stop();
			} else Serial.println(F("Not in calibration mode"));
			break;
		case '<': // nudge up
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_FINDING_TOP)
			{
				Serial.print(F("Nudging "));
				Serial.println(-NUDGE_SIZE);
				servo->step(-NUDGE_SIZE);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case '>': // nudge down
			if (calibrationMode == CALIBRATION_READY || calibrationMode == CALIBRATION_FINDING_TOP)
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
				servo->step(-10000); // Start moving down
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 's':
			int invertedSteps;
			switch (calibrationMode)
			{
				case CALIBRATION_READY:
					// start moving up at full speed
					totalRotations[calibrationServo] = 0;
					currentRotations[calibrationServo] = 0;
					Serial.println(F("Finding top position.."));
					delay(1000);
					servo->storeSteps(0);
					servo->step(10000);
					break;
				case CALIBRATION_FINDING_TOP:
					// should be all the way up now
					servo->stop();
					totalSteps[calibrationServo] = servo->getSteps();
					currentRotations[index] = totalRotations[index];

					Serial.print(F("Total: "));
					Serial.print(totalRotations[calibrationServo]);
					Serial.print(F(" rotations, "));
					Serial.print(totalSteps[calibrationServo]);
					Serial.println(F(" steps."));

					targetRotations[currentServoIndex] = 0;

					invertedSteps = -totalSteps[currentServoIndex];

					targetSteps[currentServoIndex] = invertedSteps;

					Serial.println(F("Resetting to bottom.."));
					Serial.print(totalRotations[calibrationServo]);
					Serial.print(F(" + "));
					Serial.print(totalSteps[calibrationServo]);
					Serial.print(F(" -> "));
					Serial.print(targetRotations[calibrationServo]);
					Serial.print(F(" + "));
					Serial.println(invertedSteps);

					servo->step(-10000);

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

void i2cHandler(int dataSize)
{
	char command = Wire.read();

	switch (command)
	{
		case 'i': // initial
			// Format: i110100101100 - i -> on full rotation
			Serial.println(F("Got init I2C info from sensor"));
			break;
		case 'u': // update
			byte index = Wire.read() - '0'; // address is a char, 0-255
			bool active = Wire.read();

			if (active)
			{
				ContinuousServo *servo = servos[index];
				servo->storeSteps(0); // relative to 0

				currentRotations[index] += servo->getDirection();

				if (calibrationMode == CALIBRATION_FINDING_TOP)
				{
					totalRotations[index]++; // always positive
					Serial.print(index);
					Serial.print(F(" : "));
					Serial.println(totalRotations[index]);
				}
				else
				{
					Serial.print(currentRotations[index]);
					Serial.print(F(" / "));
					Serial.println(targetRotations[index]);

					if (currentRotations[index] == targetRotations[index])
					{
						servo->stop();
						// Serial.println(F("Reached target rotation. Adjusting final steps.."));
						void (*callback)() = (calibrationMode == CALIBRATION_RESETTING ? calibrationFinished : updateNextServo);

						if (targetSteps[index] != 0)
						{
							servo->step(targetSteps[index], callback);
						}
						else
						{
							callback();
						}
					}
				}
			}
			else
			{
				// Serial.print(index);
				// Serial.println(F(" no longer centered"));
			}

			break;
	}
}

void calibrationFinished()
{
	digitalWrite(DEBUG1, HIGH);
	digitalWrite(DEBUG2, HIGH);
	// Serial.print(F("Calibration for "));
	// Serial.print(calibrationServo);
	// Serial.println(F(" completed."));

	// Serial.print(F("Rotations: "));
	// Serial.print(totalRotations[calibrationServo]);
	// Serial.print(F(" + "));
	// Serial.print(totalSteps[calibrationServo]);
	// Serial.println(F(" steps. Saving to EEPROM."));

	writeIntToEEPROM(calibrationServo * 8 + 0, totalRotations[calibrationServo]);
	writeIntToEEPROM(calibrationServo * 8 + 2, totalSteps[calibrationServo]);
	writeIntToEEPROM(calibrationServo * 8 + 4, currentRotations[calibrationServo]);
	writeIntToEEPROM(calibrationServo * 8 + 6, 0);

	calibrationMode = CALIBRATION_NONE;
	calibrationServo = -1;

	digitalWrite(DEBUG2, LOW);
}

void outputDebugInfo()
{
	Serial.println(F("Timing values (rotations + steps):"));
	for (int i = 0; i < 12; i++)
	{
		ContinuousServo *servo = servos[i];
		Serial.print(F("  "));
		Serial.print(i);
		Serial.print(F(": "));
		Serial.print(totalRotations[i]);
		Serial.print(F(" + "));
		Serial.print(totalSteps[i]);
		Serial.print(F(" @ "));
		Serial.print(currentRotations[i]);
		Serial.print(F(" + "));
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
			float p = (float)rainValues[i] / 255.0f;
			targetRotations[i] = p * totalRotations[i];
			targetSteps[i] = p * totalSteps[i];
			Serial.print(rainValues[i]);
			Serial.print(F(" / 255.0f = "));
			Serial.print((float)rainValues[i] / 255.0f);
			Serial.print(F(" -> "));
			Serial.print(totalRotations[i]);
			Serial.print(F(" + "));
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
		writeIntToEEPROM(calibrationServo * 8 + 4, currentRotations[calibrationServo]);
		writeIntToEEPROM(calibrationServo * 8 + 6, servo->getSteps());
	}
	if (++currentServoIndex >= 12) return;
	ContinuousServo *servo = servos[currentServoIndex];
	if (currentRotations[currentServoIndex] != targetRotations[currentServoIndex])
	{
		servo->step(targetRotations[currentServoIndex] > currentRotations[currentServoIndex] ? 10000 : -10000);
	}
	else if (servo->getSteps() != targetSteps[currentServoIndex])
	{
		servo->stepTo(targetSteps[currentServoIndex], updateNextServo);
	}
	else
	{
		updateNextServo();
	}
}