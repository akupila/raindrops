#include <SoftwareSerial.h>
#include <WiFlyHQ.h>
#include <ByteBuffer.h>
#include <Servo.h>

#include "pinout.h"
#include "config.h"
#include "network.h"

#define CALIBRATION_NONE 0 // waiting
#define CALIBRATION_READY 1 // waiting
#define CALIBRATION_STEP1 2 // moving down
#define CALIBRATION_STEP2 3 // moving up

WiFly wifly;
SoftwareSerial wifi(WIFI_RX, WIFI_TX);

uint32_t connectTime = 0;
char tmpBuffer[TMP_BUFFER_SIZE + 1];
ByteBuffer buffer;
byte rainValues[12];
Servo servo;
int activeServo = -1;

byte calibrationServo;
byte calibrationMode;
long calibrationStart = 0;

long downDurations[12] = {0};
long upDurations[12] = {0};
float positions[12] = {0}; // 0: up, 1: down
float targetPositions[12] = {0};
long servoMovementCompleted;

void setup()
{
	// Configure pins
	pinMode(DEBUG1, OUTPUT);
	pinMode(DEBUG2, OUTPUT);
	pinMode(WIFI_RX, INPUT);
	pinMode(WIFI_TX, OUTPUT);

	for (unsigned int servo = 0; servo < 12; servo++)
	{
		pinMode(servo, INPUT);
	}

	Serial.begin(57600);
	buffer.init(BUFFER_SIZE);

	digitalWrite(DEBUG1, HIGH);
	digitalWrite(DEBUG2, HIGH);
	delay(100);
	digitalWrite(DEBUG1, LOW);
	digitalWrite(DEBUG2, LOW);

	Serial.println(F("Ready"));

	initWifi();
}

void initWifi()
{
	Serial.println(F("Initializing wifi"));
	wifi.begin(9600);

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
			bool readSuccess = wifly.gets(tmpBuffer, TMP_BUFFER_SIZE);

			if (readSuccess) {
				Serial.println(F("Got some data.."));
				Serial.println(tmpBuffer);
				buffer.putCharArray(tmpBuffer);
				processBuffer();
			}
		}

		if (Serial.available())
		{
			wifly.write(Serial.read());
		}
	}

	// If not calibrating and servo is not moving, check if servos should be moved
	if (calibrationMode == CALIBRATION_NONE)
	{
		if (activeServo < 0)
		{
			// Check if servos should be moved
			for (int i = 0; i < 12; i++)
			{
				if (targetPositions[i] != positions[i])
				{
					Serial.print(F("Moving servo "));
					Serial.println(i);
					int multiplier = 0;
					if (targetPositions[i] < positions[i])
					{
						// Move up
						setServo(i, 90 - SPEED);
						multiplier = upDurations[i];
					}
					else
					{
						setServo(i, 90 + SPEED);
						multiplier = downDurations[i];
					}
					int movementDuration = abs(targetPositions[i] - positions[i]) * multiplier * 0.928571429f;
					servoMovementCompleted = millis() + movementDuration;
				}
			}
		}
		else
		{
			// Servo is moving
			if (millis() >= servoMovementCompleted)
			{
				int _activeServo = activeServo;
				positions[activeServo] = targetPositions[activeServo];
				setServo(activeServo, 90);
				Serial.print(F("Servo "));
				Serial.print(_activeServo);
				Serial.println(F(" completed."));
			}
		}
	}
	else
	{
		digitalWrite(DEBUG1, !digitalRead(DEBUG1));
		delay(50);
	}
}

// 0-180, 90 = stop
void setServo(byte index, byte angle)
{
	if (activeServo > 0)
	{
		servo.write(90);
		delay(10);
	}
	servo.detach();

	if (activeServo >= 0) pinMode(activeServo, INPUT);

	if (angle != 90)
	{
		servo.attach(2 + index); // starts from pin 2
		servo.write(angle);
		activeServo = index;
	}
	else
	{
		activeServo = -1;
	}
}

void processBuffer()
{
	char c = buffer.get();

	byte index;
	byte angle;
	int servoIndex = 0;

	switch (c)
	{
		case 'u': // example: u:24|24|7e|1f|1f|7e|1f|1f|3e|1a|1a|12 - values in base 16, 0 - 255
			buffer.get(); // skip :
			while ((c = buffer.get()) != 0)
			{
				if (c == '|') // values separated by |
				{
					Serial.println(F("Increase servo index.."));
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

					Serial.print(servoIndex);
					Serial.print(F(": "));
					Serial.println(rainValues[servoIndex]);
				}
			}
			updateServos();
			break;
		case 'c': // example: c00 - c0c (c index 0-c, e.g., c36 stops servo 3)
			if (calibrationMode == CALIBRATION_NONE)
			{
				// Start calibration
				calibrationServo = buffer.get() - '0';
				calibrationMode = CALIBRATION_READY;
				Serial.print(F("Ready to calibrate "));
				Serial.println(calibrationServo);
			}
			else
			{
				Serial.println(F("Already in calibration mode"));
			}
			break;
		case 't': // top
			if (calibrationMode == CALIBRATION_READY)
			{
				setServo(calibrationServo, 90 - SPEED);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 'b': // bottom
			if (calibrationMode == CALIBRATION_READY)
			{
				setServo(calibrationServo, 90 + SPEED);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 'f': // freeze
			if (calibrationMode == CALIBRATION_READY)
			{
				setServo(calibrationServo, 90);
			} else Serial.println(F("Not in calibration mode"));
			break;
		case 's':
			switch (calibrationMode)
			{
				case CALIBRATION_READY:
					// start moving down at full speed
					delay(1000);
					setServo(calibrationServo, 90 + SPEED);
					break;
				case CALIBRATION_STEP1:
					// should be all the way down now
					downDurations[calibrationServo] = millis() - calibrationStart;

					setServo(calibrationServo, 90);
					delay(1000);
					setServo(calibrationServo, 90 - SPEED);
					break;
				case CALIBRATION_STEP2:
					// should be all the way up now
					upDurations[calibrationServo] = millis() - calibrationStart;
					setServo(calibrationServo, 90);

					Serial.print(F("Calibration for servo "));
					Serial.print(calibrationServo);
					Serial.print(" completed. Down: ");
					Serial.print(downDurations[calibrationServo]);
					Serial.print(", up: ");
					Serial.println(upDurations[calibrationServo]);
					break;
				default:
					return;
					break;
			}
			calibrationStart = millis();

			// proceed
			if (++calibrationMode > CALIBRATION_STEP2)
			{
				calibrationMode = CALIBRATION_NONE; // done!
				calibrationServo = 0;
			}
			break;
		default:
			Serial.print("Ignoring ");
			Serial.println(c);
			break;
	}	
}

void updateServos()
{
	for (int i = 0; i < 12; i++)
	{
		if (upDurations[i] > 0 && downDurations[i] > 0)
		{
			targetPositions[i] = (float)rainValues[i] / 255.0f;
			Serial.print(rainValues[i]);
			Serial.print(F(" / 255.0f = "));
			Serial.println((float)rainValues[i] / 255.0f);
		}
	}
	Serial.println(F("Target positions updated: "));
	for (int i = 0; i < 12; i++)
	{
		Serial.print("  ");
		Serial.println(targetPositions[i], DEC);
	}
}

void enterTerminalMode()
{
	Serial.println(F("Entered terminal mode. Reboot to exit."));
    while (1) {
		if (wifly.available() > 0) {
			Serial.write(wifly.read());
		}


		if (Serial.available() > 0) {
			wifly.write(Serial.read());
		}
    }
}