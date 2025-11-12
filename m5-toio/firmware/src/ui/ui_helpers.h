#pragma once

#include <stdint.h>

#include "controller/toio_controller.h"

class UiHelpers {
 public:
  void Begin();
  void DrawHeader(const char* message);
  void ShowInitResult(ToioController::InitStatus status);
  void UpdateStatus(const CubePose& pose, bool has_pose, uint8_t battery_level,
                    bool has_battery, const ToioLedColor& led,
                    const ToioMotorState& motor, bool pose_dirty,
                    bool battery_dirty, uint32_t refresh_interval_ms);

 private:
  struct UiStatus {
    CubePose pose{};
    bool has_pose = false;
    uint8_t battery_level = 0;
    bool has_battery = false;
    ToioLedColor led{};
    ToioMotorState motor{};
  };

  void ShowStatus(uint32_t now_ms);

  UiStatus status_{};
  uint32_t last_display_ms_ = 0;
};
