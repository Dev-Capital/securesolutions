#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "secrets.h"

// =========================
// 1. CONFIGURATION
// =========================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* wsHost   = WS_HOST;
const uint16_t wsPort = WS_PORT;
const char* wsPath   = WS_PATH;

WebSocketsClient webSocket;

typedef struct {
  char nodeName[16];
  uint32_t counter;
  int alert;
  float value;
} NodePacket;

// --- Connection Tracking ---
struct NodeStatus {
  String name;
  unsigned long lastSeen;
  bool isOnline;
};

NodeStatus registry[3] = {
  {"NODE-1", 0, false},
  {"NODE-2", 0, false},
  {"NODE-3", 0, false}
};

// =========================
// 2. RELAY LOGIC (Mapping & Connection Counting)
// =========================
void relayToWebsite(NodePacket pkt, String macAddr) {
  int connectedCount = 0;
  unsigned long now = millis();
  String targetID = "Unknown";
  String zoneName = "Unknown";

  for (int i = 0; i < 3; i++) {
    if (String(pkt.nodeName) == registry[i].name) {
      registry[i].lastSeen = now;
      registry[i].isOnline = true;
    }

    if (now - registry[i].lastSeen < 35000 && registry[i].lastSeen > 0) {
      connectedCount++;
    }
  }

  if (String(pkt.nodeName) == "NODE-1") {
    targetID = "S1";
    zoneName = "Zone Alpha";
  } else if (String(pkt.nodeName) == "NODE-2") {
    targetID = "S3";
    zoneName = "Zone Central";
  } else if (String(pkt.nodeName) == "NODE-3") {
    targetID = "S5";
    zoneName = "Zone Echo";
  }

  StaticJsonDocument<512> doc;
  doc["sensorId"] = targetID;
  doc["source"] = zoneName;
  doc["movement"] = (pkt.alert > 0);
  doc["delta"] = pkt.value;
  doc["type"] = "sensor_update";
  doc["connectedNodes"] = connectedCount;
  doc["totalProvisioned"] = 3;

  String json;
  serializeJson(doc, json);

  if (webSocket.isConnected()) {
    webSocket.sendTXT(json);
    Serial.println(">> RELAYED: " + json);
  }
}

// =========================
// 3. CALLBACKS
// =========================
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len != sizeof(NodePacket)) return;

  NodePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  relayToWebsite(pkt, "RADIO_LINK");
}

void onWSevent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("WS: Live");
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);

  webSocket.beginSSL(wsHost, wsPort, wsPath);
  webSocket.onEvent(onWSevent);
}

void loop() {
  webSocket.loop();

  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }
}
