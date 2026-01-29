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
    String v = c->getValue();   // ← 用 Arduino String

    if (v.length() > 0) {
      startRequested = true;
      Serial.println("[RX] START received from PC");
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
  Serial.println(audioDataLen);

  // 提高 MTU（central 不同意也没关系，会回落）
  BLEDevice::setMTU(185);
  BLEDevice::init(DEVICE_NAME);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  // RX: PC 写入触发 START
  rxChar = service->createCharacteristic(
    CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

  // TX: ESP32 notify 发数据
  txChar = service->createCharacteristic(
    CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->addDescriptor(new BLE2902());

  service->start();

  // 广播带 service UUID（Windows 扫描更稳定）
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

  // 等 PC 端订阅 + 写 START
  if (!startRequested) {
    static uint32_t lastMsg = 0;
    if (millis() - lastMsg > 1000) {
      Serial.println("[LOSS] Waiting for PC START...");
      lastMsg = millis();
    }
    delay(20);
    return;
  }

  // 消费这次 START（避免重复发送）
  startRequested = false;

  // 再给 Windows 一点缓冲时间（可选，但很稳）
  delay(200);

  // 每包：4-byte seq + payload
  const size_t PAYLOAD = 120;   // 稳定优先；想更快可改 180
  const size_t HEADER  = 4;
  uint8_t buf[HEADER + PAYLOAD];

  Serial.println("[LOSS] Start streaming with seq...");
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

    // payload 从 PROGMEM 拷贝到 buf[4..]
    memcpy_P(buf + HEADER, audioData + sent, n);

    notifyChunk(buf, HEADER + n);

    sent += n;
    seq++;
    chunks++;

    // 温和让出，避免把 central/协议栈打爆
    if (chunks % 50 == 0) delay(0);

    if (chunks % 200 == 0) {
      Serial.print("[LOSS] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.println(audioDataLen);
    }
  }

  // END 包：seq = 0xFFFFFFFF，长度只有 4 bytes
  uint8_t endBuf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (connected) notifyChunk(endBuf, 4);

  Serial.print("[LOSS] End. elapsed(ms)=");
  Serial.println(millis() - t0);
  Serial.println("[LOSS] Done. Waiting for next START (no need to reconnect).");

  // 连接不断开也没关系：回到 loop 顶部继续等下一次 START
  delay(200);
}
