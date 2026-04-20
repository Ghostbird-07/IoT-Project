/*
 * ============================================================
 *  Smart Classroom Occupancy System with RFID Attendance
 * ============================================================
 *  MCU       : ESP32 (Wokwi simulation)
 *  Sensors   : RC522 RFID, PIR, MQ-135 (analog), DHT22
 *  Actuators : LCD 16x2 I2C, Buzzer
 *  Cloud     : ThingsBoard (MQTT)
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>   // MQTT
#include <SPI.h>
#include <MFRC522.h>        // RFID RC522
#include <DHT.h>            // DHT22
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ─── PIN DEFINITIONS ────────────────────────────────────────
#define RST_PIN       4     // RC522 RST
#define SS_PIN        5     // RC522 SDA/SS
#define PIR_PIN       27    // PIR motion sensor
#define MQ135_PIN     34    // MQ-135 analog output (ADC)
#define DHT_PIN       15    // DHT22 data
#define BUZZER_PIN    26    // Buzzer

// ─── DHT SETUP ──────────────────────────────────────────────
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

// ─── RFID SETUP ─────────────────────────────────────────────
MFRC522 rfid(SS_PIN, RST_PIN);

// ─── LCD SETUP (address 0x27, 16 cols, 2 rows) ─────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── WiFi CREDENTIALS ───────────────────────────────────────
// Wokwi provides a virtual WiFi – use "Wokwi-GUEST" with no password
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ─── THINGSBOARD MQTT ───────────────────────────────────────
// Replace with your ThingsBoard server IP/hostname
const char* TB_SERVER     = "demo.thingsboard.io";
const int   TB_PORT       = 1883;
// Replace with the device access token from ThingsBoard
const char* TB_TOKEN      = "YOUR_DEVICE_ACCESS_TOKEN";

WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ─── STUDENT DATABASE (simulated RFID UIDs) ─────────────────
// In Wokwi, the RFID tag UID can be configured in diagram.json
struct Student {
  byte uid[4];
  const char* name;
  bool present;       // true = inside classroom
};

Student students[] = {
  { {0xE9, 0x76, 0x42, 0xA3}, "Anshu",    false },
  { {0xA1, 0xB2, 0xC3, 0xD4}, "Rahul",    false },
  { {0x11, 0x22, 0x33, 0x44}, "Priya",    false },
  { {0xDE, 0xAD, 0xBE, 0xEF}, "Vikram",   false },
};
const int NUM_STUDENTS = sizeof(students) / sizeof(students[0]);

// ─── STATE VARIABLES ────────────────────────────────────────
int  occupancyCount  = 0;
bool pirMotion       = false;
float temperature    = 0.0;
float humidity       = 0.0;
int   airQuality     = 0;  // raw ADC 0-4095

unsigned long lastSensorRead  = 0;
unsigned long lastCloudPush   = 0;
unsigned long lcdClearTime    = 0;
bool          lcdShowingMsg   = false;

const unsigned long SENSOR_INTERVAL = 2000;   // 2 s
const unsigned long CLOUD_INTERVAL  = 5000;   // 5 s
const unsigned long LCD_MSG_TIME    = 3000;   // 3 s for welcome msg
const int MAX_OCCUPANCY             = 30;     // overcrowding threshold

// ─── FUNCTION PROTOTYPES ────────────────────────────────────
void connectWiFi();
void connectMQTT();
void readSensors();
void handleRFID();
void pushToCloud();
void updateLCDDefault();
void showWelcome(const char* name, bool entering);
void beep(int times);
int  findStudent(byte* uid);
void checkOvercrowding();
void pushAttendanceEvent(const char* name, bool entered);

// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Classroom Occupancy System ===");

  // Pin modes
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Classroom");
  lcd.setCursor(0, 1);
  lcd.print("  Initializing..");

  // DHT
  dht.begin();

  // SPI + RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 initialized");

  // WiFi
  connectWiFi();

  // MQTT
  mqtt.setServer(TB_SERVER, TB_PORT);
  connectMQTT();

  delay(1000);
  updateLCDDefault();
  Serial.println("[SYSTEM] Ready!\n");
}

// ═══════════════════════════════════════════════════════════
void loop() {
  // Keep MQTT alive
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // ── RFID scan ──
  handleRFID();

  // ── Periodic sensor reads ──
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    readSensors();
  }

  // ── Periodic cloud push ──
  if (millis() - lastCloudPush >= CLOUD_INTERVAL) {
    lastCloudPush = millis();
    pushToCloud();
  }

  // ── LCD message timeout ──
  if (lcdShowingMsg && millis() >= lcdClearTime) {
    lcdShowingMsg = false;
    updateLCDDefault();
  }
}

// ─── WiFi ───────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Connection failed – running offline");
  }
}

// ─── MQTT ───────────────────────────────────────────────────
void connectMQTT() {
  if (mqtt.connected()) return;
  Serial.print("[MQTT] Connecting to ThingsBoard...");
  if (mqtt.connect("ESP32_Classroom", TB_TOKEN, "")) {
    Serial.println(" connected!");
  } else {
    Serial.printf(" failed (rc=%d). Retrying later.\n", mqtt.state());
  }
}

// ─── SENSOR READING ─────────────────────────────────────────
void readSensors() {
  // PIR
  pirMotion = digitalRead(PIR_PIN);

  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity = h;

  // MQ-135 (analog)
  airQuality = analogRead(MQ135_PIN);

  // Serial log
  Serial.printf("[SENSORS] Temp=%.1f°C  Hum=%.1f%%  AQ=%d  PIR=%s  Occupancy=%d\n",
                temperature, humidity, airQuality,
                pirMotion ? "MOTION" : "clear", occupancyCount);

  // Check overcrowding
  checkOvercrowding();
}

// ─── RFID HANDLING ──────────────────────────────────────────
void handleRFID() {
  // Check for new card
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // Get UID
  byte* uid = rfid.uid.uidByte;
  Serial.printf("[RFID] Card UID: %02X:%02X:%02X:%02X\n",
                uid[0], uid[1], uid[2], uid[3]);

  // Look up student
  int idx = findStudent(uid);

  if (idx >= 0) {
    // Toggle entry/exit
    students[idx].present = !students[idx].present;

    if (students[idx].present) {
      occupancyCount++;
      Serial.printf("[RFID] %s  → ENTERED (Occupancy: %d)\n",
                    students[idx].name, occupancyCount);
      showWelcome(students[idx].name, true);
      beep(1);  // single beep = entry
    } else {
      occupancyCount = max(0, occupancyCount - 1);
      Serial.printf("[RFID] %s  → EXITED  (Occupancy: %d)\n",
                    students[idx].name, occupancyCount);
      showWelcome(students[idx].name, false);
      beep(2);  // double beep = exit
    }

    // Send attendance event to cloud immediately
    pushAttendanceEvent(students[idx].name, students[idx].present);
  } else {
    Serial.println("[RFID] Unknown card!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Unknown Card!");
    lcd.setCursor(0, 1);
    lcd.print("Access Denied");
    beep(3);  // triple beep = unknown
    lcdShowingMsg = true;
    lcdClearTime = millis() + LCD_MSG_TIME;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

int findStudent(byte* uid) {
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (memcmp(uid, students[i].uid, 4) == 0) return i;
  }
  return -1;
}

// ─── LCD FUNCTIONS ──────────────────────────────────────────
void updateLCDDefault() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("Occ:%d T:%.0fC", occupancyCount, temperature);
  lcd.setCursor(0, 1);
  lcd.printf("H:%.0f%% AQ:%d", humidity, airQuality);
}

void showWelcome(const char* name, bool entering) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (entering) {
    lcd.print("Welcome,");
  } else {
    lcd.print("Goodbye,");
  }
  lcd.setCursor(0, 1);
  lcd.print(name);
  lcd.print("!");

  lcdShowingMsg = true;
  lcdClearTime = millis() + LCD_MSG_TIME;
}

// ─── BUZZER ─────────────────────────────────────────────────
void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(100);
  }
}

// ─── OVERCROWDING CHECK ─────────────────────────────────────
void checkOvercrowding() {
  // Alert if occupancy exceeds threshold OR air quality is bad
  if (occupancyCount > MAX_OCCUPANCY || airQuality > 2500) {
    Serial.println("[ALERT] ⚠ OVERCROWDING / BAD AIR QUALITY!");
    // Flash LCD warning briefly
    if (!lcdShowingMsg) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! WARNING !!");
      lcd.setCursor(0, 1);
      if (occupancyCount > MAX_OCCUPANCY)
        lcd.print("OVERCROWDED!");
      else
        lcd.print("BAD AIR QUALITY");
      lcdShowingMsg = true;
      lcdClearTime = millis() + 4000;
      beep(5);
    }
  }
}

// ─── CLOUD PUSH (ThingsBoard telemetry) ─────────────────────
void pushToCloud() {
  if (!mqtt.connected()) return;

  // Build JSON telemetry payload
  StaticJsonDocument<256> doc;
  doc["temperature"]   = temperature;
  doc["humidity"]       = humidity;
  doc["air_quality"]    = airQuality;
  doc["occupancy"]      = occupancyCount;
  doc["motion"]         = pirMotion ? 1 : 0;

  char payload[256];
  serializeJson(doc, payload);

  // ThingsBoard telemetry topic
  mqtt.publish("v1/devices/me/telemetry", payload);
  Serial.printf("[CLOUD] Published: %s\n", payload);
}

// ─── ATTENDANCE EVENT ───────────────────────────────────────
void pushAttendanceEvent(const char* name, bool entered) {
  if (!mqtt.connected()) return;

  StaticJsonDocument<200> doc;
  doc["student"]   = name;
  doc["event"]     = entered ? "ENTRY" : "EXIT";
  doc["occupancy"] = occupancyCount;
  doc["timestamp"] = millis();

  char payload[200];
  serializeJson(doc, payload);

  mqtt.publish("v1/devices/me/telemetry", payload);
  Serial.printf("[CLOUD] Attendance: %s\n", payload);
}
