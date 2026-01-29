#include <Arduino.h>
#include <pgmspace.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "audio_data.h"   

static const char* DEVICE_NAME = "ESP32-MP3-H-TRY";

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

BLECharacteristic* txChar = nullptr;
BLECharacteristic* rxChar = nullptr;

volatile bool connected = false;
volatile int requestedSeconds = 0;

// ---------------- BLE callbacks ----------------

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    Serial.println("[BLE] Connected");
  }

  void onDisconnect(BLEServer*) override {
    connected = false;
    requestedSeconds = 0;
    Serial.println("[BLE] Disconnected -> restart advertising");
    BLEDevice::startAdvertising();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length() == 0) return;

    int sec = v.toInt();
    if (sec == 1 || sec == 2 || sec == 5 ||
        sec == 10 || sec == 20 || sec == 30 || sec == 100) {
      requestedSeconds = sec;
      Serial.print("[RX] Request ");
      Serial.print(sec);
      Serial.println("s");
    } else {
      Serial.print("[RX] Invalid command: ");
      Serial.println(v);
    }
  }
};

// ---------------- BLE send helper ----------------

static void notifyChunk(const uint8_t* buf, size_t n) {
  if (!connected || !txChar) return;
  txChar->setValue((uint8_t*)buf, n);
  txChar->notify();
}

// ---------------- Send logic ----------------

void sendAudio(int seconds) {
  size_t cutLen = audioDataLen_10s;
  int repeat = 1;

  // ---- decide cut / repeat ----
  switch (seconds) {
    case 1:   cutLen = audioDataLen_1s;  repeat = 1;  break;
    case 2:   cutLen = audioDataLen_2s;  repeat = 1;  break;
    case 5:   cutLen = audioDataLen_5s;  repeat = 1;  break;
    case 10:  cutLen = audioDataLen_10s; repeat = 1;  break;
    case 20:  cutLen = audioDataLen_10s; repeat = audioRepeat_20s;  break;
    case 30:  cutLen = audioDataLen_10s; repeat = audioRepeat_30s;  break;
    case 100: cutLen = audioDataLen_10s; repeat = audioRepeat_100s; break;
    default:  return;
  }

  // ---- packet format ----
  const size_t HEADER = 4;
  const size_t PAYLOAD = 180;   // if unstable, try 120
  uint8_t buf[HEADER + PAYLOAD];

  Serial.print("[SEND] Start ");
  Serial.print(seconds);
  Serial.print("s  (repeat=");
  Serial.print(repeat);
  Serial.print(", cutLen=");
  Serial.print(cutLen);
  Serial.println(")");

  uint32_t t0 = millis();
  uint32_t seq = 0;
  size_t totalSent = 0;

  for (int r = 0; r < repeat && connected; r++) {
    size_t sent = 0;
    while (sent < cutLen && connected) {
      size_t n = min(PAYLOAD, cutLen - sent);

      // seq (big endian)
      buf[0] = (seq >> 24) & 0xFF;
      buf[1] = (seq >> 16) & 0xFF;
      buf[2] = (seq >>  8) & 0xFF;
      buf[3] = (seq >>  0) & 0xFF;

      memcpy_P(buf + HEADER, audioData + sent, n);
      notifyChunk(buf, HEADER + n);

      sent += n;
      totalSent += n;
      seq++;

      if (seq % 50 == 0) delay(0);
    }
  }

  // END packet
  uint8_t endBuf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (connected) notifyChunk(endBuf, 4);

  uint32_t dt = millis() - t0;

  float secondsElapsed = dt / 1000.0f;
  float kbps = (secondsElapsed > 0)
                 ? (totalSent * 8.0f / secondsElapsed / 1000.0f)
                 : 0.0f;

  Serial.print("[SEND] End. elapsed(ms)=");
  Serial.println(dt);
  Serial.print("[SEND] throughput(kbps)=");
  Serial.println(kbps, 2);
  Serial.println("[SEND] Ready for next command");
}

// ---------------- setup / loop ----------------

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n=== BLE AUDIO LENGTH TEST (1/2/5/10/20/30/100s) ===");

  BLEDevice::setMTU(185);
  BLEDevice::init(DEVICE_NAME);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  rxChar = service->createCharacteristic(
    CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

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
  Serial.println("[INFO] Write 1 / 2 / 5 / 10 / 20 / 30 / 100 to RX to send");
}

void loop() {
  if (!connected) {
    delay(50);
    return;
  }

  if (requestedSeconds == 0) {
    delay(20);
    return;
  }

  int sec = requestedSeconds;
  requestedSeconds = 0;

  sendAudio(sec);
  delay(200);
}
