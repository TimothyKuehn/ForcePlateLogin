#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "HX711.h"
#include <WiFiClientSecure.h>

// ---------------- Configuration ----------------

#define BUTTON_PIN 0               // GPIO pin for the config button
#define EEPROM_SIZE 512
#define INIT_HOLD_TIME 3000       // 3 seconds
#define AP_SSID "ESP32_Config"
#define AP_PASSWORD "password"

const int LOADCELL_DOUT_PIN = D4;
const int LOADCELL_SCK_PIN = D5;
const float calibration_factor = -9950;
boolean Debug_Mode = true;
const String DEVICE_ID = "0000 0001";
const String SERVER_URL = "https://3.145.35.242:5000";
bool isTransmitting = false;
String user_id;
uint timeout = 0;
String recording_name;

HX711 scale;
WebServer server(8080);

// Stored credentials
String stored_ssid = "";
String stored_password = "";
String stored_authentication_key = "";

// Button hold tracking
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

// Timers
unsigned long lastHeartbeat = 0;
unsigned long lastDataSend = 0;
unsigned long recordingStartTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 1000;   // 1 second
const unsigned long DATA_INTERVAL = 250;         // 0.25 seconds

// ---------------- Function Declarations ----------------

void setup_wifi();
void enterInitializationMode();
void handleConfigPost();
void sendData(float weight);
void sendHeartbeat();

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Read stored WiFi and authentication_key
  stored_ssid = EEPROM.readString(0);
  stored_password = EEPROM.readString(100);
  stored_authentication_key = EEPROM.readString(200);

  if (stored_ssid.length() == 0) {
    Serial.println("No WiFi credentials found. Entering setup mode.");
    enterInitializationMode();
    return;
  }

  setup_wifi();

  Serial.println("Initializing scale...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  Serial.println("Setup complete!");
}

// ---------------- Loop ----------------

void loop() {
  // ---- Button Hold Detection ----
  if (Debug_Mode == true || digitalRead(BUTTON_PIN) == LOW) {
    if (buttonPressStart == 0) {
      buttonPressStart = millis();
    } else if (Debug_Mode == true || (!buttonHeld && millis() - buttonPressStart >= INIT_HOLD_TIME)) {
      buttonHeld = true;
      Serial.println("Long press detected. Entering Initialization Mode...");
      enterInitializationMode(); // will not return
    }
  } else {
    buttonPressStart = 0;
    buttonHeld = false;
  }

  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED && now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    sendHeartbeat();
  }

  if (isTransmitting && WiFi.status() == WL_CONNECTED && now - lastDataSend >= DATA_INTERVAL) {
    lastDataSend = now;
    float weight = scale.get_units(5);
    Serial.printf("Weight: %.1f lbs\n", weight);
    sendData(weight);
    timeout++;
    if (timeout >= 15) timeout = 0;
  }

  server.handleClient();
}

// ---------------- Setup WiFi ----------------

void setup_wifi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(stored_ssid);
  WiFi.begin(stored_ssid.c_str(), stored_password.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.println("IP: " + WiFi.localIP().toString());
    Debug_Mode = false;
  } else {
    Debug_Mode = true;
    Serial.println("\nFailed to connect.");
  }
}

// ---------------- Send Data ----------------

void sendData(float weight) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  unsigned long elapsed = millis() - recordingStartTime;

  StaticJsonDocument<200> doc;
  doc["sensor_id"] = DEVICE_ID;
  doc["weight_lbs"] = String(weight, 1);
  doc["user_id"] = user_id;
  doc["timestamp"] = String(elapsed);
  doc["recording_name"] = recording_name;

  String jsonString;
  serializeJson(doc, jsonString);

  http.begin(client, SERVER_URL + "/sendmeasurements"); 
  http.addHeader("Content-Type", "application/json"); 

  Serial.println("Sending to: " + SERVER_URL + "/sendmeasurements");
  Serial.println("Payload: " + jsonString);

  int httpCode = http.POST(jsonString);

  if (httpCode > 0) {
    Serial.println("HTTP Code: " + String(httpCode));
    Serial.println("Response: " + http.getString());
  } else {
    Serial.println("HTTP Error: " + String(httpCode));
  }

  http.end();
}

// ---------------- Send Heartbeat & Check Commands ----------------

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  StaticJsonDocument<128> doc;
  doc["device_id"] = DEVICE_ID;
  doc["user_id"] = user_id;

  String payload;
  serializeJson(doc, payload);

  http.begin(client, SERVER_URL + "/heartbeat");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", "xxxxxx");

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("Heartbeat sent. HTTP code: %d\n", httpCode);

    String response = http.getString();
    StaticJsonDocument<1024> respDoc;
    DeserializationError err = deserializeJson(respDoc, response);

    if (!err && respDoc.containsKey("commands")) {
      for (JsonObject cmd : respDoc["commands"].as<JsonArray>()) {
        String cmd_device_id = cmd["device_id"] | "";
        String cmd_token = cmd["authentication_key"] | "";
        String cmd_type = cmd["command"] | "";
        String cmd_user_id = String((int)cmd["user_id"]);
        String cmd_recording_name = cmd["recording_name"];
        recording_name = cmd_recording_name;

        Serial.println("---- Command Debug ----");
        Serial.print("cmd_device_id: "); Serial.println(cmd_device_id);
        Serial.print("cmd_token: "); Serial.println(cmd_token);
        Serial.print("cmd_type: "); Serial.println(cmd_type);
        Serial.print("cmd_user_id: "); Serial.println(cmd_user_id);
        Serial.print("DEVICE_ID: "); Serial.println(DEVICE_ID);
        Serial.print("Stored token: "); Serial.println(stored_authentication_key);
        Serial.print("Recording name: "); Serial.println(cmd_recording_name);
        Serial.println("-----------------------");

        if (cmd_device_id.equals(DEVICE_ID) && cmd_token.equals(stored_authentication_key)) {
          if (cmd_type.equals("start_recording")) {
            isTransmitting = true;
            user_id = cmd_user_id;
            recordingStartTime = millis();
            Serial.println("user_id : " + user_id);
            Serial.println("cmd_user_id " + cmd_user_id);
            Serial.println("Start command received. Transmission started.");
          } 
          else if (cmd_type.equals("stop_recording") && cmd_user_id.equals(user_id)) {
            isTransmitting = false;
            Serial.println("Stop command received. Transmission stopped.");
          }
        }
      }
    }

  } else {
    Serial.printf("Heartbeat failed. Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ---------------- Initialization Mode ----------------

void enterInitializationMode() {
  Serial.println("Starting WiFi AP for config...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  delay(1000);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);

  server.on("/config", HTTP_POST, handleConfigPost);
  server.begin();
  Serial.println("HTTP server started. POST to /config with SSID, password, authentication_key");

  while (true) {
    server.handleClient();
    delay(10);
  }
}

// ---------------- Handle POST /config ----------------

void handleConfigPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  String ssid = doc["SSID"];
  String password = doc["password"];
  String authentication_key = doc["authentication_key"];

  if (ssid.length() == 0 || authentication_key.length() == 0) {
    server.send(400, "text/plain", "Missing fields");
    return;
  }

  Serial.println("Received configuration:");
  Serial.println("  SSID: " + ssid);
  Serial.println("  Password: " + password);
  Serial.println("  authentication_key: " + authentication_key);

  EEPROM.writeString(0, ssid);
  EEPROM.writeString(100, password);
  EEPROM.writeString(200, authentication_key);
  EEPROM.commit();

  StaticJsonDocument<100> responseDoc;
  responseDoc["device_id"] = DEVICE_ID;

  String responseBody;
  serializeJson(responseDoc, responseBody);

  Debug_Mode = false;
  server.send(200, "application/json", responseBody);
  delay(1000);
  ESP.restart();
}

