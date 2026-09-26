#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C
#define WIFI_CHANNEL 1

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// Must be identical to the transmitter structure.
struct SensorData {
  uint32_t magic;
  uint32_t sequence;
  float temperature;
  float humidity;
  uint32_t sensorOK;
};

const uint32_t PACKET_MAGIC = 0x44485431;
const unsigned long DATA_TIMEOUT = 10000;

QueueHandle_t receiveQueue;
unsigned long lastReceived = 0;
bool haveReceived = false;
bool timeoutShown = false;

void showStatus(const char *message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP-NOW Receiver");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  display.setCursor(0, 24);
  display.println(message);
  display.display();
}

void stopWithError(const char *message) {
  Serial.println(message);
  showStatus(message);
  while (true) {
    delay(1000);
  }
}

// Receive callback signature depends on ESP32 core version.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data, int length) {
#else
void onReceive(const uint8_t *mac,
               const uint8_t *data, int length) {
#endif
  if (length != sizeof(SensorData)) {
    return;
  }

  SensorData packet;
  memcpy(&packet, data, sizeof(packet));

  if (packet.magic != PACKET_MAGIC) {
    return;
  }

  // Keep the newest packet; update OLED in loop().
  xQueueOverwrite(receiveQueue, &packet);
}

void showReadings(const SensorData &packet) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("DHT11 WIRELESS DATA");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  if (packet.sensorOK == 1) {
    display.setCursor(0, 20);
    display.print("Temp: ");
    display.print(packet.temperature, 1);
    display.println(" C");

    display.setCursor(0, 35);
    display.print("Humidity: ");
    display.print(packet.humidity, 1);
    display.println(" %");
  } else {
    display.setCursor(0, 22);
    display.println("DHT11 sensor error");
    display.setCursor(0, 36);
    display.println("Check TX sensor.");
  }

  display.setCursor(0, 54);
  display.print("Packet: ");
  display.print(packet.sequence);
  display.display();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed.");
    while (true) {
      delay(1000);
    }
  }

  showStatus("Starting...");

  receiveQueue = xQueueCreate(1, sizeof(SensorData));
  if (receiveQueue == nullptr) {
    stopWithError("Queue failed.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (esp_wifi_set_channel(
        WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    stopWithError("Channel failed.");
  }

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Use THIS MAC in the transmitter code.");

  if (esp_now_init() != ESP_OK) {
    stopWithError("ESP-NOW failed.");
  }

  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    stopWithError("Callback failed.");
  }

  showStatus("Waiting for data...");
  Serial.println("Receiver ready.");
}

void loop() {
  SensorData packet;

  if (xQueueReceive(receiveQueue, &packet, 0) == pdTRUE) {
    lastReceived = millis();
    haveReceived = true;
    timeoutShown = false;

    if (packet.sensorOK == 1) {
      Serial.printf(
        "Packet %lu | Temp: %.1f C | Humidity: %.1f %%\n",
        (unsigned long)packet.sequence,
        packet.temperature,
        packet.humidity
      );
    } else {
      Serial.println("Received: DHT11 sensor error.");
    }

    showReadings(packet);
  }

  if (haveReceived &&
      !timeoutShown &&
      millis() - lastReceived >= DATA_TIMEOUT) {
    showStatus("No data for 10 sec\nCheck transmitter.");
    Serial.println("Receiver timeout.");
    timeoutShown = true;
  }

  delay(10);
}