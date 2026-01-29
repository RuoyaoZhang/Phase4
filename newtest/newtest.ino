#include <Arduino.h>
#include <pgmspace.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "audio_data.h"

static const char* DEVICE_NAME = "ESP32-MP3-H-TRY";

#define SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (Phone/PC -> ESP32)
#define CHAR_TX_UUID   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (ESP32 -> Phone/PC)

BLECharacteristic* txChar = nullptr;
BLECharacteristic* rxChar = nullptr;

volatile bool connected = false;
volatile bool startRequested = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    Serial.println("[BLE] Connected");
  }

  void onDisconnect(BLEServer*) override {
    connected = false;
    startRequested = false;
    Serial.println("[BLE] Disconnected -> restart advertising");
    BLEDevice::startAdvertising();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();          

    if (v.length() == 0) return;

    // Accept "start" / "START" / "s" / any non-empty as start.
    // If you want strict: only when v == "start" or "START", see below.
    startRequested = true;

    Serial.print("[RX] START received: \"");
    for (size_t i = 0; i < (size_t)v.length(); i++) Serial.print(v[i]);
    Serial.println("\"");
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

  Serial.println("\n=== BLE SPEED TEST (Central writes START): seq + payload + END ===");
  Serial.print("audioDataLen = ");
  Serial.println(audioDataLen);

  // Try higher MTU (central may refuse -> fallback automatically)
  BLEDevice::setMTU(185);
  BLEDevice::init(DEVICE_NAME);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  // RX: Central writes "start" to trigger one transfer
  rxChar = service->createCharacteristic(
    CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

  // TX: Notify stream out
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
  Serial.println("[SPEED] After connect: subscribe to TX (..0003), then write \"start\" to RX (..0002).");
}

void loop() {
  if (!connected) {
    delay(50);
    return;
  }

  // Wait for START command
  if (!startRequested) {
    delay(20);
    return;
  }

  // Consume this START so it only runs once per command
  startRequested = false;

  // ---- send one full file and time it ----

  // Packet format: 4-byte seq + payload
  const size_t PAYLOAD = 180;   // fast
  const size_t HEADER  = 4;
  uint8_t buf[HEADER + PAYLOAD];

  Serial.println("[SPEED] Start streaming...");
  uint32_t t0 = millis();

  uint32_t seq = 0;
  size_t sent = 0;
  size_t chunks = 0;

  while (connected && sent < audioDataLen) {
    size_t n = min(PAYLOAD, audioDataLen - sent);

    // seq big-endian
    buf[0] = (seq >> 24) & 0xFF;
    buf[1] = (seq >> 16) & 0xFF;
    buf[2] = (seq >>  8) & 0xFF;
    buf[3] = (seq >>  0) & 0xFF;

    // payload from PROGMEM
    memcpy_P(buf + HEADER, audioData + sent, n);

    notifyChunk(buf, HEADER + n);

    sent += n;
    seq++;
    chunks++;

    // yield a little so the BLE stack/central doesn't get overwhelmed
    if (chunks % 50 == 0) delay(0);

    // Optional progress logs
    if (chunks % 200 == 0) {
      Serial.print("[SPEED] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.println(audioDataLen);
    }
  }

  // END packet: seq = 0xFFFFFFFF (4 bytes only)
  uint8_t endBuf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (connected) notifyChunk(endBuf, 4);

  uint32_t dt = millis() - t0;

  Serial.print("[SPEED] End. elapsed(ms)=");
  Serial.println(dt);

  // Approx throughput
  uint32_t txBytes = (uint32_t)audioDataLen + (uint32_t)(seq * HEADER) + 4; // headers + END
  float seconds = dt / 1000.0f;
  float kbps = (seconds > 0) ? (txBytes * 8.0f / seconds / 1000.0f) : 0.0f;

  Serial.print("[SPEED] bytes(on-air, approx)=");
  Serial.println(txBytes);
  Serial.print("[SPEED] throughput(kbps, approx)=");
  Serial.println(kbps, 2);

  Serial.println("[SPEED] Done. Send START again to run another timed transfer.");
  delay(200);
}
