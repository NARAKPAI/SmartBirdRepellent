#include <Arduino.h>
#include <DHT.h>

// ==========================================
// 1. กำหนดขา GPIO
// ==========================================
#define PIR_PIN   27  // Digital Input สำหรับ PIR
#define LDR_PIN   32  // Analog Input (ADC1) สำหรับ LDR
#define DHT_PIN   18  // Digital สำหรับ DHT11

// ==========================================
// 2. ตั้งค่าเซนเซอร์และตัวแปรเวลา
// ==========================================
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

unsigned long previousMillis = 0;
const long interval = 2000; // อ่านค่าทุกๆ 2 วินาที

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  dht.begin();
  
  Serial.println("System Initialized: Reading PIR, LDR, and DHT11...");
  Serial.println("--------------------------------------------------");
}

void loop() {
  int pirState = digitalRead(PIR_PIN);

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // อ่านค่าแสงและกลับด้านให้เป็นจำนวนเต็ม (int) เพื่อใช้กับ %d
    int ldrRaw = analogRead(LDR_PIN);
    int ldrInverted = 4095 - ldrRaw; 
    float lightPercent = (ldrInverted / 4095.0) * 100.0;

    // อ่านค่าความชื้นและอุณหภูมิ
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("[ERROR] Failed to read from DHT11 sensor!");
    } else {
      Serial.printf("PIR: %s | LDR: %d (%.1f%%) | Temp: %.2f °C | Humidity: %.2f %%\n",
                    (pirState == HIGH ? "DETECTED" : "CLEAR"),
                    ldrInverted,
                    lightPercent,
                    temperature,
                    humidity);
    }
  }
}