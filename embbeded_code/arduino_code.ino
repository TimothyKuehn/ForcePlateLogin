#include <Arduino.h>
#include <WiFi.h>
#include "HX711.h"
#include <BLEDevice.h>
#include <BLEServer.h>

// WiFi credentials (default, can be overwritten via BLE)
const char* ssid = "xxxxxx";
const char* password = "xxxxxxxx";

// API endpoint and key
const char* api_endpoint = "https://";
const char* api_key = "xxxxxx";  // Add your API key here

// HX711 circuit wiring
const int SWITCH_INPUT_PIN = D3;
const int LOADCELL_DOUT_PIN = D4;
const int LOADCELL_SCK_PIN = D5;
const float calibration_factor = -9950;

bool bluetoothMode = false;
bool receivedNewCreds = false;
String newSSID = "";
String newPassword = "";

HX711 scale;

// BLE UUIDs (you can replace these with your own)
#define SERVICE_UUID        "180A"
#define SSID_CHAR_UUID      "2A00"
#define PASSWORD_CHAR_UUID  "2A01"

BLECharacteristic* ssidCharacteristic;
BLECharacteristic* passwordCharacteristic;

class BLEWriteCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    if (pCharacteristic == ssidCharacteristic) {
      newSSID = pCharacteristic->getValue();
      Serial.println("Received SSID: " + newSSID);
    } else if (pCharacteristic == passwordCharacteristic) {
      newPassword = pCharacteristic->getValue();
      Serial.println("Received Password: " + newPassword);
    }

    if (newSSID.length() > 0 && newPassword.length() > 0) {
      receivedNewCreds = true;
    }
  }
};

void setup_wifi(const char* s, const char* p) {
  Serial.print("Connecting to WiFi");
  WiFi.begin(s, p);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed.");
  }
}

void sendData(float weight) {
  WiFiClient client;

  // Extract host and path from the API endpoint
  String host = "your-api-host.com";  // Replace with your API host
  String path = "/your-api-path";     // Replace with your API path
  int port = 443;                     // Use 80 for HTTP or 443 for HTTPS

  if (!client.connect(host.c_str(), port)) {
    Serial.println("Connection to server failed.");
    return;
  }

  // Construct the JSON payload
  String jsonString = "{\"sensor_id\":\"LameScore_" + WiFi.macAddress() + "\",";
  jsonString += "\"weight_lbs\":" + String(weight, 1) + ",";
  jsonString += "\"timestamp\":" + String(millis()) + "}";

  // Construct the HTTP POST request
  String request = "POST " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "Content-Type: application/json\r\n";
  request += "x-api-key: " + String(api_key) + "\r\n";
  request += "Content-Length: " + String(jsonString.length()) + "\r\n";
  request += "\r\n";
  request += jsonString;

  // Send the request
  client.print(request);

  // Wait for the response
  while (client.connected() || client.available()) {
    if (client.available()) {
      String response = client.readStringUntil('\n');
      Serial.println(response);
    }
  }

  client.stop();
}

void setup_ble() {
  BLEDevice::init("XIAO-ESP32C6");
  BLEServer* pServer = BLEDevice::createServer();

  BLEService* wifiService = pServer->createService(SERVICE_UUID);

  ssidCharacteristic = wifiService->createCharacteristic(
    SSID_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  passwordCharacteristic = wifiService->createCharacteristic(
    PASSWORD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  ssidCharacteristic->setCallbacks(new BLEWriteCallbacks());
  passwordCharacteristic->setCallbacks(new BLEWriteCallbacks());

  wifiService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(wifiService->getUUID());
  pAdvertising->start();
}

void handle_ble() {
  if (receivedNewCreds) {
    receivedNewCreds = false;
    setup_wifi(newSSID.c_str(), newPassword.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(SWITCH_INPUT_PIN, INPUT_PULLUP);

  setup_wifi(ssid, password);

  Serial.println("Initializing scale...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  setup_ble();

  Serial.println("Setup complete!");
}

void loop() {
  // Reduce debug prints
  if (bluetoothMode || digitalRead(SWITCH_INPUT_PIN) == LOW) {
    bluetoothMode = true;
    while (bluetoothMode) {
      handle_ble();
      if (digitalRead(SWITCH_INPUT_PIN) == HIGH) {
        bluetoothMode = false;
        break;
      }
      delay(500);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    float weight = scale.get_units(5);
    sendData(weight);
  } else {
    setup_wifi(ssid, password);
  }

  delay(5000);
}
