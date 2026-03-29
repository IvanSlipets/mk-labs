#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
 
//  WI-FI ACCESSNPOINT SETTINGS
const char* ssid = "ESP_Labs"; 
const char* password = "1234567890"; 

ESP8266WebServer server(80);

//  PINS 
const uint8_t LED_PINS[] = { 14, 0, 13 };
const uint8_t BUTTON_PIN = 15;

//   VARIABLES FOR BUTTON AND DEBOUNCE
volatile uint32_t lastInterruptTime = 0;
const uint8_t debounceDelay = 50; 
volatile bool buttonTriggered = false;

//   VARIABLES FOR BLINKING
uint8_t currentStep = 0;
uint32_t lastLedTime = 0;
bool isSlowingDown = false; 

uint16_t currentInterval = 300; 
const uint8_t intervalStep = 150;
const uint16_t maxInterval = 1500;

//   BUTTON LOGIC

// Debounce
void checkButton() {
  uint32_t currentTime = millis();
  if (currentTime - lastInterruptTime > debounceDelay) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      buttonTriggered = true;
    }
    lastInterruptTime = currentTime;
  }
}

//  (ISR)
void IRAM_ATTR handleButtonInterrupt() {
  checkButton();
}

//   WEB-SERVER

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP8266 LED Control</title>
  <style>
    body { text-align: center; font-family: sans-serif; margin-top: 50px; background-color: #1e1e1e; color: #fff;}
    .btn { background-color: #007BFF; color: white; padding: 25px 50px; font-size: 24px; border: none; border-radius: 12px; cursor: pointer;}
  </style>
</head>
<body>
  <h2>Algorithm Changer</h2>
  <button class="btn" onclick="fetch('/toggle')">Change Algorithm!</button>
</body>
</html>
)=====";

void handleRoot() {
  server.send(200, "text/html", MAIN_page);
}

void handleToggle() {
  buttonTriggered = true;
  server.send(200, "text/plain", "OK");
}


void processModeChange() {
  if (buttonTriggered) {
    isSlowingDown = !isSlowingDown;
    currentInterval = 300;
    currentStep = 0;
    
    for (uint8_t i = 0; i < 3; i++) {
      digitalWrite(LED_PINS[i], LOW);
    }
    
    buttonTriggered = false;
    Serial.println(isSlowingDown ? "Mode: Slowdown" : "Mode: Constant");
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

    for (uint8_t i = 0; i < 3; i++) {
      digitalWrite(LED_PINS[i], LOW);
    }

    digitalWrite(LED_PINS[currentStep], HIGH);
    currentStep = (currentStep + 1) % 3;
  }
}


void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
  }

  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.begin();
}

void loop() {
  server.handleClient();
  processModeChange();
  updateAnimation();
}