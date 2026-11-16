#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>

// ไลบรารีสำหรับปิดระบบตัดไฟตก (Brownout Detector)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// โหลดไฟล์เสริมของไลบรารี Firebase และไฟล์รหัสผ่าน
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "secrets.h"

// ==========================================
// 1. กำหนดขา GPIO และตั้งค่าเซนเซอร์
// ==========================================
#define PIR_PIN   27  // Digital Input สำหรับ PIR
#define LDR_PIN   32  // Analog Input (ADC1) สำหรับ LDR
#define DHT_PIN   18  // Digital สำหรับ DHT11
#define DHT_TYPE  DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// ตัวแปรสำหรับ Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long previousMillis = 0;
const long interval = 2000; // อ่านและส่งค่าทุกๆ 2 วินาที
bool signupOK = false;

void setup() {
  // ปิดระบบแจ้งเตือนไฟตกชั่วคราว เป็นคำสั่งแรกสุด
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  dht.begin();

  // 1. ตั้งโหมดและลดกำลังส่งก่อนเชื่อมต่อ Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  // 2. ตั้งค่า Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // ล็อกอินแบบ Anonymous (ต้องเปิด Anonymous Sign-in ใน Firebase Authentication)
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase SignUp OK");
    signupOK = true;
  } else {
    Serial.printf("Firebase SignUp Failed: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("System Initialized: Reading PIR, LDR, DHT11 & Sending to Firebase...");
  Serial.println("------------------------------------------------------------------");
}

void loop() {
  int pirState = digitalRead(PIR_PIN);

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // แก้ไขสมการคำนวณค่าแสงให้ถูกต้อง
    int ldrRaw = analogRead(LDR_PIN);
    int ldrInverted = 4095 - ldrRaw; 
    float lightPercent = (ldrInverted / 4095.0) * 100.0;

    // อ่านค่าความชื้นและอุณหภูมิ
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("[ERROR] Failed to read from DHT11 sensor!");
      return; 
    }

    // แสดงผลออกทาง Serial Monitor
    Serial.printf("PIR: %s | LDR: %d (%.1f%%) | Temp: %.2f °C | Humidity: %.2f %%\n",
                  (pirState == HIGH ? "DETECTED" : "CLEAR"),
                  ldrInverted,
                  lightPercent,
                  temperature,
                  humidity);

    // 3. ส่งข้อมูลขึ้น Firebase Realtime Database
    if (Firebase.ready() && signupOK) {
      FirebaseJson json;
      json.set("pir", pirState);
      json.set("ldr_raw", ldrInverted);
      json.set("ldr_percent", round(lightPercent * 10) / 10.0);
      json.set("temp", round(temperature * 100) / 100.0);
      json.set("hum", round(humidity * 100) / 100.0);

      if (Firebase.RTDB.setJSON(&fbdo, "/live_status/Node_01", &json)) {
        Serial.println(" -> [Firebase] Data sent successfully!");
      } else {
        Serial.printf(" -> [Firebase ERROR] %s\n", fbdo.errorReason().c_str());
      }
    }
  }
}