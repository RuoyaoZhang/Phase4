#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <pgmspace.h>
#include "audio_data.h"

static const char* DEVICE_NAME = "ESP32-MP3-FAST";

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

static inline void notifyChunk(const uint8_t* buf, size_t n) {
  if (!connected || !txChar) return;
  txChar->setValue((uint8_t*)buf, n);
  txChar->notify();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== FAST BLE notify streaming (LightBlue) ===");
  Serial.print("audioDataLen = ");
  Serial.println(audioDataLen);

  // iOS 常见 MTU=185（有效payload约182）；Android 可能更大
  // 这句是“我这边愿意支持更大 MTU”，最终 MTU 由手机协商决定
  BLEDevice::setMTU(185);

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

  // 这两行是常见的 iOS 兼容/连接参数提示（不保证一定按这个走，但有帮助）
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising started. Connect + enable Notify in LightBlue.");
}

void loop() {
  if (!connected) {
    delay(50);
    return;
  }

  // 180 bytes：适配 iOS 常见 MTU=185（payload≈182）
  // 如果你发现手机收不到或经常断连，把它改回 120/60/20 试试
  const size_t CHUNK = 180;
  static uint8_t tmp[CHUNK];

  Serial.println("[FAST] Start streaming...");
  uint32_t t0 = millis();

  size_t sent = 0;
  size_t chunks = 0;

  while (connected && sent < audioDataLen) {
    size_t n = audioDataLen - sent;
    if (n > CHUNK) n = CHUNK;

    // 从 PROGMEM 读出 n 字节到 tmp
    memcpy_P(tmp, audioData + sent, n);

    notifyChunk(tmp, n);
    sent += n;
    chunks++;

    // 每隔一段让出 CPU，避免把 BLE 栈压死（比固定 delay 更“上限”）
    if ((chunks % 50) == 0) {
      delay(0); // yield
    }

    // 进度打印：别太频繁，否则串口打印会拖慢速度
    if ((chunks % 200) == 0) {
      Serial.print("[FAST] sent=");
      Serial.print(sent);
      Serial.print("/");
      Serial.print(audioDataLen);
      Serial.println(" bytes");
    }
  }

  uint32_t t1 = millis();
  uint32_t dt = (t1 - t0);
  Serial.print("[FAST] End. elapsed(ms)=");
  Serial.println(dt);

  if (dt > 0) {
    // 发送端“尝试吞吐率”（不保证接收端完整收到）
    float bytes_per_s = (1000.0f * (float)sent) / (float)dt;
    Serial.print("[FAST] attempted throughput = ");
    Serial.print(bytes_per_s, 1);
    Serial.println(" B/s");
    Serial.print("[FAST] = ");
    Serial.print((bytes_per_s * 8.0f) / 1000.0f, 2);
    Serial.println(" kbps (attempted)");
  }

  Serial.println("[FAST] Done (won't repeat until reconnect).");
  while (connected) delay(200);
}
