#include <WiFi.h>
#include <HTTPClient.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

const char* WIFI_SSID     = "aaaaa";
const char* WIFI_PASSWORD = "asdfghjk";

const size_t TEST_FILE_SIZE = 50 * 1024;  // 50 kB
uint8_t* testBuffer = nullptr;

const char* BLE_DEVICE_NAME = "ESP32";
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHAR_NOTIFY_UUID    "abcd1234-5678-90ab-cdef-1234567890ab"

BLEServer* pServer = nullptr;
bool deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE connected.");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("BLE disconnected.");
    BLEDevice::startAdvertising();
  }
};

void connectToWiFi();
void prepareTestBuffer();
bool runWifiUploadOnce(int attempt);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===== Wireless Performance Test (Wi-Fi + BLE) =====");

  prepareTestBuffer();
  connectToWiFi();  

  BLEDevice::init(BLE_DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic* pChar = pService->createCharacteristic(
    CHAR_NOTIFY_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pChar->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started.\n");
}

void loop() {

  if (deviceConnected) {
    Serial.println("BLE connection is active.");
  } else {
    Serial.println("Waiting for BLE connection...");
  }

  delay(2000);

  Serial.println("\nStarting Wi-Fi upload reliability test...");
  int successCount = 0;

  for (int i = 1; i <= 3; i++) {
    if (runWifiUploadOnce(i)) successCount++;
    delay(1500);
  }

  Serial.print("Upload success count: ");
  Serial.println(successCount);

  Serial.println("=== Test Finished ===");

  while (true) delay(1000);  
}

// ==================== Wi-Fi ====================
void connectToWiFi() {
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.isConnected()) {
    Serial.println("\nWi-Fi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("\nWi-Fi failed to connect.");
  }
}


void prepareTestBuffer() {
  testBuffer = (uint8_t*)malloc(TEST_FILE_SIZE);
  for (size_t i = 0; i < TEST_FILE_SIZE; i++) testBuffer[i] = 0xAB;
}


bool runWifiUploadOnce(int attempt) {
  Serial.printf("\n[Attempt %d] Uploading %d bytes...\n",
                attempt, TEST_FILE_SIZE);

  if (!WiFi.isConnected()) {
    Serial.println("Wi-Fi not connected.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  http.begin(client, "http://webhook.site/906efddf-1c85-4761-9bcd-c6ad2c737adc");  
  http.addHeader("Content-Type", "application/octet-stream");

  unsigned long start = millis();
  int code = http.POST(testBuffer, TEST_FILE_SIZE);
  unsigned long end = millis();

  float timeSec = (end - start) / 1000.0;

  Serial.printf("Time: %.3f s\n", timeSec);
  Serial.printf("HTTP Code: %d\n", code);

  http.end();

  return code > 0;
}
