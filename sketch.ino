/*
 * ============================================================
 *  Smart Classroom Occupancy System with RFID Attendance
 * ============================================================
 *  MCU       : ESP32 (Wokwi simulation)
 *  Sensors   : RC522 RFID, PIR, MQ-135 (pot), DHT22
 *  Actuators : LCD 16x2 I2C, Buzzer
 *  Cloud     : ThingSpeak (HTTP)
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ─── PIN DEFINITIONS ────────────────────────────────────────
#define SS_PIN        5
#define RST_PIN       4
#define PIR_PIN       27
#define MQ135_PIN     34
#define DHT_PIN       15
#define BUZZER_PIN    26

// ─── SENSOR OBJECTS ─────────────────────────────────────────
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── WiFi (Wokwi virtual WiFi) ─────────────────────────────
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ─── THINGSPEAK ─────────────────────────────────────────────
// 1. Go to thingspeak.com → Sign up free
// 2. Create new Channel with fields:
//    Field1=Temperature, Field2=Humidity, Field3=AirQuality
//    Field4=Occupancy, Field5=Motion
// 3. Copy the Write API Key and paste below
const char* TS_API_KEY = "C8BOZE5CYTZ0C6B6";
const char* TS_URL     = "https://api.thingspeak.com/update";

// ─── STUDENT DATABASE ───────────────────────────────────────
struct Student {
  byte uid[4];
  const char* name;
  bool present;
};

Student students[] = {
  { {0xE9, 0x76, 0x42, 0xA3}, "Anshu",    false },
  { {0xA1, 0xB2, 0xC3, 0xD4}, "Rahul",    false },
  { {0x11, 0x22, 0x33, 0x44}, "Priya",    false },
  { {0xDE, 0xAD, 0xBE, 0xEF}, "Vikram",   false },
};
const int NUM_STUDENTS = 4;

// ─── STATE ──────────────────────────────────────────────────
int   occupancyCount = 0;
bool  pirMotion      = false;
float temperature    = 0.0;
float humidity       = 0.0;
int   airQuality     = 0;

unsigned long lastSensorRead = 0;
unsigned long lastCloudPush  = 0;
unsigned long lcdClearTime   = 0;
bool          lcdShowingMsg  = false;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long CLOUD_INTERVAL  = 16000;  // ThingSpeak free = 15s min
const unsigned long LCD_MSG_TIME    = 3000;
const int MAX_OCCUPANCY             = 30;

// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Classroom Occupancy System ===");

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Classroom");
  lcd.setCursor(0, 1);
  lcd.print(" Initializing...");

  // DHT
  dht.begin();

  // SPI + RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 ready");

  // WiFi
  Serial.print("[WiFi] Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" Failed - offline mode");
  }

  delay(1000);
  updateLCDDefault();
  Serial.println("[SYSTEM] Ready!\n");
}

// ═══════════════════════════════════════════════════════════
void loop() {
  // RFID scan
  handleRFID();

  // Periodic sensor reads
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    readSensors();
  }

  // Periodic cloud push (every 16s for ThingSpeak free tier)
  if (millis() - lastCloudPush >= CLOUD_INTERVAL) {
    lastCloudPush = millis();
    pushToThingSpeak();
  }

  // LCD message timeout
  if (lcdShowingMsg && millis() >= lcdClearTime) {
    lcdShowingMsg = false;
    updateLCDDefault();
  }
}

// ─── SENSOR READING ─────────────────────────────────────────
void readSensors() {
  pirMotion = digitalRead(PIR_PIN);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity = h;

  airQuality = analogRead(MQ135_PIN);

  Serial.print("[SENSORS] T=");
  Serial.print(temperature, 1);
  Serial.print("C H=");
  Serial.print(humidity, 1);
  Serial.print("% AQ=");
  Serial.print(airQuality);
  Serial.print(" PIR=");
  Serial.print(pirMotion ? "YES" : "no");
  Serial.print(" Occ=");
  Serial.println(occupancyCount);

  checkOvercrowding();
}

// ─── RFID HANDLING ──────────────────────────────────────────
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  byte* uid = rfid.uid.uidByte;
  Serial.print("[RFID] UID: ");
  for (int i = 0; i < 4; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i < 3) Serial.print(":");
  }
  Serial.println();

  int idx = findStudent(uid);

  if (idx >= 0) {
    students[idx].present = !students[idx].present;

    if (students[idx].present) {
      occupancyCount++;
      Serial.print("[RFID] ");
      Serial.print(students[idx].name);
      Serial.print(" ENTERED | Occupancy: ");
      Serial.println(occupancyCount);
      showWelcome(students[idx].name, true);
      beep(1);
    } else {
      if (occupancyCount > 0) occupancyCount--;
      Serial.print("[RFID] ");
      Serial.print(students[idx].name);
      Serial.print(" EXITED  | Occupancy: ");
      Serial.println(occupancyCount);
      showWelcome(students[idx].name, false);
      beep(2);
    }
  } else {
    Serial.println("[RFID] Unknown card!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Unknown Card!");
    lcd.setCursor(0, 1);
    lcd.print("Access Denied");
    beep(3);
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

// ─── LCD ────────────────────────────────────────────────────
void updateLCDDefault() {
  lcd.clear();
  lcd.setCursor(0, 0);
  // Row 1: Occupancy + Temperature
  lcd.print("Occ:");
  lcd.print(occupancyCount);
  lcd.print(" T:");
  lcd.print((int)temperature);
  lcd.print("C");

  lcd.setCursor(0, 1);
  // Row 2: Humidity + Air Quality
  lcd.print("H:");
  lcd.print((int)humidity);
  lcd.print("% AQ:");
  lcd.print(airQuality);
}

void showWelcome(const char* name, bool entering) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(entering ? "Welcome," : "Goodbye,");
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
  if (occupancyCount > MAX_OCCUPANCY || airQuality > 2500) {
    Serial.println("[ALERT] OVERCROWDING / BAD AIR!");
    if (!lcdShowingMsg) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! WARNING !!");
      lcd.setCursor(0, 1);
      lcd.print(occupancyCount > MAX_OCCUPANCY ? "OVERCROWDED!" : "BAD AIR QUALITY");
      lcdShowingMsg = true;
      lcdClearTime = millis() + 4000;
      beep(5);
    }
  }
}

// ─── THINGSPEAK PUSH ────────────────────────────────────────
void pushToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CLOUD] WiFi not connected, skip");
    return;
  }

  // Build URL: api.thingspeak.com/update?api_key=KEY&field1=temp&field2=hum...
  String url = String(TS_URL);
  url += "?api_key=";
  url += TS_API_KEY;
  url += "&field1=";
  url += String(temperature, 1);
  url += "&field2=";
  url += String(humidity, 1);
  url += "&field3=";
  url += String(airQuality);
  url += "&field4=";
  url += String(occupancyCount);
  url += "&field5=";
  url += String(pirMotion ? 1 : 0);

  WiFiClientSecure client;
  client.setInsecure();  // skip SSL certificate verification

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();

  if (httpCode == 200) {
    Serial.print("[CLOUD] ThingSpeak entry #");
    Serial.println(http.getString());
  } else {
    Serial.print("[CLOUD] HTTP code: ");
    Serial.println(httpCode);
  }
  http.end();
}
