#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define WIFI_CHANNEL 1

DHT dht(DHT_PIN, DHT_TYPE);

// OLED receiver's Wi-Fi station MAC address
uint8_t receiverMAC[] = {
  0x68, 0x25, 0xDD, 0x33, 0x87, 0x5C
};

// This structure must be identical in both sketches.
struct SensorData {
  uint32_t magic;
  uint32_t sequence;
  float temperature;
  float humidity;
  uint32_t sensorOK;
};

const uint32_t PACKET_MAGIC = 0x44485431;
uint32_t sequenceNumber = 0;
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 3000;

void stopWithError(const char *message) {
  Serial.println(message);
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (esp_wifi_set_channel(
        WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    stopWithError("Could not set Wi-Fi channel.");
  }

  Serial.print("Transmitter MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    stopWithError("ESP-NOW initialization failed.");
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, sizeof(receiverMAC));
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    stopWithError("Could not add receiver.");
  }

  Serial.println("Transmitter ready.");
  lastSend = millis();
}

void loop() {
  if (millis() - lastSend < SEND_INTERVAL) {
    return;
  }
  lastSend = millis();

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  SensorData packet = {};
  packet.magic = PACKET_MAGIC;
  packet.sequence = ++sequenceNumber;
  packet.sensorOK =
      (!isnan(temperature) && !isnan(humidity)) ? 1 : 0;

  packet.temperature = temperature;
  packet.humidity = humidity;

  if (packet.sensorOK) {
    Serial.printf(
      "Packet %lu | Temperature: %.1f C | Humidity: %.1f %%\n",
      (unsigned long)packet.sequence,
      temperature,
      humidity
    );
  } else {
    Serial.println("DHT11 read failed. Check wiring.");
  }

  esp_err_t result = esp_now_send(
    receiverMAC,
    reinterpret_cast<const uint8_t *>(&packet),
    sizeof(packet)
  );

  if (result == ESP_OK) {
    Serial.println("Packet queued. Check receiver for arrival.");
  } else {
    Serial.printf("Send error: %d\n", (int)result);
  }
}