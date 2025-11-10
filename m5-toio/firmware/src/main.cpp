#include <M5Unified.h>
#include <M5GFX.h>
#include <M5StickCPlus2.h>

namespace {
constexpr int kBuiltInLedPin = 10;
constexpr uint32_t kBlinkIntervalMs = 500;
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  pinMode(kBuiltInLedPin, OUTPUT);

  auto &display = M5.Display;
  display.setBrightness(128);
  display.setRotation(3);
  display.fillScreen(BLACK);
  display.setTextColor(WHITE, BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextSize(2);
  display.drawString("StickC Plus2 Ready", display.width() / 2, 35);
  display.setTextSize(1);
  display.drawString("BtnA: toggle text", display.width() / 2, 55);
}

void loop() {
  static uint32_t lastBlink = 0;
  static bool ledOn = false;
  static bool showStatus = true;

  M5.update();

  if (millis() - lastBlink >= kBlinkIntervalMs) {
    ledOn = !ledOn;
    digitalWrite(kBuiltInLedPin, ledOn);
    lastBlink = millis();
  }

  if (M5.BtnA.wasPressed()) {
    showStatus = !showStatus;
    M5.Display.fillRect(0, 70, M5.Display.width(), 32, BLACK);
  }

  if (showStatus) {
    M5.Display.setCursor(10, 75);
    M5.Display.printf("LED: %s   \n", ledOn ? "ON " : "OFF");
    M5.Display.printf("Uptime: %lus   ",
                      static_cast<unsigned long>(millis() / 1000));
  }

  delay(10);
}
