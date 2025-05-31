#include <Wire.h>
#include <RDA5807.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RDA5807 radio;

const uint8_t buttonUp = 2;
const uint8_t buttonDown = 3;
uint16_t freq = 8800;
uint16_t lastFreq = 0xFFFF;
const uint8_t volume = 15; // Max volume

unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 200;

void setup() {
  pinMode(buttonUp, INPUT_PULLUP);
  pinMode(buttonDown, INPUT_PULLUP);

  Serial.begin(9600);
  Wire.begin();

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("FM Radio");
  display.display();
  delay(1000);

  // Radio Init
  radio.setup();
  radio.setVolume(volume);
  radio.setFrequency(freq);
  updateDisplay();
}

void loop() {
  static bool lastUpState = HIGH;
  static bool lastDownState = HIGH;
  bool upState = digitalRead(buttonUp);
  bool downState = digitalRead(buttonDown);

  if ((millis() - lastDebounce) > debounceDelay) {
    if (upState == LOW && lastUpState == HIGH) {
      freq += 10; // +0.1 MHz
      if (freq > 10800) freq = 10800;
      updateRadio();
      lastDebounce = millis();
    }
    if (downState == LOW && lastDownState == HIGH) {
      if (freq >= 8510) {
        freq -= 10;
      } else {
        freq = 8500;
      }
      updateRadio();
      lastDebounce = millis();
    }
  }
  lastUpState = upState;
  lastDownState = downState;
}

void updateRadio() {
  if (freq != lastFreq) {
    radio.setFrequency(freq);
    lastFreq = freq;
    updateDisplay();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Tuned to:");
  display.setTextSize(3);
  display.setCursor(0, 20);
  display.print(freq / 100.0, 1);
  display.println(" MHz");
  display.display();
}