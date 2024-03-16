/*
 * Jenny Mini
 * An ESP32-based multilingual home assistant with a quirky personality.
 * 2024-03 v3.0 (beta)
 * by S. Shahriar <shadowshahriar@gmail.com>
 * Repository: https://github.com/ShadowShahriar/jenny-mini
 * Licensed under MIT
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <HTTPClient.h>

#define DEVICE_ID "JENNY-MINI"
#define STATION_SSID "ESP32 Jenny Mini"
#define STATION_PSK "admin1234"

#define WORKER "http://<app_name>.<user_name>.workers.dev/" // Your Cloudflare Worker URL here
#define BOT_TOKEN "<bot_token>"								// Your Telegram Bot Token here
#define ADMIN_ID "<user_id>"								// Your Telegram User ID here

int NETWORK_TIMEOUT = 10;			// in seconds
const unsigned long INTERVAL = 200; // in milliseconds
unsigned long LASTINTERVAL = 0;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long START_TIME = 0;
unsigned long WIFI_TIME = 0;

// === pinouts ===
#define COUNT 4						  // number of switches
int LED_WIFI = 25;					  // wifi LED
int LED_STATUS = 26;				  // status LED
int LED_PIN[COUNT] = {4, 21, 22, 23}; // array of Relay modules
int SW_PIN[COUNT] = {16, 17, 18, 19}; // array of LEDs for Relay modules
int SW_VAL[COUNT] = {0, 0, 0, 0};	  // array of switch states

// === states ===
#define BIN_ON 1
#define BIN_OFF 0
#define ON 0
#define OFF 1
#define TOGGLE 2

bool isSILENT = false;
bool willSLEEP = false;
bool willREBOOT = false;
bool willRESETWIFI = false;

TaskHandle_t WiFiIndicator;

const int STARTUP_DELAY = 1000;
const int NO_WIFI_DELAY = 500;
const int CONNECT_DELAY = 500;
const int GETTIME_DELAY = 100;

// ===========================
// === indicator functions ===
// ===========================
void updateWiFiLED(int value)
{
	if (digitalRead(LED_WIFI) != value)
	{
		digitalWrite(LED_WIFI, value);
	}
}

void toggleWiFiLED()
{
	int state = digitalRead(LED_WIFI);
	digitalWrite(LED_WIFI, !state);
}

void updateStatusLED(int value)
{
	if (digitalRead(LED_STATUS) != value)
	{
		digitalWrite(LED_STATUS, value);
	}
}

void toggleStatusLED()
{
	int state = digitalRead(LED_STATUS);
	digitalWrite(LED_STATUS, !state);
}

void checkWiFi()
{
	if (WiFi.status() != WL_CONNECTED)
	{
		Serial.println("WiFi Status: ❌ Disconnected");
	}
	else
	{
		Serial.println("WiFi Status: 🆗 Connected");
	}
}

void indicateWiFi(void *pvParameters)
{
	for (;;)
	{
		if (WiFi.status() != WL_CONNECTED)
		{
			toggleWiFiLED();
			delay(NO_WIFI_DELAY);
		}
		else
		{
			updateWiFiLED(BIN_OFF);
			delay(100);
		}
	}
}

void portalCallback(WiFiManager *myWiFiManager)
{
	Serial.println("📶 Entered in Config Mode");
}

// =======================
// === setup functions ===
// =======================
void setPinMode()
{
	pinMode(LED_STATUS, OUTPUT);
	pinMode(LED_WIFI, OUTPUT);
	for (int i = 0; i < COUNT; i++)
	{
		pinMode(LED_PIN[i], OUTPUT);
		pinMode(SW_PIN[i], OUTPUT);
	}
}

void setPinState()
{
	digitalWrite(LED_STATUS, LOW);
	digitalWrite(LED_WIFI, LOW);
	for (int i = 0; i < COUNT; i++)
	{
		digitalWrite(LED_PIN[i], LOW);
		digitalWrite(SW_PIN[i], HIGH);
	}
}

void setupWiFi()
{
	updateWiFiLED(BIN_ON);
	WiFi.mode(WIFI_STA);
	WiFi.setHostname(DEVICE_ID);
	WiFiManager wm;
	wm.setConnectTimeout(NETWORK_TIMEOUT);
	wm.setAPCallback(portalCallback);
	wm.autoConnect(STATION_SSID, STATION_PSK);
	secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

	Serial.print("\n⏳ Connecting");
	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print(".");
		delay(CONNECT_DELAY);
	}
	Serial.println((String) "\n✅ Connected, IP Address: " + WiFi.localIP());
	updateWiFiLED(BIN_OFF);
}

void setupTime()
{
	updateStatusLED(BIN_ON);
	Serial.print("⏳ Obtaining Network Time");
	configTime(0, 0, "pool.ntp.org");
	time_t now = time(nullptr);
	while (now < 24 * 3600)
	{
		Serial.print(".");
		toggleStatusLED();
		delay(GETTIME_DELAY);
		now = time(nullptr);
	}
	Serial.println((String) "\n✅ Current Time: " + now);
	updateStatusLED(BIN_OFF);
}

void setupBot()
{
	unsigned long END_TIME = WIFI_TIME - START_TIME;
	pingWorker("/sysinit", 23473, END_TIME, "0");
}

// ========================
// === switch functions ===
// ========================
void updatePinState(int index, int value)
{
	SW_VAL[index] = value;
	digitalWrite(SW_PIN[index], !value);
	digitalWrite(LED_PIN[index], value);
	Serial.println((String) "== Switch " + (index + 1) + " is " + (value == 1 ? "🟩 ON" : "🟥 OFF"));
}

void togglePinState(int index)
{
	int state = digitalRead(LED_PIN[index]);
	updatePinState(index, !state);
}

unsigned long long int getSwitchValue(int sw, int state, int extension)
{
	int offset = extension * 12;
	return pow(2, sw + offset + state * 4);
}

void setSwitchValue(int index, int value)
{
	if (value == ON)
	{
		updatePinState(index, 1);
	}
	else if (value == OFF)
	{
		updatePinState(index, 0);
	}
	else if (value == TOGGLE)
	{
		togglePinState(index);
	}
}

void setAllSwitches(int value)
{
	for (int i = 0; i < COUNT; ++i)
	{
		setSwitchValue(i, value);
	}
}

// ======================
// === main functions ===
// ======================
void respond(int count)
{
	for (int i = 0; i < count; i++)
	{
		telegramMessage &message = bot.messages[i];
		if (message.chat_id == ADMIN_ID)
		{
			Serial.print("📨 New message: ");
			Serial.println(message.text);
			pingWorker(message.text, message.message_id, 0, message.date);
		}
	}
}

void pingWorker(String text, int id, unsigned long timediff, String datestr)
{
	unsigned long long int payload = 0;
	if (WiFi.status() == WL_CONNECTED)
	{
		updateStatusLED(BIN_ON);
		int32_t RSSI = WiFi.RSSI();

		WiFiClient client;
		HTTPClient http;
		http.begin(client, WORKER);
		http.addHeader("Content-Type", "application/json");

		unsigned long END_TIME = millis();
		unsigned long DIFF_TIME;
		if (timediff == 0)
		{
			DIFF_TIME = END_TIME - START_TIME;
		}
		else
		{
			DIFF_TIME = timediff;
		}

		unsigned long int switchConfig = (SW_VAL[0] * 1) | (SW_VAL[1] * 2) | (SW_VAL[2] * 4) | (SW_VAL[3] * 8);
		String data = "{\"text\":\"" + text + "\",\"id\":\"" + id + "\",\"date\":\"" + datestr + "\",\"uptime\":" + millis() + ",\"sw\":" + switchConfig + ",\"duration\":" + DIFF_TIME + ",\"silent\":" + isSILENT + ",\"signal\":" + RSSI + "}";
		Serial.println("⏳ Sending...");
		int ResposeCode = http.POST(data);

		if (ResposeCode == 200)
		{
			String text = http.getString();
			payload = text.toInt();
			Serial.print("✅ Response: OK, String: \"");
			Serial.print(text);
			Serial.print("\", Code: ");
			Serial.println(payload);
			executeCommand(text, payload);
		}
		else
		{
			Serial.print("⛔ Response: Error, Code: ");
			Serial.println(ResposeCode);
		}

		http.end();
		START_TIME = END_TIME;
		updateStatusLED(BIN_OFF);
	}
	else
	{
	}
}

void executeCommand(String text, unsigned long long int cmd)
{
	if (cmd > 0)
	{
		for (int sw = 0; sw < 4; sw++)
		{
			for (int state = 0; state < 3; state++)
			{
				if (cmd & getSwitchValue(sw, state, 0))
				{
					setSwitchValue(sw, state);
				}
			}
		}
	}
	else
	{
		if (text == "ON")
		{
			setAllSwitches(ON);
		}
		else if (text == "OFF")
		{
			setAllSwitches(OFF);
		}
		else if (text == "TOGGLE")
		{
			setAllSwitches(TOGGLE);
		}
	}
}

// ===========================
// === mandatory functions ===
// ===========================
void setup()
{
	setPinMode();
	setPinState();
	Serial.begin(115200);
	delay(STARTUP_DELAY);

	setupWiFi();
	setupTime();
	Serial.println("✅ Ready!");

	xTaskCreatePinnedToCore(
		indicateWiFi,	 /* function to implement the task */
		"WiFiIndicator", /* task name */
		10000,			 /* stack size */
		NULL,			 /* input parameter */
		0,				 /* priority */
		&WiFiIndicator,	 /* handle name */
		0);				 /* core */

	setupBot();
	Serial.println("✅ Greeting sent!");
}

void loop()
{
	if ((millis() - LASTINTERVAL) > INTERVAL)
	{
		checkWiFi();
		int pending = bot.getUpdates(bot.last_message_received + 1);
		while (pending)
		{
			respond(pending);
			pending = bot.getUpdates(bot.last_message_received + 1);
		}
		LASTINTERVAL = millis();
	}
}