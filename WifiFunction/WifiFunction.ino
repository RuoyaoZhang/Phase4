#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "aaa";     
const char* password = "asdfghjk";     


const char* serverUrl = "https://webhook.site/906efddf-1c85-4761-9bcd-c6ad2c737adc";   

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Connection FAILED.");
    Serial.print("WiFi.status() = ");
    Serial.println(WiFi.status());
  }
}

void loop() {

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"value\": 123}";   

    int httpResponseCode = http.POST(jsonData);

    Serial.print("Response Code: ");
    Serial.println(httpResponseCode);

    http.end();
  } else {
    Serial.println("WiFi disconnected… retrying.");
    WiFi.reconnect();
  }

  delay(5000);  // upload every 5 seconds
}
