#include <WiFi.h>           // WiFi library for ESP8266/ESP32
#include <WiFiUdp.h>        // UDP library
#include <SoftwareSerial.h>  // SoftwareSerial for RFID
#include <ESPAsyncWebServer.h> // AsyncWebServer library
#include <ArduinoJson.h>    // JSON library for API responses

// Definition of RFID card structure
struct RfidCard {
  String id;
  String owner;
};

// Definition of access history structure
struct AccessEntry {
  String cardId;
  String owner;
  String timestamp;
  bool authorized;
};

// Configuration
const int MAX_RFID_CARDS = 50;     // Maximum number of RFID cards
const int MAX_HISTORY_ENTRIES = 10000; // Maximum number of history entries
RfidCard rfidList[MAX_RFID_CARDS] = {
  {"code", "name"}
};
int rfidListSize = 10; // Current number of RFID cards

AccessEntry accessHistory[MAX_HISTORY_ENTRIES];
int historySize = 0; // Current number of history entries

// WiFi credentials
const char* ssid = "ssid";
const char* password = "password";

// Static IP configuration
IPAddress local_IP(255, 255, 255, 255);
IPAddress gateway(255, 255, 255, 255);
IPAddress subnet(255, 255, 255, 255);
IPAddress primaryDNS(1, 1, 1, 1); // Optional: primary DNS
IPAddress secondaryDNS(8, 8, 8, 8);   // Optional: secondary DNS (Google DNS)

// UDP configuration
const char* udpAddress2 = "255.255.255.255";
const int udpPort2 = 'port-number';
WiFiUDP udp;

// RFID and relay setup
SoftwareSerial RFID(5, 4); // RX, TX
const int relayPin = 27;   // Relay pin
const int ledPin = 15;     // LED pin

// Web server on port 80
AsyncWebServer server(80);

// Variables for RFID and relay
String text;
char c;
unsigned long relayStartTime = 0;
bool relayActive = false;

// Variable to track if IP was displayed
bool ipDisplayed = false;

void setup() {
  Serial.begin(9600);
  RFID.begin(9600);

  pinMode(relayPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, LOW);

  // Configure static IP
  Serial.println("Konfiguracja statycznego IP: " + local_IP.toString());
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Błąd konfiguracji statycznego IP!");
  }

  // Connect to WiFi with timeout
  Serial.println("Łączenie z WiFi: " + String(ssid));
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 30000; // 30 seconds timeout

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(1000);
    Serial.print("Łączę... Status: ");
    Serial.println(WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Połączono z WiFi");
    Serial.print("Adres IP: ");
    Serial.println(WiFi.localIP());
    ipDisplayed = true;
    setupWebServer();
  } else {
    Serial.println("Nie udało się połączyć z WiFi w ciągu 30 sekund. Kontynuuję bez WiFi.");
  }

  Serial.println("Przybliż kartę RFID...");
}

void loop() {
  // Check WiFi status and display IP if connected and not yet displayed
  if (WiFi.status() == WL_CONNECTED && !ipDisplayed) {
    Serial.println("Połączono z WiFi");
    Serial.print("Adres IP: ");
    Serial.println(WiFi.localIP());
    ipDisplayed = true;
    setupWebServer();
  }

  // Read RFID data
  while (RFID.available() > 0) {
    delay(5);
    c = RFID.read();
    text += c;
  }

  if (text.length() > 20) {
    check();
    text = "";
  }

  // Handle relay and LED
  if (relayActive && (millis() - relayStartTime > 5000)) {
    digitalWrite(relayPin, LOW);
    digitalWrite(ledPin, LOW);
    relayActive = false;
  }
}

void check() {
  text = text.substring(1, 11);
  text.trim();
  Serial.println("ID karty: " + text);

  bool authorized = false;
  String owner;

  for (int i = 0; i < rfidListSize; i++) {
    if (rfidList[i].id == text) {
      Serial.println("Dostęp zaakceptowany dla: " + rfidList[i].owner);
      authorized = true;
      owner = rfidList[i].owner;
      break;
    }
  }

  // Record access attempt
  addAccessEntry(text, owner, authorized);

  if (authorized) {
    digitalWrite(relayPin, HIGH);
    digitalWrite(ledPin, HIGH);
    relayStartTime = millis();
    relayActive = true;
    sendAuthorizationInfo(owner, text);
  } else {
    Serial.println("Dostęp odrzucony");
  }

  Serial.println("Przybliż kartę RFID...");
}

void sendAuthorizationInfo(String owner, String cardId) {
  String message = "Autoryzacja: " + owner + ", ID: " + cardId;
  udp.beginPacket(udpAddress2, udpPort2);
  udp.print(message);
  udp.endPacket();
  Serial.println("Wysłano informacje o autoryzacji: " + message);
}

void addAccessEntry(String cardId, String owner, bool authorized) {
  if (historySize < MAX_HISTORY_ENTRIES) {
    accessHistory[historySize].cardId = cardId;
    accessHistory[historySize].owner = owner;
    accessHistory[historySize].authorized = authorized;
    accessHistory[historySize].timestamp = getCurrentTime();
    historySize++;
  } else {
    // Shift entries and add new one
    for (int i = 1; i < MAX_HISTORY_ENTRIES; i++) {
      accessHistory[i - 1] = accessHistory[i];
    }
    accessHistory[MAX_HISTORY_ENTRIES - 1].cardId = cardId;
    accessHistory[MAX_HISTORY_ENTRIES - 1].owner = owner;
    accessHistory[MAX_HISTORY_ENTRIES - 1].authorized = authorized;
    accessHistory[MAX_HISTORY_ENTRIES - 1].timestamp = getCurrentTime();
  }
}

String getCurrentTime() {
  // Simple timestamp based on millis (replace with NTP for real time)
  unsigned long seconds = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           2025, 4, 22, (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
  return String(buf);
}

void setupWebServer() {
  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<title>ESP32 RFID Management</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "table { border-collapse: collapse; width: 100%; }";
    html += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
    html += "th { background-color: #f2f2f2; }";
    html += "h2 { color: #333; }";
    html += ".form-container { margin: 20px 0; }";
    html += "input, button { padding: 5px; margin: 5px; }";
    html += "</style></head><body>";
    html += "<h2>ESP32 RFID Management</h2>";

    // Access history
    html += "<h3>Access History</h3>";
    html += "<table><tr><th>Timestamp</th><th>Card ID</th><th>Owner</th><th>Status</th></tr>";
    for (int i = historySize - 1; i >= 0; i--) {
      html += "<tr>";
      html += "<td>" + accessHistory[i].timestamp + "</td>";
      html += "<td>" + accessHistory[i].cardId + "</td>";
      html += "<td>" + accessHistory[i].owner + "</td>";
      html += "<td>" + String(accessHistory[i].authorized ? "Authorized" : "Denied") + "</td>";
      html += "</tr>";
    }
    html += "</table>";

    // RFID list
    html += "<h3>Authorized RFID Cards</h3>";
    html += "<table><tr><th>Card ID</th><th>Owner</th><th>Action</th></tr>";
    for (int i = 0; i < rfidListSize; i++) {
      html += "<tr>";
      html += "<td>" + rfidList[i].id + "</td>";
      html += "<td>" + rfidList[i].owner + "</td>";
      html += "<td><a href='/delete?index=" + String(i) + "'>Delete</a></td>";
      html += "</tr>";
    }
    html += "</table>";

    // Add new RFID card form
    html += "<h3>Add New RFID Card</h3>";
    html += "<div class='form-container'>";
    html += "<form action='/add' method='POST'>";
    html += "Card ID: <input type='text' name='cardId' required><br>";
    html += "Owner: <input type='text' name='owner' required><br>";
    html += "<button type='submit'>Add Card</button>";
    html += "</form></div>";

    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Handle adding new RFID card
  server.on("/add", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (rfidListSize < MAX_RFID_CARDS && request->hasParam("cardId", true) && request->hasParam("owner", true)) {
      String cardId = request->getParam("cardId", true)->value();
      String owner = request->getParam("owner", true)->value();
      cardId.trim();
      owner.trim();
      if (cardId.length() > 0 && owner.length() > 0) {
        rfidList[rfidListSize].id = cardId;
        rfidList[rfidListSize].owner = owner;
        rfidListSize++;
      }
    }
    request->redirect("/");
  });

  // Handle deleting RFID card
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("index")) {
      int index = request->getParam("index")->value().toInt();
      if (index >= 0 && index < rfidListSize) {
        for (int i = index; i < rfidListSize - 1; i++) {
          rfidList[i] = rfidList[i + 1];
        }
        rfidListSize--;
      }
    }
    request->redirect("/");
  });

  // API endpoint for RFID list
  server.on("/api/rfid", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    JsonArray cards = doc.createNestedArray("cards");
    for (int i = 0; i < rfidListSize; i++) {
      JsonObject card = cards.createNestedObject();
      card["id"] = rfidList[i].id;
      card["owner"] = rfidList[i].owner;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API endpoint for access history
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray entries = doc.createNestedArray("entries");
    for (int i = 0; i < historySize; i++) {
      JsonObject entry = entries.createNestedObject();
      entry["timestamp"] = accessHistory[i].timestamp;
      entry["cardId"] = accessHistory[i].cardId;
      entry["owner"] = accessHistory[i].owner;
      entry["authorized"] = accessHistory[i].authorized;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // Start server
  server.begin();
  Serial.println("Serwer HTTP uruchomiony");
}
