/*
  Video 1 (Simulated Data Timing Test) - BLE only
  - Sends a fixed amount of synthetic data over BLE notifications
  - Prints start/finish + elapsed time on Serial Monitor
  - No real audio content

  Phone side (LightBlue):
  1) Connect to "ESP32-BLE-SimTiming"
  2) Open service 6E400001...
  3) Open characteristic 6E400003... (Notify)
  4) Tap Subscribe
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char* DEVICE_NAME = "ESP32-BLE-SimTiming";

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

BLECharacteristic* txChar = nullptr;
volatile bool connected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    Serial.println("[BLE] Connected");
  }
  void onDisconnect(BLEServer*) override {
    connected = false;
    Serial.println("[BLE] Disconnected -> restart advertising");
    BLEDevice::startAdvertising();
  }
};

static void notifyChunk(const uint8_t* buf, size_t n) {
  if (!connected || !txChar) return;
  txChar->setValue((uint8_t*)buf, n);
  txChar->notify();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== BLE Simulated Data Timing Test ===");
  Serial.println("LightBlue: connect -> 6E400003 -> Subscribe (Notify)");
  Serial.println("After connection, device waits 5s then transmits a fixed payload.\n");

  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(
    CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  txChar->addDescriptor(new BLE2902());

  // Set an initial value so LightBlue has something to show even before notify
  const char* readyMsg = "READY";
  txChar->setValue((uint8_t*)readyMsg, strlen(readyMsg));

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising started");
}

void loop() {
  static bool started = false;

  if (!connected) {
    started = false;
    delay(100);
    return;
  }

  if (!started) {
    Serial.println("[BLE] Connected. Please tap Subscribe in LightBlue...");
    delay(5000); // give time to subscribe
    started = true;
  }

  // ===== Test parameters =====
  const size_t TOTAL_BYTES = 10000;  // change if your datasheet uses a different size
  const size_t CHUNK = 20;           // safe BLE payload size
  const uint16_t PACING_MS = 2;      // pacing to avoid iOS/LightBlue dropping UI updates

  uint8_t buf[CHUNK];
  for (size_t i = 0; i < CHUNK; i++) buf[i] = (uint8_t)i;

  Serial.println("[TEST] Start simulated transfer");
  Serial.print("[TEST] totalBytes=");
  Serial.print(TOTAL_BYTES);
  Serial.print(" chunk=");
  Serial.print(CHUNK);
  Serial.print(" pacing_ms=");
  Serial.println(PACING_MS);

  uint32_t t0 = millis();
  size_t sent = 0;
  size_t packets = 0;

  while (sent < TOTAL_BYTES && connected) {
    size_t n = min(CHUNK, TOTAL_BYTES - sent);

    // Make packets visibly different (sequence counter)
    buf[0] = (uint8_t)(packets & 0xFF);

    notifyChunk(buf, n);
    sent += n;
    packets++;

    if (packets % 50 == 0) {
      Serial.print("[TEST] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.print(TOTAL_BYTES);
      Serial.println(" bytes");
    }

    if (PACING_MS) delay(PACING_MS);
  }

  uint32_t t1 = millis();
  Serial.println("[TEST] Finished");
  Serial.print("[TEST] packets=");
  Serial.println(packets);
  Serial.print("[TEST] elapsed(ms)=");
  Serial.println(t1 - t0);

  // Repeat for easier filming / re-subscribe
  Serial.println("[TEST] Restarting in 3 seconds...\n");
  delay(3000);
}
