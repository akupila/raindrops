#include <Wire.h>

#define MIN_SWITCH_DELAY 100 // ms

bool states[12];
long previousReadings[12];

void setup()
{
	Serial.begin(57600);

	Wire.begin();

	for (int i = 0; i < 12; i++)
	{
		states[i] = true;
		pinMode(2 + i, INPUT);
		digitalWrite(2 + i, HIGH);
	}

	delay(3000); // Wait for master to start up

	Serial.println(F("Sending initial data:"));
	Wire.beginTransmission(4);
	Wire.write('i'); // initial
	for (byte i = 0; i < 12; i++)
	{
		bool state = digitalRead(i + 2);
		Serial.print(state);
		Wire.write(!state);
	}
	Wire.endTransmission();
	Serial.println();
}

void loop()
{
	for (uint8_t i = 0; i < 12; i++)
	{
		bool state = digitalRead(i + 2);
		if (state != states[i] && millis() > previousReadings[i] + MIN_SWITCH_DELAY)
		{
			previousReadings[i] = millis();
			states[i] = state;
			Wire.beginTransmission(4);
			Wire.write('u'); // update
			Wire.write(i + '0');
			Wire.write(!state);
			Wire.endTransmission();
			Serial.print(i);
			Serial.println(state);
		}
	}
}
