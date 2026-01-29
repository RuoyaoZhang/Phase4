#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic;
bool deviceConnected = false;
unsigned long lastSend = 0;

// Check IPhone Input Type
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue().c_str();  

    if (rxValue.length() > 0) {
      Serial.print("Received from iPhone: ");
      Serial.println(rxValue);

      // Reply Message
      String reply = "ESP32 got: " + rxValue;
      pTxCharacteristic->setValue(reply.c_str());
      pTxCharacteristic->notify();
    }
  }
};


// BLE connecting 
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("iPhone connected!");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("iPhone disconnected, advertising restarted...");
    BLEDevice::startAdvertising();
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE notify + receive demo...");

  BLEDevice::init("ESP32-BLE-Notify");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // TX: ESP32 → iPhone
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX: iPhone → ESP32
  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX,
                        BLECharacteristic::PROPERTY_WRITE
                      );
  pRxCharacteristic->setCallbacks(new MyCallbacks()); 
  pRxCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Ready! Open LightBlue and connect to 'ESP32-BLE-Notify'");
}

void loop() {
  if (deviceConnected && millis() - lastSend > 5000) {
    lastSend = millis();
    String msg = "Hello iPhone Time: " + String(millis() / 1000) + "s";
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
    Serial.println("Sent: " + msg);
  }
}
