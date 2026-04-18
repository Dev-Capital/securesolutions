#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "secrets.h"

// =========================
// 1. CONFIGURATION
// =========================
const char *NODE_NAME = "NODE-3";
uint8_t controllerMac[] = CONTROLLER_MAC ;
int gatewayChannel = 1; 

const int BUTTON_PIN = 9;
const int SCL_PIN    = 5; 
const int SDA_PIN    = 6; 
const int HALL_PIN   = 4;
const int LED_PIN    = 8; 

typedef struct {
  char nodeName[16];
  uint32_t counter;
  int alert;      // 0=HB, 1=Btn, 2=Accel, 3=Hall (Magnet Removed)
  float value;    
} NodePacket;

NodePacket pkt;
esp_now_peer_info_t peerInfo;
uint32_t sendCounter = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastTrigger = 0;
const unsigned long COOLDOWN = 1000; // 1 Second Delay

float accelBaseline = 0;
int   hallBaseline  = 0;
uint8_t LIS_ADDR    = 0x18; 

// =========================
// 2. SENSOR HELPERS
// =========================
void writeRegister(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LIS_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

int16_t readAxis(uint8_t reg) {
  Wire.beginTransmission(LIS_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(LIS_ADDR, (uint8_t)2);
  if (Wire.available() == 2) {
    uint8_t lsb = Wire.read();
    uint8_t msb = Wire.read();
    return (int16_t)((msb << 8) | lsb);
  }
  return 0;
}

float getAccelMag() {
  float x = readAxis(0x28) * 0.061;
  float y = readAxis(0x2A) * 0.061;
  float z = readAxis(0x2C) * 0.061;
  return sqrt(x*x + y*y + z*z);
}

void sendPacket(int alertType, float val) {
  memset(&pkt, 0, sizeof(pkt));
  strncpy(pkt.nodeName, NODE_NAME, sizeof(pkt.nodeName) - 1);
  pkt.counter = ++sendCounter;
  pkt.alert   = alertType;
  pkt.value   = val;

  esp_now_send(controllerMac, (uint8_t *)&pkt, sizeof(pkt));
  Serial.printf("[TX] Alert %d | Val: %.2f\n", alertType, val);
}

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    digitalWrite(LED_PIN, LOW); delay(15); digitalWrite(LED_PIN, HIGH);
  }
}

// =========================
// 3. SETUP
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(0x18);
  if (Wire.endTransmission() != 0) { LIS_ADDR = 0x19; }
  writeRegister(0x20, 0x50); // 100Hz

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(gatewayChannel, WIFI_SECOND_CHAN_NONE); 

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_send_cb(onDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, controllerMac, 6);
  peerInfo.channel = 0; 
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // --- CALIBRATION (Keep still, magnet present) ---
  Serial.println("CALIBRATING... KEEP STILL & KEEP MAGNET PRESENT");
  digitalWrite(LED_PIN, LOW); // LED ON during calibration
  
  float aSum = 0;
  long hSum = 0;
  for(int i = 0; i < 50; i++) {
    aSum += getAccelMag();
    hSum += analogRead(HALL_PIN);
    delay(30);
  }
  accelBaseline = aSum / 50.0;
  hallBaseline  = hSum / 50;
  
  digitalWrite(LED_PIN, HIGH); // LED OFF when ready
  Serial.println("NODE-1 READY.");
}

// =========================
// 4. LOOP
// =========================
void loop() {
  unsigned long now = millis();

  // A. Heartbeat (10s)
  if (now - lastHeartbeat > 10000) {
    sendPacket(0, 0.0);
    lastHeartbeat = now;
  }

  // B. ACCELEROMETER CHECK
  float currentMag = getAccelMag();
  float aDelta = abs(currentMag - accelBaseline);

  // Sensitivity set to 300.0 (Adjust if too sensitive/not enough)
  if (aDelta > 300.0 && (now - lastTrigger > COOLDOWN)) {
    Serial.printf("\n[MOTION] Alert! Delta: %.2f\n", aDelta);
    sendPacket(2, aDelta);
    lastTrigger = now;
  }

  // C. HALL EFFECT CHECK (Inverted)
  int currentHall = analogRead(HALL_PIN);
  int hDelta = abs(currentHall - hallBaseline);

  if (hDelta > 200 && (now - lastTrigger > COOLDOWN)) {
    Serial.printf("\n[SECURITY] Magnet REMOVED! Delta: %d\n", hDelta);
    sendPacket(3, (float)hDelta);
    lastTrigger = now;
  }

  // D. BUTTON
  if (digitalRead(BUTTON_PIN) == LOW) {
    sendPacket(1, 1.0);
    delay(200);
  }

  delay(20);
}
