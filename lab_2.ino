#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// ====================== UART ======================
#define RX_PIN 5   // D1 = RX
#define TX_PIN 4   // D2 = TX
SoftwareSerial uart(RX_PIN, TX_PIN);

// Parameters
const uint32_t UART_BAUD = 9600;
const char PARTNER_COMMAND = 'M';   // 0x4D

// ====================== WI-FI ======================
const char* ssid = "ESP_Labs";
const char* password = "1234567890";

ESP8266WebServer server(80);

// ====================== PINS ======================
const uint8_t LED_PINS[] = {14, 0, 13};   // D5, D3, D7
const uint8_t BUTTON_PIN = 15;            // D8 — Local button
const uint8_t PARTNER_BUTTON_PIN = 12;     // D6 — Partner command button

// ====================== VARIABLES ======================
volatile uint32_t lastInterruptTime = 0;
const uint8_t debounceDelay = 50;

volatile bool buttonTriggered = false;        // Local button flag
volatile bool partnerButtonTriggered = false; // Flag to send command to partner

uint8_t currentStep = 0;
uint32_t lastLedTime = 0;
bool isSlowingDown = false;

uint16_t currentInterval = 300;
const uint8_t intervalStep = 150;
const uint16_t maxInterval = 1500;

String currentModeStr = "Constant";
String currentLedColor = "Orange";

// ====================== INTERRUPTS ======================
void IRAM_ATTR handleButtonInterrupt() {
  uint32_t t = millis();
  if (t - lastInterruptTime > debounceDelay) {
    if (digitalRead(BUTTON_PIN) == LOW) buttonTriggered = true;
    lastInterruptTime = t;
  }
}

void IRAM_ATTR handlePartnerButtonInterrupt() {
  uint32_t t = millis();
  if (t - lastInterruptTime > debounceDelay) {
    if (digitalRead(PARTNER_BUTTON_PIN) == LOW) partnerButtonTriggered = true;
    lastInterruptTime = t;
  }
}

// ====================== UART ======================
void sendPartnerCommand() {
  char dataToSend = PARTNER_COMMAND & 0x1F; // 5 least significant bits
  uart.write(dataToSend);
  uart.flush(); 
  Serial.println("→ Sent command 'M' (one-time)");
}

void checkUart() {
  if (uart.available()) {
    char c = uart.read();
    Serial.printf("Received from partner: 0x%02X\n", c);
    
    if ((c & 0x1F) == (PARTNER_COMMAND & 0x1F)) {
      while(uart.available()) uart.read(); // Clear buffer (duplicates)

      buttonTriggered = true; 
      Serial.println("Command recognized! Mode changed.");
    }
  }
}

// ====================== WEB SERVER ======================
const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP8266 Lab 13</title>
  <style>
    body { 
      text-align: center; 
      font-family: sans-serif; 
      margin-top: 30px; 
      background: #1e1e1e; 
      color: #fff; 
    }
    .status { 
      font-size: 28px; 
      margin: 20px auto; 
      padding: 20px; 
      background: #333; 
      border-radius: 12px; 
      width: 90%; 
      max-width: 500px;
    }
    .led { 
      font-size: 32px; 
      font-weight: bold; 
      margin: 10px;
    }
    .btn { 
      padding: 18px 40px; 
      font-size: 22px; 
      margin: 12px; 
      border: none; 
      border-radius: 12px; 
      cursor: pointer; 
    }
    .btn1 { background: #007BFF; color: white; }
    .btn2 { background: #FF5722; color: white; }
  </style>
</head>
<body>
  <div id="status" class="status">
    Mode: Constant | LED: <span id="led" class="led">Orange</span>
  </div>
  
  <button class="btn btn1" onclick="fetch('/toggleMe')">Change Local Algorithm</button><br>
  <button class="btn btn2" onclick="fetch('/togglePartner')">Change Partner Algorithm</button>

  <script>
    function updateStatus() {
      fetch('/status')
        .then(r => r.text())
        .then(txt => {
          document.getElementById('status').innerHTML = txt;
        })
        .catch(() => {});
    }

    // Refresh every 150ms to keep up with the animation
    setInterval(updateStatus, 150);
    updateStatus(); 
  </script>
</body>
</html>
)=====";

void handleRoot() { server.send(200, "text/html", MAIN_page); }
void handleToggleMe() { buttonTriggered = true; server.send(200, "text/plain", "OK"); }
void handleTogglePartner() { partnerButtonTriggered = true; server.send(200, "text/plain", "OK"); }

void handleStatus() {
  String resp = "Mode: " + currentModeStr + 
                " | LED: <span class='led'>" + currentLedColor + "</span>";
  server.send(200, "text/plain", resp);
}

// ====================== LOGIC ======================
void processModeChange() {
  if (buttonTriggered) {
    isSlowingDown = !isSlowingDown;
    currentInterval = 300;
    currentStep = 0;
    for (uint8_t i = 0; i < 3; i++) digitalWrite(LED_PINS[i], LOW);

    buttonTriggered = false;
    currentModeStr = isSlowingDown ? "Slowdown" : "Constant";
    Serial.println(isSlowingDown ? "=== Mode: Slowdown ===" : "=== Mode: Constant ===");
  }
}

void processPartnerCommand() {
  if (partnerButtonTriggered) {
    sendPartnerCommand();
    partnerButtonTriggered = false;
  }
}

void updateAnimation() {
  if (millis() - lastLedTime >= currentInterval) {
    lastLedTime = millis();

    if (isSlowingDown && currentInterval < maxInterval) {
      currentInterval += intervalStep;
    } else if (!isSlowingDown) {
      currentInterval = 300;
    }

    for (uint8_t i = 0; i < 3; i++) digitalWrite(LED_PINS[i], LOW);
    digitalWrite(LED_PINS[currentStep], HIGH);

    const char* colors[] = {"Orange", "Red", "Green"};
    currentLedColor = colors[currentStep];

    currentStep = (currentStep + 1) % 3;
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  pinMode(BUTTON_PIN, INPUT);
  pinMode(PARTNER_BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(PARTNER_BUTTON_PIN), handlePartnerButtonInterrupt, FALLING);

  // UART Setup for Option 13: 5 Data bits, Odd parity, 2 Stop bits
  uart.begin(UART_BAUD, SWSERIAL_5O2);
  Serial.printf("UART started: %d baud, 5 data, odd parity, 2 stop bits\n", UART_BAUD);

  // WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/toggleMe", handleToggleMe);
  server.on("/togglePartner", handleTogglePartner);
  server.on("/status", handleStatus);
  server.begin();

  Serial.println("Web server started → http://192.168.4.1");  
}

// ====================== LOOP ======================
void loop() {
  server.handleClient();
  
  processModeChange();
  processPartnerCommand();
  updateAnimation();
  checkUart();
}