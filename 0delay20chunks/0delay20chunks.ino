#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "audio_data.h"

static const char* DEVICE_NAME = "ESP32-MP3-H-TRY";

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

  Serial.println("\n=== Attempt: MP3(.h) -> BLE notify streaming (no timeout) ===");
  Serial.print("audioDataLen = ");
  Serial.println(audioDataLen);
  Serial.println("This continuously attempts to stream the byte array over BLE.\n");

  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(
    CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising started");
}

void loop() {
  if (!connected) {
    delay(100);
    return;
  }

  const size_t CHUNK = 20;  // safe payload size
  uint8_t tmp[CHUNK];

  Serial.println("[TRY] Start streaming audioData (no timeout)...");
  uint32_t t0 = millis();

  size_t sent = 0;
  size_t chunks = 0;

  while (sent < audioDataLen && connected) {
    size_t n = min(CHUNK, audioDataLen - sent);

    for (size_t i = 0; i < n; i++) {
      tmp[i] = pgm_read_byte(audioData + sent + i);
    }

    // Optional: make progress visible in the BLE app (first byte changes)
    tmp[0] = (uint8_t)(sent & 0xFF);

    notifyChunk(tmp, n);
    sent += n;
    chunks++;

    if (chunks % 25 == 0) {
      Serial.print("[TRY] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.print(audioDataLen);
      Serial.println(" bytes");
    }

    // delay 
    // delay(2);
  }

  uint32_t t1 = millis();
  Serial.print("[TRY] End. elapsed(ms)=");
  Serial.println(t1 - t0);

  Serial.println("[TRY] Done (won't repeat until reconnect).");
  while (connected) delay(200);
}
