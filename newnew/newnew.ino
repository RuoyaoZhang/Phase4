#include <Arduino.h>
#include <pgmspace.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "audio_data.h"

static const char* DEVICE_NAME = "ESP32-MP3-H-TRY";

#define SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (PC -> ESP32)
#define CHAR_TX_UUID   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (ESP32 -> PC)

static const uint32_t END_SEQ = 0xFFFFFFFF;

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
    if (!v.isEmpty()) {
      startRequested = true;
      Serial.println("[RX] START received");
    }
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

  Serial.println("\n=== BLE LOSS TEST (PC triggers START): seq + payload + END ===");
  Serial.print("audioDataLen = ");
  Serial.println((unsigned int)audioDataLen);

  // Try larger MTU (central may refuse -> will fallback)
  BLEDevice::setMTU(185);
  BLEDevice::init(DEVICE_NAME);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  // RX: PC writes to trigger START
  rxChar = service->createCharacteristic(
    CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

  // TX: notify
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
    delay(50);
    return;
  }

  if (!startRequested) {
    delay(20);
    return;
  }

  // consume this START (one write -> one run)
  startRequested = false;

  // Packet format: 4-byte seq + payload
  const size_t HEADER  = 4;
  const size_t PAYLOAD = 60;     
  uint8_t buf[HEADER + PAYLOAD];

  Serial.println("[LOSS] Start streaming with seq...");
  uint32_t t0 = millis();

  uint32_t seq = 0;
  size_t sent = 0;
  size_t chunks = 0;

  while (connected && sent < (size_t)audioDataLen) {
    size_t n = min(PAYLOAD, (size_t)audioDataLen - sent);

    // seq big-endian
    buf[0] = (seq >> 24) & 0xFF;
    buf[1] = (seq >> 16) & 0xFF;
    buf[2] = (seq >>  8) & 0xFF;
    buf[3] = (seq >>  0) & 0xFF;

    // payload from PROGMEM
    memcpy_P(buf + HEADER, audioData + sent, n);

    notifyChunk(buf, HEADER + n);

    // add delay
    delay(4);

    sent += n;
    seq++;
    chunks++;

    // yield a bit so we don't overwhelm central/stack
    if (chunks % 50 == 0) delay(0);

    if (chunks % 200 == 0) {
      Serial.print("[LOSS] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.println((unsigned int)audioDataLen);
    }
  }

  // END packet (seq=0xFFFFFFFF, 4 bytes only)
  uint8_t endBuf[4] = {0xFF, 0xFF, 0xFF, 0xFF};

  if (connected) {
    // Sending End
    for (int i = 0; i < 30; i++) {
      notifyChunk(endBuf, 4);
      delay(5);  
    }
  }

  Serial.print("[LOSS] End. elapsed(ms)=");
  Serial.println(millis() - t0);
  Serial.println("[LOSS] Done. Waiting for next START (no need to reconnect).");

  delay(200);
}
