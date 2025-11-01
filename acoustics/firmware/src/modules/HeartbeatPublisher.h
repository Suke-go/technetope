#pragma once

#include <WiFiUdp.h>

#include <string>

#include "AudioPlayer.h"
#include "NtpClient.h"
#include "PlaybackQueue.h"
#include "WiFiManager.h"

namespace firmware {

class HeartbeatPublisher {
 public:
  HeartbeatPublisher();

  void configure(const std::string& host, uint16_t port);
  void begin();
  void loop(const WiFiManager& wifi, const NtpClient& ntp,
            const PlaybackQueue& queue, const AudioPlayer& player);

 private:
  WiFiUDP udp_;
  std::string remote_host_;
  uint16_t remote_port_ = 0;
  unsigned long last_send_ms_ = 0;
};

}  // namespace firmware
