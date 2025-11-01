#include "NtpClient.h"

#include <Arduino.h>

namespace firmware {

NtpClient::NtpClient(const char* server, long time_offset_sec,
                     unsigned long update_interval_ms)
    : client_(udp_, server, time_offset_sec, update_interval_ms) {}

void NtpClient::begin() {
  client_.begin();
}

bool NtpClient::forceSync(uint32_t timeout_ms) {
  const unsigned long start = millis();
  while ((millis() - start) < timeout_ms) {
    if (client_.forceUpdate()) {
      synced_ = true;
      last_sync_ = client_.getEpochTime();
      last_sync_millis_ = millis();
      return true;
    }
    delay(250);
  }
  return false;
}

void NtpClient::loop() {
  if (!client_.update()) {
    return;
  }
  synced_ = true;
  last_sync_ = client_.getEpochTime();
  last_sync_millis_ = millis();
}

bool NtpClient::isSynced() const { return synced_; }

uint64_t NtpClient::nowMicros() const {
  if (!synced_) {
    return static_cast<uint64_t>(millis()) * 1000ULL;
  }
  const uint64_t elapsed_ms =
      static_cast<uint64_t>(millis() - last_sync_millis_);
  return static_cast<uint64_t>(last_sync_) * 1000000ULL + elapsed_ms * 1000ULL;
}

uint32_t NtpClient::lastSyncEpoch() const { return last_sync_; }

}  // namespace firmware
