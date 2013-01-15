#include <SoftwareSerial.h>
#include <WiFlyHQ.h>
#include <ByteBuffer.h>

#include "pinout.h"
#include "config.h"
#include "network.h"

WiFly wifly;
SoftwareSerial wifi(WIFI_RX, WIFI_TX);

uint32_t connectTime = 0;
char tmpBuffer[TMP_BUFFER_SIZE + 1];
ByteBuffer buffer;
byte rainValues[12];

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
		digitalWrite(DEBUG1, HIGH);
		digitalWrite(DEBUG2, HIGH);
		delay(5000);
		digitalWrite(DEBUG1, LOW);
		digitalWrite(DEBUG2, LOW);

		Serial.println(F("Retrying wifi.."));
		initWifi();
		return;
	}

#ifdef HELLO_SAVANTS
	Serial.println(F("Setting static ip & gateway"));
	wifly.disableDHCP();
	wifly.setIP("10.0.0.235");
	wifly.setGateway("10.0.0.1");
#else
	wifly.enableDHCP();
#endif

#ifndef HELLO_SAVANTS
	/* Join wifi network if not already associated */
	if (!wifly.isAssociated()) {
#endif
		/* Setup the WiFly to connect to a wifi network */
		Serial.print(F("Joining network '"));
		Serial.print(ssid);
		Serial.println(F("'"));

		wifly.setSSID(ssid);
		wifly.setPassphrase(password);

		if (wifly.join()) {
			Serial.println(F("Joined wifi network"));
		} else {
			Serial.println(F("Failed to join wifi network"));
			for (int i = 0; i < 5; i++)
			{
				digitalWrite(DEBUG2, HIGH);
				digitalWrite(DEBUG2, HIGH);
				delay(200);
				digitalWrite(DEBUG1, LOW);
				digitalWrite(DEBUG2, LOW);
			}

			Serial.println(F("Rebooting wifly"));
			wifly.reboot();

			delay(5000);

			initWifi();
			return;
		}
#ifndef HELLO_SAVANTS
	} else {
		Serial.println(F("Already joined network"));
	}
#endif

	char buf[32];

	Serial.print("SSID: ");
	Serial.println(wifly.getSSID(buf, sizeof(buf)));
	Serial.print("MAC: ");
	Serial.println(wifly.getMAC(buf, sizeof(buf)));
	Serial.print("IP: ");
	Serial.println(wifly.getIP(buf, sizeof(buf)));
	Serial.print("Netmask: ");
	Serial.println(wifly.getNetmask(buf, sizeof(buf)));
	Serial.print("Gateway: ");
	Serial.println(wifly.getGateway(buf, sizeof(buf)));

	Serial.println(F("Setting wifi device id"));
	wifly.setDeviceID("Weather Balloon");
	Serial.println(F("Setting wifi protocol: TCP"));
	wifly.setIpProtocol(WIFLY_PROTOCOL_TCP);

	Serial.print(F("Pinging "));
	Serial.print(server);
	Serial.println(F(".."));
	boolean ping = wifly.ping(server);
	Serial.print(F("Ping: "));
	Serial.println(ping ? "ok!" : "failed");

	Serial.println(F("Wifi initialized"));

	if (wifly.isConnected()) {
		Serial.println(F("Closing old connection"));
		wifly.close();
	}
}

void loop()
{
	int available;

	if (!wifly.isConnected()) {
		Serial.println("Connecting..");
		if (wifly.open(server, port)) {
			Serial.println(F("Connected!"));
			connectTime = millis();
		} else {
			Serial.println("Connection failed. Retrying in 1sec");
			delay(1000);
		}
	} else {
		available = wifly.available();
		if (available < 0) {
			Serial.println(F("Disconnected"));
		} else if (available > 0) {
			bool readSuccess = wifly.gets(tmpBuffer, TMP_BUFFER_SIZE);

			if (readSuccess) {
				Serial.println(F("Got some data.."));
				Serial.println(tmpBuffer);
				buffer.putCharArray(tmpBuffer);
				processBuffer();
			}
		}

		/* Send data from the serial monitor to the TCP server */
		if (Serial.available()) {
			wifly.write(Serial.read());
		}
	}
}

void processBuffer()
{
	char c = buffer.get();
	switch (c)
	{
		case 'u': // example: u:24|24|7e|1f|1f|7e|1f|1f|3e|1a|1a|12 - values in base 16, 0 - 255
			buffer.get(); // skip :
			while ((c = buffer.get()) != 0)
			{
				int servoIndex = 0;
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

					Serial.println(rainValues[servoIndex]);
				}
			}
			updateServos();
			break;
		default:

			break;
	}	
}

void updateServos()
{

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