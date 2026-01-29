#include <pgmspace.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "audio_data.h"   // 必须在同一文件夹

static const char* DEVICE_NAME = "ESP32-MP3-LOSS";

// 你缺的就是这两个 UUID 定义：
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

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

void setup() {
  Serial.begin(115200);

  BLEDevice::init("ESP32-MP3-FAST");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(
    CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->addDescriptor(new BLE2902());

  service->start();
  BLEDevice::startAdvertising();
}


void loop() {
  static bool waited = false;

  if (!connected) {
    waited = false;          // 断开就复位
    delay(50);
    return;
  }

  if (!waited) {
    Serial.println("[LOSS] Connected. Waiting 800ms for subscription...");
    delay(800);              // 我建议 800ms，比500更稳
    waited = true;
  }

  const size_t CHUNK_PAYLOAD = 120;
  const size_t HEADER = 4;
  static uint8_t buf[HEADER + CHUNK_PAYLOAD];

  Serial.println("[LOSS] Start streaming with seq...");
  uint32_t t0 = millis();

  uint32_t seq = 0;
  size_t sent = 0;
  size_t chunks = 0;

  while (connected && sent < audioDataLen) {
    size_t n = min(CHUNK_PAYLOAD, audioDataLen - sent);

    buf[0] = (seq >> 24) & 0xFF;
    buf[1] = (seq >> 16) & 0xFF;
    buf[2] = (seq >>  8) & 0xFF;
    buf[3] = (seq >>  0) & 0xFF;

    memcpy_P(buf + HEADER, audioData + sent, n);

    txChar->setValue(buf, HEADER + n);
    txChar->notify();

    sent += n;
    seq++;
    chunks++;

    if (chunks % 50 == 0) delay(0);
  }

  uint8_t endBuf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (connected) {
    txChar->setValue(endBuf, 4);
    txChar->notify();
  }

  Serial.print("[LOSS] End. elapsed(ms)=");
  Serial.println(millis() - t0);
  Serial.println("[LOSS] Done. Waiting for disconnect/reconnect...");

  // 不要卡死在这里导致状态不复位
  waited = false;
  while (connected) delay(50);
}

