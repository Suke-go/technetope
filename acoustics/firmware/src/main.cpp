#include <M5StickCPlus2.h>
#include <SPIFFS.h>

#include <optional>
#include <cstring>

#include "modules/AudioPlayer.h"
#include "modules/HeartbeatPublisher.h"
#include "modules/NtpClient.h"
#include "modules/OscReceiver.h"
#include "modules/PlaybackQueue.h"
#include "modules/PresetStore.h"
#include "modules/WiFiManager.h"

#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

namespace firmware {

WiFiManager wifi_manager;
NtpClient ntp_client("pool.ntp.org", 0, 60000);
PresetStore preset_store;
PlaybackQueue playback_queue;
AudioPlayer audio_player;
OscReceiver osc_receiver;
HeartbeatPublisher heartbeat;

TaskHandle_t wifi_task_handle = nullptr;
TaskHandle_t ntp_task_handle = nullptr;
TaskHandle_t osc_task_handle = nullptr;
TaskHandle_t playback_task_handle = nullptr;
TaskHandle_t heartbeat_task_handle = nullptr;

void wifiTask(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    wifi_manager.loop();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void ntpTask(void* pvParameters) {
  (void)pvParameters;
  bool initial_sync_done = false;
  for (;;) {
    if (!wifi_manager.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!initial_sync_done) {
      initial_sync_done = ntp_client.forceSync(5000);
    } else {
      ntp_client.loop();
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
}

void oscTask(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    osc_receiver.loop(ntp_client, playback_queue, preset_store);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void playbackTask(void* pvParameters) {
  (void)pvParameters;
  std::optional<PlaybackItem> active_item;

  for (;;) {
    const uint64_t now_us = ntp_client.nowMicros();

    if (!audio_player.isPlaying()) {
      if (active_item && active_item->loop) {
        auto preset = preset_store.findById(active_item->preset_id);
        if (preset) {
          audio_player.play(*preset, active_item->gain);
        }
      } else {
        active_item.reset();
      }
    }

    auto due = playback_queue.popDue(now_us);
    if (due) {
      const auto preset = preset_store.findById(due->preset_id);
      if (!preset) {
        Serial.printf("[Playback] Missing preset for id %s\n",
                      due->preset_id.c_str());
      } else if (audio_player.play(*preset, due->gain)) {
        active_item = due;
        Serial.printf("[Playback] Started preset %s\n",
                      due->preset_id.c_str());
      }
    }

    audio_player.loop();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void heartbeatTask(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    heartbeat.loop(wifi_manager, ntp_client, playback_queue, audio_player);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace firmware

void setup() {
  using namespace firmware;

  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.external_speaker_value = 0;
  cfg.internal_spk = false;            // use external HAT speaker
  cfg.internal_mic = false;
  cfg.external_speaker.hat_spk2 = 1;   // enable SPK2 hat routing
  cfg.external_speaker.hat_spk = 0;
  cfg.external_speaker.atomic_spk = 0;
  cfg.output_power = true;

  StickCP2.begin(cfg);
  StickCP2.Power.setLed(0);

  if (!SPIFFS.begin(true)) {
    Serial.println("[Boot] Failed to mount SPIFFS.");
  }

  wifi_manager.configure(
      {secrets::WIFI_PRIMARY_SSID, secrets::WIFI_PRIMARY_PASS},
      (strlen(secrets::WIFI_SECONDARY_SSID) > 0)
          ? std::optional<WiFiCredentials>(
                WiFiCredentials{secrets::WIFI_SECONDARY_SSID,
                                secrets::WIFI_SECONDARY_PASS})
          : std::nullopt);
  wifi_manager.begin();

  audio_player.begin();

  ntp_client.begin();

  if (!preset_store.load(SPIFFS, "/manifest.json")) {
    Serial.println("[Boot] Preset manifest not loaded.");
  }

  osc_receiver.configure(secrets::OSC_LISTEN_PORT);
  osc_receiver.setCryptoKey(secrets::OSC_AES_KEY, secrets::OSC_AES_IV);
  osc_receiver.begin();

  heartbeat.configure(secrets::HEARTBEAT_REMOTE_HOST,
                      secrets::HEARTBEAT_REMOTE_PORT);
  heartbeat.begin();

  xTaskCreatePinnedToCore(wifiTask, "wifiTask", 4096, nullptr, 2,
                          &wifi_task_handle, 0);
  xTaskCreatePinnedToCore(ntpTask, "ntpTask", 4096, nullptr, 3,
                          &ntp_task_handle, 0);
  xTaskCreatePinnedToCore(oscTask, "oscTask", 6144, nullptr, 4,
                          &osc_task_handle, 1);
  xTaskCreatePinnedToCore(playbackTask, "playbackTask", 8192, nullptr, 5,
                          &playback_task_handle, 1);
  xTaskCreatePinnedToCore(heartbeatTask, "heartbeatTask", 4096, nullptr, 1,
                          &heartbeat_task_handle, 1);

  // --- Temporary test: play sample_test preset on boot ---
  if (auto preset = preset_store.findById("sample_test")) {
    audio_player.play(*preset);
  } else {
    Serial.println("[Boot] sample_test preset not found.");
  }
  // --- End of temporary test code ---
}

void loop() {
  StickCP2.update();
  delay(50);
}
