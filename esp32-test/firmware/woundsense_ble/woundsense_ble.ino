// WoundSense ESP32 + DHT22 BLE test firmware.
// Advertises as "WoundSense-ESP32" and notifies "temp,humidity,moisture" (e.g. "24.5,60.2,3.4")
// every 2s. moisture is 0-10 (0 = dry, 10 = fully wet) from an analog moisture sensor.
//
// Libraries (Arduino Library Manager): "DHT sensor library" + "Adafruit Unified Sensor".
// BLE comes with the ESP32 Arduino core. Board: "ESP32 Dev Module".
// Wiring: DHT22 VCC -> 3V3, GND -> GND, DATA -> GPIO 4 (add 10k pull-up to 3V3 if your module has none).
//         Moisture sensor VCC -> 3V3, GND -> GND, AOUT -> GPIO 34 (an ADC1 input-only pin; ADC2 pins stop working when Wi-Fi is on).

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT22

// The ESP32 has only one 3V3 pin. With no breadboard to split it, power the DHT22 from a GPIO
// instead (it draws ~1.5 mA): wire DHT22 VCC to this pin, e.g. #define DHT_POWER_PIN 18.
// Leave at -1 when DHT22 VCC is wired to 3V3.
#define DHT_POWER_PIN -1

// Moisture sensor calibration (resistive HW-103 probe): the raw ADC value reads HIGH when dry
// and drops when wet. Calibrated from the serial monitor: dry 4095, wet about 2438-2500.
// Re-measure if you change sensor, water type or how deep you dip the prongs.
#define MOISTURE_PIN 34
#define ADC_DRY 4095  // raw reading with the prongs dry in air (measured)
#define ADC_WET 2500  // raw reading with the prongs wet (measured lowest: 2438)

#define DEVICE_NAME "WoundSense-ESP32"
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

DHT dht(DHT_PIN, DHT_TYPE);
BLECharacteristic *sensorChar;
bool connected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { connected = true; }
  void onDisconnect(BLEServer *server) override {
    connected = false;
    server->startAdvertising();  // allow the phone to reconnect
  }
};

void setup() {
  Serial.begin(115200);
  if (DHT_POWER_PIN >= 0) {
    pinMode(DHT_POWER_PIN, OUTPUT);
    digitalWrite(DHT_POWER_PIN, HIGH);
    delay(1500);  // let the DHT22 start up
  }
  dht.begin();

  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);
  sensorChar = service->createCharacteristic(
      CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  sensorChar->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("Advertising as " DEVICE_NAME);
}

void loop() {
  delay(2000);  // DHT22 can't be read faster than ~0.5 Hz
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT22 read failed - check wiring");
    return;
  }
  int raw = analogRead(MOISTURE_PIN);
  float moisture = (float)(raw - ADC_DRY) / (float)(ADC_WET - ADC_DRY) * 10.0f;
  moisture = constrain(moisture, 0.0f, 10.0f);

  char buf[28];
  snprintf(buf, sizeof(buf), "%.1f,%.1f,%.1f", t, h, moisture);
  Serial.print("raw moisture ADC: ");
  Serial.println(raw);
  Serial.println(buf);
  sensorChar->setValue((uint8_t *)buf, strlen(buf));
  if (connected) sensorChar->notify();
}
