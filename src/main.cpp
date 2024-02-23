#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <ElegantOTA.h>
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

#define SM_DIR 27
#define SM_STEP 25
#define SM_nEN 26
#define SW 16
#define LED_CONNECT 22
#define INIT_RESET_BTN 13
#define MOVE_STOP 0
#define MOVE_UP 1
#define MOVE_DOWN 2
#define MOVE_CALIBRATE 3

const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";
const char *PARAM_INPUT_3 = "ip";
const char *PARAM_INPUT_4 = "gateway";
const char *PARAM_INPUT_5 = "dns";
const char *PARAM_INPUT_6 = "subnet";

const char *settingsPath = "/settings.json";
const char *timersPath = "/timers.json";

const char *ssid;
const char *pass;
const char *ip;
const char *gateway;
const char *dns;
const char *subnet;

IPAddress localIP;
IPAddress localGateway;
IPAddress localDNS;
IPAddress localSubnet;

struct tm timeinfo;
time_t localSec;
time_t localMin;
time_t localHour;
time_t netSec;
time_t netMin;
time_t netHour;

struct SunrieseSunsetTime
{
	time_t sunriseSec;
	time_t sunriseMin;
	time_t sunriseHour;
	time_t sunsetSec;
	time_t sunsetMin;
	time_t sunsetHour;
	String strSunrise12;
	String strSunrise24;
	String strSunset12;
	String strSunset24;
	bool dataReady;
};

SunrieseSunsetTime sstime;

DynamicJsonDocument json(1024);
DynamicJsonDocument timersDoc(1024);
JsonArray timersArray = timersDoc.createNestedArray("timers");

unsigned long currMillis;
unsigned long prevMillis;

char ws_data[2048];
size_t ws_len;

int targetPos = 0;	// Tagret motor position in steps
int shade = 0;		// Tagret motor position in percent
int currentPos = 0; // Current motor position
int shadeLenght = 0;
int nTimers = 0;
int tz = 3;

int calibrateCnt = 0;
String calibrateStatus = "false";
bool sw_flag = false;
bool init_flag = false;
bool targetFlag = false;
bool timeSyncFlag = false;
int moveState = MOVE_STOP;
bool clientRequest = false;
bool onSunset = false;
bool onSunrise = false;

int i = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long ota_progress_millis = 0;

void onOTAStart()
{
	// Log when OTA has started
	Serial.println("OTA update started!");
}

void onOTAProgress(size_t current, size_t final)
{

	// Log every 1 second
	if (millis() - ota_progress_millis > 1000)
	{
		ota_progress_millis = millis();
		Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
	}
}

void onOTAEnd(bool success)
{
	// Log when OTA has finished
	if (success)
	{
		Serial.println("OTA update finished successfully!");
	}
	else
	{
		Serial.println("There was an error during OTA update!");
	}
	// <Add your own code here>
}

// Init SPIFFS function
void initSPIFFS()
{
	Serial.print("Mount SPIFFS... ");
	if (!SPIFFS.begin(true))
		Serial.println("- failed");
	else
		Serial.println("- succeeded");
}

// Read file from SPIFFS function
DynamicJsonDocument readJsonFile(fs::FS &fs, const char *path)
{
	Serial.printf("Read json file: %s", path);
	DynamicJsonDocument doc(1024);
	File file = fs.open(path);
	if (deserializeJson(doc, file) == DeserializationError::Ok)
	{
		Serial.println("- succeeded");
	}
	else
		Serial.println("- failed");

	return doc;
}

// Write file to SPIFFS function
void writeJsonFile(fs::FS &fs, const char *path, DynamicJsonDocument json)
{
	Serial.printf("Save json file: %s", path);

	File file = fs.open(path, FILE_WRITE);
	if (!file)
	{
		Serial.println("- error opening file for saving");
		return;
	}
	if (serializeJson(json, file) != 0)
		Serial.println("- succeeded");
	else
		Serial.println("- failed");

	file.close();
}

// Handle web socket message WS_EVT_DATA
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
	AwsFrameInfo *info = (AwsFrameInfo *)arg;
	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
	{
		data[len] = 0;
		String msg = (char *)data;
		Serial.println("WebSocket message received: " + msg);

		// Deserialize JSON object from string
		DynamicJsonDocument doc(128);
		if (deserializeJson(doc, msg) == DeserializationError::Ok)
		{
			if (doc["cmd"] == "open")
			{
				Serial.print("Request from client to open...\n");
				if (calibrateStatus == "true")
				{
					// Set zero position
					shade = 0;
					json["shade"] = shade;

					// Serialize JSON object to string and send to client
					ws_len = serializeJson(json, ws_data);
					ws.textAll(ws_data, ws_len);
				}
			}

			if (doc["cmd"] == "close")
			{
				Serial.print("Request from client to close...\n");
				if (calibrateStatus == "true")
				{
					// Set max position
					shade = 100;
					json["shade"] = shade;

					// Serialize JSON object to string and send to client
					ws_len = serializeJson(json, ws_data);
					ws.textAll(ws_data, ws_len);
				}
			}

			if (doc["cmd"] == "calibrate")
			{
				Serial.print("Request from client to calibrate shade lenght...\n");
				moveState = MOVE_CALIBRATE;
				calibrateStatus = "progress";
				json["calibrateStatus"] = calibrateStatus;
				calibrateCnt = 0;

				// Serialize JSON object to string and send to client
				ws_len = serializeJson(json, ws_data);
				ws.textAll(ws_data, ws_len);
			}

			if (doc["cmd"] == "stop")
			{
				Serial.print("Request from client to stop motor...\n");
				moveState = MOVE_STOP;
				if (calibrateStatus == "progress")
				{
					calibrateStatus = "false";
					json["calibrateStatus"] = calibrateStatus;
					writeJsonFile(SPIFFS, settingsPath, json);

					// Serialize JSON object to string and send to client
					ws_len = serializeJson(json, ws_data);
					ws.textAll(ws_data, ws_len);
				}
				if (calibrateStatus == "true")
				{
					// Set current position as target and save to file
					targetPos = currentPos;
					shade = (int)(100.0 * targetPos / shadeLenght);
					json["targetPos"] = targetPos;
					json["shade"] = shade;
					writeJsonFile(SPIFFS, settingsPath, json);

					// Serialize JSON object to string and send to client
					ws_len = serializeJson(json, ws_data);
					ws.textAll(ws_data, ws_len);
				}
			}

			if (doc["cmd"] == "setShade")
			{
				Serial.printf("Request from client to set manual shade position: %d...\n", doc["shade"].as<int>());
				shade = doc["shade"];

				// Serialize JSON object to string and send to client
				json["shade"] = shade;
				ws_len = serializeJson(json, ws_data);
				ws.textAll(ws_data, ws_len);
			}

			if (doc["cmd"] == "addSunset")
			{
				Serial.printf("Request from client to set shade position: %d on sunset...\n", doc["shadeSunset"].as<int>());
				onSunset = true;
				timersDoc["onSunset"] = onSunset;
				timersDoc["shadeSunset"] = doc["shadeSunset"];

				serializeJson(timersDoc, Serial);
				ws_len = serializeJson(timersDoc, ws_data);
				ws.textAll(ws_data, ws_len);
				Serial.println();
				writeJsonFile(SPIFFS, timersPath, timersDoc);
			}

			if (doc["cmd"] == "addSunrise")
			{
				Serial.printf("Request from client to set shade position: %d on sunrise...\n", doc["shadeSunrise"].as<int>());
				onSunrise = true;
				timersDoc["onSunrise"] = onSunrise;
				timersDoc["shadeSunrise"] = doc["shadeSunrise"];

				serializeJson(timersDoc, Serial);
				ws_len = serializeJson(timersDoc, ws_data);
				ws.textAll(ws_data, ws_len);
				Serial.println();
				writeJsonFile(SPIFFS, timersPath, timersDoc);
			}

			if (doc["cmd"] == "addTimer")
			{
				Serial.printf("Request from client to add new timer id: %s...\n", doc["timer"][0].as<String>());

				if (nTimers < 10)
				{
					timersArray.add(doc["timer"]);
				}

				nTimers = timersArray.size();
				Serial.printf("Number of timers: %d\n", nTimers);
				serializeJson(timersDoc, Serial);
				ws_len = serializeJson(timersDoc, ws_data);
				ws.textAll(ws_data, ws_len);
				Serial.println();
				writeJsonFile(SPIFFS, timersPath, timersDoc);
			}

			// If delete timer message received
			if (doc["cmd"] == "deleteTimer")
			{
				Serial.printf("Request from client to remove timer id %s at %s ...\n", doc["id"].as<String>(), doc["time"].as<String>());
				nTimers = timersArray.size();

				// Find and remove timer from array by id
				for (int i = 0; i < nTimers; i++)
				{
					if (timersArray[i][0].as<String>() == doc["id"].as<String>())
					{
						timersArray.remove(i);
						Serial.printf("Timer %d id %s removed\n", i, doc["id"].as<String>());
					}
				}

				if (doc["time"] == "Восход")
				{
					onSunrise = false;
					timersDoc["onSunrise"] = onSunrise;
				}

				if (doc["time"] == "Закат")
				{
					onSunset = false;
					timersDoc["onSunset"] = onSunset;
				}

				nTimers = timersArray.size();
				Serial.printf("Number of timers: %d\n", nTimers);
				serializeJson(timersDoc, Serial);
				ws_len = serializeJson(timersDoc, ws_data);
				ws.textAll(ws_data, ws_len);
				Serial.println();
				writeJsonFile(SPIFFS, timersPath, timersDoc);
			}
			// If get timers message received
			if (doc["cmd"] == "getTimers")
			{
				Serial.printf("Request from client number of timers...\n");
				nTimers = timersArray.size();
				Serial.printf("Number of timers: %d\n", nTimers);
				serializeJson(timersDoc, Serial);
				ws_len = serializeJson(timersDoc, ws_data);
				ws.textAll(ws_data, ws_len);
			}
		}
		else
		{
			Serial.println("Error parsing JSON");
		}

		clientRequest = true;
	}
}

// Web socket onEvent handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
	switch (type)
	{
	// Client connected to server
	case WS_EVT_CONNECT:
		Serial.printf("Client [%u] is connected %s\n", client->id(), client->remoteIP().toString());

		// Serialize object to JSON string and send to client
		ws_len = serializeJson(json, ws_data);
		ws.textAll(ws_data, ws_len);
		ws_len = serializeJson(timersDoc, ws_data);
		ws.textAll(ws_data, ws_len);

		break;
	// Client disconnected from server
	case WS_EVT_DISCONNECT:
		Serial.printf("Client [%u] disconnected\n", client->id());
		break;
	// Error occured
	case WS_EVT_ERROR:
		Serial.printf("Client [%u] error(%u): %s\n", client->id(), *((uint16_t *)arg), (char *)data);
		break;
	// Message received from client
	case WS_EVT_DATA:
		handleWebSocketMessage(arg, data, len);
		break;
	default:
		break;
	}
}

// ESP Timer interrupt routine
hw_timer_t *timer = NULL;
bool timerInt = false;
void IRAM_ATTR onTimer()
{
	timerInt = true;
	localSec++;
	if (localSec == 60)
	{
		localSec = 0;
		localMin++;
	}
	if (localMin == 60)
	{
		localMin = 0;
		localHour++;
	}
	if (localHour == 24)
	{
		localHour = 0;
		timeSyncFlag = true;
	}
}

// GET request from sunrise-sunset API
SunrieseSunsetTime getSunriseSunset(String req, int tz)
{
	HTTPClient http;
	int httpCode = 0;
	SunrieseSunsetTime _time;
	DynamicJsonDocument doc(1024);

	http.begin(req);
	httpCode = http.GET();
	if (httpCode == 200)
	{
		Serial.println();
		Serial.println("Http code: " + (String)httpCode);
		String msg = http.getString();
		Serial.println("Sunrise-Sunset API message: " + msg);
		deserializeJson(doc, msg);

		_time.strSunrise12 = doc["results"]["sunrise"].as<String>();
		_time.strSunset12 = doc["results"]["sunset"].as<String>();
		_time.sunriseHour = _time.strSunrise12.substring(0, 1).toInt();
		if (_time.strSunrise12.endsWith("PM"))
			_time.sunriseHour += 12;
		_time.sunriseHour += tz;
		_time.sunriseMin = _time.strSunrise12.substring(2, 4).toInt();
		_time.sunriseSec = _time.strSunrise12.substring(5, 7).toInt();

		_time.sunsetHour = _time.strSunset12.substring(0, 1).toInt();
		if (_time.strSunset12.endsWith("PM"))
			_time.sunsetHour += 12;
		_time.sunsetHour += tz;
		_time.sunsetMin = _time.strSunset12.substring(2, 4).toInt();
		_time.sunsetSec = _time.strSunset12.substring(5, 7).toInt();
		_time.strSunrise24 = (String)_time.sunriseHour + ":" + (String)_time.sunriseMin + ":" + (String)_time.sunriseSec;
		_time.strSunset24 = (String)_time.sunsetHour + ":" + (String)_time.sunsetMin + ":" + (String)_time.sunsetSec;
		_time.dataReady = true;
	}
	return _time;
}

String translateEncryptionType(wifi_auth_mode_t encryptionType)
{
	switch (encryptionType)
	{
	case (WIFI_AUTH_OPEN):
		return "Open";
	case (WIFI_AUTH_WEP):
		return "WEP";
	case (WIFI_AUTH_WPA_PSK):
		return "WPA_PSK";
	case (WIFI_AUTH_WPA2_PSK):
		return "WPA2_PSK";
	case (WIFI_AUTH_WPA_WPA2_PSK):
		return "WPA_WPA2_PSK";
	case (WIFI_AUTH_WPA2_ENTERPRISE):
		return "WPA2_ENTERPRISE";
	}
}

DynamicJsonDocument scanNetworks()
{
	DynamicJsonDocument doc(1024);
	int numberOfNetworks = WiFi.scanNetworks();

	Serial.print("Number of networks found: ");
	Serial.println(numberOfNetworks);

	for (int i = 0; i < numberOfNetworks; i++)
	{
		Serial.print("Network name: ");
		doc[i] = WiFi.SSID(i);
		Serial.println(WiFi.SSID(i));

		Serial.print("Signal strength: ");
		Serial.println(WiFi.RSSI(i));

		Serial.print("MAC address: ");
		Serial.println(WiFi.BSSIDstr(i));

		Serial.println("-----------------------");
	}

	return doc;
}

// Setup
void setup()
{
	pinMode(SM_STEP, OUTPUT);
	pinMode(SM_DIR, OUTPUT);
	pinMode(SM_nEN, OUTPUT);
	pinMode(SW, INPUT_PULLUP);
	pinMode(LED_CONNECT, OUTPUT);
	pinMode(23, INPUT);

	digitalWrite(SM_DIR, HIGH);

	// Disable motor
	digitalWrite(SM_nEN, HIGH);

	Serial.begin(115200);

	// Init SPIFFS
	initSPIFFS();
	// ... read settings file from SPIFFS
	json = readJsonFile(SPIFFS, settingsPath);

	// json["ssid"] = "YA31";
	// json["pass"] = "audia3o765km190rus";
	// json["ip"] = "192.168.68.201";
	// json["gateway"] = "192.168.68.1";
	// json["dns"] = "192.168.68.1";
	// json["subnet"] = "255.255.255.0";
	// json["shadeLenght"] = 0;
	// json["targetPos"] = 0;
	// json["shade"] = 0;
	// json["calibrateStatus"] = "false";

	Serial.println("Settings json file content: ");
	serializeJson(json, Serial);

	if (json != nullptr)
	{
		ssid = json["ssid"];
		pass = json["pass"];
		ip = json["ip"];
		gateway = json["gateway"];
		subnet = json["subnet"];
		dns = json["dns"];

		Serial.println("SSID: " + String(ssid));
		Serial.println("Password: " + String(pass));
		Serial.println("IP Address: " + String(ip));
		Serial.println("Gateway: " + String(gateway));
		Serial.println("DNS: " + String(dns));
		Serial.println("Subnet: " + String(subnet));
		Serial.print("Hostname: ");
		Serial.println(WiFi.getHostname());

		shadeLenght = json["shadeLenght"];
		targetPos = json["targetPos"];
		shade = json["shade"];
		calibrateStatus = json["calibrateStatus"].as<String>();
		moveState = MOVE_STOP;
		currentPos = targetPos;

		Serial.println("Shade lenght: " + String(shadeLenght));
		Serial.println("Current position: " + String(currentPos));
		Serial.println("Calibrate flag: " + calibrateStatus);
	}
	else
	{
		Serial.println("Error reading settings file");
	}

	// Read saving timers from SPIFFS and add to current timers array
	DynamicJsonDocument doc(1024);
	doc = readJsonFile(SPIFFS, timersPath);
	if (doc != nullptr)
	{
		Serial.println("Saving timers doc:");
		serializeJson(doc, Serial);
		Serial.println();
		for (int i = 0; i < doc["timers"].size(); i++)
		{
			Serial.println("Timer " + (String)i + ": " + doc["timers"][i].as<String>());
			// Add saving timers to current timers array
			timersArray.add(doc["timers"][i]);
		}
		onSunrise = doc["onSunrise"];
		onSunset = doc["onSunset"];

		timersDoc["onSunrise"] = doc["onSunrise"];
		timersDoc["onSunset"] = doc["onSunset"];
		timersDoc["shadeSunrise"] = doc["shadeSunrise"];
		timersDoc["shadeSunset"] = doc["shadeSunset"];
	}
	else
	{
		Serial.println("Error reading timers file");
	}

	// If ssid is empty create access point
	if (ssid == 0)
	{
		init_flag = false;

		Serial.print("Setting Access Point: ");
		Serial.println(WiFi.getHostname());
		WiFi.softAP(WiFi.getHostname(), NULL);

		DynamicJsonDocument networks = scanNetworks();
		serializeJson(networks, Serial);

		Serial.println();
		Serial.print("AP IP address: ");
		Serial.println(WiFi.softAPIP());

		// Route WiFi settings page
		server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
				  { request->send(SPIFFS, "/wifiinit.html", "text/html"); });

		server.serveStatic("/", SPIFFS, "/");

		// POST processing
		server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
				  {
	    	int params = request->params();
	  		for(int i = 0; i < params; i ++)
	  		{
	    		AsyncWebParameter* p = request->getParam(i);
	    		if(p->isPost())
				{
	      			// HTTP POST ssid value
	      			if (p->name() == PARAM_INPUT_1)
					{
	        			ssid = p->value().c_str();
	        			Serial.print("SSID set to: ");
	        			Serial.println(ssid);
						json["ssid"] = ssid;
						json["shadeLenght"] = 0;
						json["targetPos"] = 0;
						json["shade"] = 0;
						json["calibrateStatus"] = "false";
	        			writeJsonFile(SPIFFS, settingsPath, json);
	     	 		}
	      			// HTTP POST password value
	      			if (p->name() == PARAM_INPUT_2)
					{
	        			pass = p->value().c_str();
	        			Serial.print("Password set to: ");
	        			Serial.println(pass);
						json["pass"] = pass;
	        			writeJsonFile(SPIFFS, settingsPath, json);
	      			}
	      			// HTTP POST ip value
	      			if (p->name() == PARAM_INPUT_3)
					{
	        			ip = p->value().c_str();
	        			Serial.print("IP Address set to: ");
	        			Serial.println(ip);
						json["ip"] = ip;
	        			writeJsonFile(SPIFFS, settingsPath, json);
	      			}
					// HTTP POST gateway value
	      			if (p->name() == PARAM_INPUT_4)
					{
	        			gateway = p->value().c_str();
	        			Serial.print("Gateway set to: ");
	        			Serial.println(gateway);
						json["gateway"] = gateway;
	        			writeJsonFile(SPIFFS, settingsPath, json);
	      			}
					// HTTP POST dns value
					if(p->name() == PARAM_INPUT_5)
					{
						dns = p->value().c_str();
	        			Serial.print("DNS set to: ");
	        			Serial.println(dns);
						json["dns"] = dns;
	        			writeJsonFile(SPIFFS, settingsPath, json);
					}
					// HTTP POST subnet value
					if(p->name() == PARAM_INPUT_6)
					{
						subnet  = p->value().c_str();
	        			Serial.print("Subnet set to: ");
	        			Serial.println(subnet);
						json["subnet"] = subnet;
	        			writeJsonFile(SPIFFS, settingsPath, json);
					}
	      			//Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
	    		}
	  		}
	  	//	request->send(200, "text/plain", "ESP rebooting. Connect to SSID: " + String(ssid1) + " IP: " + String(ip1));
	  		request->send(200, "text/plain", "ESP rebooting");
	  		delay(3000);
	  		ESP.restart(); });
		server.begin();
	}

	// If settings file read successfully, connect to WiFi
	else
	{
		init_flag = true;

		WiFi.mode(WIFI_STA);
		localIP.fromString(ip);
		localDNS.fromString(dns);
		localGateway.fromString(gateway);
		localSubnet.fromString(subnet);

		if (!WiFi.config(localIP, localGateway, localSubnet, localDNS))
		{
			Serial.println("Config WiFi error");
		}
		else
		{
			Serial.println("Config WiFi success");

			WiFi.begin(ssid, pass);
			Serial.print("Default ESP32 MAC Address: ");
			Serial.println(WiFi.macAddress());

			// Try to connect to wifi with timeout 10 sec
			Serial.printf("Try to connect: %s ", String(ssid));
			currMillis = millis();
			prevMillis = currMillis;
			while (WiFi.status() != WL_CONNECTED)
			{
				Serial.print('.');
				digitalWrite(LED_CONNECT, HIGH);
				delay(200);
				digitalWrite(LED_CONNECT, LOW);
				delay(200);

				currMillis = millis();
				if (currMillis - prevMillis >= 10000)
				{
					Serial.println();
					Serial.println("WiFi connection timout. Rebooting...");
					ESP.restart();
				}
			}
			digitalWrite(LED_CONNECT, HIGH);

			Serial.println(" -success");
			Serial.printf("Connected to WiFi: %s", String(ssid));
			Serial.println("");
			Serial.print("Local IP: ");
			Serial.println(WiFi.localIP());

			// Try to get local time with timeout 10 sec
			Serial.print("Waiting for NTP time sync... ");
			configTime(tz * 3600, 0, "pool.ntp.org", "time.nist.gov");
			currMillis = millis();
			prevMillis = currMillis;
			while (!getLocalTime(&timeinfo))
			{
				Serial.print(".");

				currMillis = millis();
				if (currMillis - prevMillis >= 10000)
				{
					Serial.println();
					Serial.println("Get NTP time sync timout. Rebooting...");

					ESP.restart();
				}
			}
			Serial.println(" -success");
			Serial.print("Current time: ");
			Serial.print(asctime(&timeinfo));
			localSec = timeinfo.tm_sec;
			localMin = timeinfo.tm_min;
			localHour = timeinfo.tm_hour;

			// Set up timer0 with divider 80
			timer = timerBegin(0, 80, true);
			// Attach timer interrup
			timerAttachInterrupt(timer, &onTimer, true);
			// Set timer period 1 sec
			timerAlarmWrite(timer, 1000000, true);
			// Start timer
			timerAlarmEnable(timer);

			// Connect AsyncWebSocket
			ws.onEvent(onEvent);
			server.addHandler(&ws);

			// Route to main page index.html on SPIFFS
			server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
					  { request->send(SPIFFS, "/index.html"); });
			server.serveStatic("/", SPIFFS, "/");

			// Try to get sunrise/sunset time with timeout 10 sec
			Serial.print("Request to Sunrise-Sunset API...");
			currMillis = millis();
			prevMillis = currMillis;
			while (!sstime.dataReady)
			{
				sstime = getSunriseSunset("https://api.sunrise-sunset.org/json?lat=54.93583&lng=43.32352&date=today", tz);
				Serial.print(".");

				currMillis = millis();
				if (currMillis - prevMillis >= 10000)
				{
					Serial.println("Error get Sunrise-Sunset API. Rebooting...");
					ESP.restart();
				}
			}
			Serial.println(" -success");
			Serial.println("Sunrise: " + sstime.strSunrise24);
			Serial.println("Sunset: " + sstime.strSunset24);
			json["sunrise"] = sstime.strSunrise24;
			json["sunset"] = sstime.strSunset24;
			ws_len = serializeJson(json, ws_data);
			ws.textAll(ws_data, ws_len);
		}

		// Start ElegantOTA server for on air updates
		ElegantOTA.begin(&server); // Start ElegantOTA
		// ElegantOTA callbacks
		ElegantOTA.onStart(onOTAStart);
		ElegantOTA.onProgress(onOTAProgress);
		ElegantOTA.onEnd(onOTAEnd);
		// Start server
		server.begin();
		Serial.println("HTTP server started...");
	}
}

// Main loop
void loop()
{
	ElegantOTA.loop();
	// If the system is not initialized, blink briefly 2 times
	if (!init_flag)
	{
		digitalWrite(LED_CONNECT, HIGH);
		delay(70);
		digitalWrite(LED_CONNECT, LOW);
		delay(70);
		digitalWrite(LED_CONNECT, HIGH);
		delay(70);
		digitalWrite(LED_CONNECT, LOW);
		delay(500);
	}

	// If the system is initialized, turn on the LED
	else
	{
		digitalWrite(LED_CONNECT, HIGH);

		// Sync local time and get new sunrise/sunset time every day at 24:00:00
		if (timeSyncFlag)
		{
			Serial.println("Sync local time and get sunrise/sunset time...");
			timeSyncFlag = false;

			// Try to get local time with timeout 10 sec
			Serial.print("Waiting for NTP time sync... ");
			configTime(tz * 3600, 0, "pool.ntp.org", "time.nist.gov");
			currMillis = millis();
			prevMillis = currMillis;
			while (!getLocalTime(&timeinfo))
			{
				Serial.print(".");

				currMillis = millis();
				if (currMillis - prevMillis >= 10000)
				{
					Serial.println();
					Serial.println("Get NTP time sync timout. Breaking...");
					break;
				}
			}
			Serial.println(" -success");
			Serial.print("Current time: ");
			Serial.print(asctime(&timeinfo));
			localSec = timeinfo.tm_sec;
			localMin = timeinfo.tm_min;
			localHour = timeinfo.tm_hour;

			// Try to get sunrise/sunset time with timeout 10 sec
			Serial.print("Request to Sunrise-Sunset API...");
			currMillis = millis();
			prevMillis = currMillis;
			while (!sstime.dataReady)
			{
				sstime = getSunriseSunset("https://api.sunrise-sunset.org/json?lat=54.93583&lng=43.32352&date=today", tz);
				Serial.print(".");

				currMillis = millis();
				if (currMillis - prevMillis >= 10000)
				{
					Serial.println("Get Sunrise-Sunset API timeout. Breaking...");
					break;
				}
			}
			Serial.println(" -success");
			Serial.println("Sunrise: " + sstime.strSunrise12);
			Serial.println("Sunset: " + sstime.strSunset12);
			json["sunrise"] = sstime.strSunrise24;
			json["sunset"] = sstime.strSunset24;
			ws_len = serializeJson(json, ws_data);
			ws.textAll(ws_data, ws_len);
		}

		// Check upper switch limit status
		sw_flag = !digitalRead(SW);
		// if upper switch limit trggered in calibrate mode save target position as shade lenght
		if (sw_flag && moveState == MOVE_CALIBRATE)
		{
			moveState = MOVE_STOP;
			shadeLenght = calibrateCnt;
			currentPos = 0;
			targetPos = currentPos;
			shade = (int)(100.0 * targetPos / shadeLenght);
			if (calibrateStatus == "progress")
			{
				calibrateStatus = "true";
				// Save calibrate status
				json["shadeLenght"] = shadeLenght;
				json["calibrateStatus"] = "true";
				json["targetPos"] = currentPos;
				json["shade"] = shade;
				writeJsonFile(SPIFFS, settingsPath, json);
				ws_len = serializeJson(json, ws_data);
				ws.textAll(ws_data, ws_len);
			}
		}

		if (calibrateStatus == "true")
		{
			targetPos = (int)(shadeLenght * shade / 100.0);
			if (currentPos < targetPos)
			{
				moveState = MOVE_DOWN;
				targetFlag = false;
			}
			if (currentPos > targetPos)
			{
				moveState = MOVE_UP;
				targetFlag = false;
			}
			// Stop motor and save current positions on target
			if (currentPos == targetPos)
			{
				moveState = MOVE_STOP;
				if (!targetFlag)
				{
					json["targetPos"] = targetPos;
					json["shade"] = shade;
					writeJsonFile(SPIFFS, settingsPath, json);

					// Serialize JSON object to string and send to client
					ws_len = serializeJson(json, ws_data);
					ws.textAll(ws_data, ws_len);
				}
				targetFlag = true;
			}
		}

		if (moveState == MOVE_DOWN)
		{
			// Enable motor and move down
			digitalWrite(SM_nEN, LOW);
			digitalWrite(SM_DIR, HIGH);
			currentPos++;
		}
		if (moveState == MOVE_UP)
		{
			// Enable motor and move up
			digitalWrite(SM_nEN, LOW);
			digitalWrite(SM_DIR, LOW);
			currentPos--;
		}
		if (moveState == MOVE_CALIBRATE)
		{
			// Enable motor and move up
			digitalWrite(SM_nEN, LOW);
			digitalWrite(SM_DIR, LOW);
			calibrateCnt++;
		}
		if (moveState == MOVE_STOP)
		{
			// Disable motor
			digitalWrite(SM_nEN, HIGH);
		}

		// Generate STEP-signal for step motor
		digitalWrite(SM_STEP, HIGH);
		delayMicroseconds(1000);
		digitalWrite(SM_STEP, LOW);
		delayMicroseconds(1000);

		//  Send data to client every second by timer
		if (timerInt)
		{
			// Serial.println("Current local time: " + (String)localHour + ":" + (String)localMin + ":" + (String)localSec);
			for (int i = 0; i < nTimers; i++)
			{
				if (localHour == timersDoc["timers"][i][1].as<int>() && localMin == timersDoc["timers"][i][2].as<int>() && localSec == 0)
				{
					shade = timersDoc["timers"][i][3].as<int>();
					Serial.printf("Set shade to: %d at %d:%d\n", shade, localHour, localMin);
				}
			}
			if (onSunrise)
				if (localHour == sstime.sunriseHour && localMin == sstime.sunriseMin && localSec == sstime.sunriseSec)
				{
					shade = timersDoc["shadeSunrise"].as<int>();
					Serial.printf("Set shade to: %d at %d:%d:%d on sunrise\n", shade, localHour, localMin, localSec);
				}
			if (onSunset)
				if (localHour == sstime.sunsetHour && localMin == sstime.sunsetMin && localSec == sstime.sunsetSec)
				{
					shade = timersDoc["shadeSunset"].as<int>();
					Serial.printf("Set shade to: %d at %d:%d:%d on sunset\n", shade, localHour, localMin, localSec);
				};
			timerInt = false;
		}
		// Request from client processing
		if (clientRequest)
		{
			clientRequest = false;
		}

		ws.cleanupClients();
	}
}