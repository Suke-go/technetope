#include "HeartbeatPublisher.h"

#include <Arduino.h>
#include <cstring>

namespace firmware {

HeartbeatPublisher::HeartbeatPublisher() = default;

void HeartbeatPublisher::configure(const std::string& host, uint16_t port) {
  remote_host_ = host;
  remote_port_ = port;
}

void HeartbeatPublisher::begin() { udp_.begin(0); }

void HeartbeatPublisher::loop(const WiFiManager& wifi, const NtpClient& ntp,
                              const PlaybackQueue& queue,
                              const AudioPlayer& player) {
  const unsigned long now = millis();
  if (now - last_send_ms_ < 1000) {
    return;
  }
  last_send_ms_ = now;

  if (!wifi.isConnected() || remote_host_.empty() || remote_port_ == 0) {
    return;
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           R"({"ip":"%s","rssi":%d,"synced":%s,"queue":%u,"playing":%s})",
           wifi.ip().toString().c_str(), wifi.rssi(),
           ntp.isSynced() ? "true" : "false",
           static_cast<unsigned>(queue.size()),
           player.isPlaying() ? "true" : "false");
  udp_.beginPacket(remote_host_.c_str(), remote_port_);
  udp_.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
  udp_.endPacket();
}

}  // namespace firmware
