#include "WiFiManager.h"

#include <Arduino.h>

namespace firmware {

void WiFiManager::configure(const WiFiCredentials& primary,
                            const std::optional<WiFiCredentials>& secondary) {
  primary_ = primary;
  secondary_ = secondary;
  configured_ = true;
}

void WiFiManager::begin() {
  if (!configured_) {
    Serial.println("[WiFi] begin() called before configure()");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  connect(primary_);
}

void WiFiManager::loop() {
  if (!configured_) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    attempt_count_ = 0;
    return;
  }

  const unsigned long now = millis();
  if (now - last_attempt_ms_ < 5000) {
    return;
  }
  last_attempt_ms_ = now;

  if (attempt_count_ == 0) {
    connect(primary_);
  } else if (secondary_ && (attempt_count_ % 3 == 0)) {
    connect(*secondary_);
  } else {
    connect(primary_);
  }

  attempt_count_ = (attempt_count_ + 1) % 6;
}

bool WiFiManager::ensureConnected(uint32_t timeout_ms) {
  const unsigned long start = millis();
  while ((millis() - start) < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::disconnect() {
  WiFi.disconnect(true, true);
}

bool WiFiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiManager::ip() const {
  return WiFi.localIP();
}

int32_t WiFiManager::rssi() const {
  return WiFi.RSSI();
}

void WiFiManager::connect(const WiFiCredentials& credentials) {
  Serial.printf("[WiFi] Connecting to %s\n", credentials.ssid.c_str());
  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
}

}  // namespace firmware
