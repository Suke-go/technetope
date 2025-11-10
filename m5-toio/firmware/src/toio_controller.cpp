#include "toio_controller.h"

#include <algorithm>

ToioController::InitStatus ToioController::scanTargets(
    const std::string& target_fragment, uint32_t scan_duration_sec,
    ToioCore** out_target) {
  scan_duration_sec_ = scan_duration_sec;

  auto cores = scan(scan_duration_sec_);
  if (cores.empty()) {
    if (out_target) {
      *out_target = nullptr;
    }
    return InitStatus::kNoCubeFound;
  }

  ToioCore* target = pickTarget(cores, target_fragment);
  if (!target) {
    if (out_target) {
      *out_target = nullptr;
    }
    return InitStatus::kTargetNotFound;
  }

  if (out_target) {
    *out_target = target;
  }
  return InitStatus::kReady;
}

ToioController::InitStatus ToioController::connectAndConfigure(
    ToioCore* target_core) {
  if (!target_core) {
    return InitStatus::kInvalidArgument;
  }

  InitStatus status = connectCore(target_core);
  if (status != InitStatus::kReady) {
    return status;
  }

  configureCore(target_core);
  return InitStatus::kReady;
}

void ToioController::loop() { toio_.loop(); }

bool ToioController::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!active_core_) {
    return false;
  }
  active_core_->turnOnLed(r, g, b);
  led_color_.r = r;
  led_color_.g = g;
  led_color_.b = b;
  return true;
}

bool ToioController::driveMotor(bool ldir, uint8_t lspeed, bool rdir,
                                uint8_t rspeed) {
  if (!active_core_) {
    return false;
  }
  active_core_->controlMotor(ldir, lspeed, rdir, rspeed);
  return true;
}

std::vector<ToioCore*> ToioController::scan(uint32_t duration_sec) {
  last_scan_results_ = toio_.scan(duration_sec);
  return last_scan_results_;
}

ToioCore* ToioController::pickTarget(
    const std::vector<ToioCore*>& cores, const std::string& fragment) const {
  if (cores.empty()) {
    return nullptr;
  }
  if (fragment.empty()) {
    return cores.front();
  }
  for (auto* core : cores) {
    const std::string& name = core->getName();
    if (name.find(fragment) != std::string::npos) {
      return core;
    }
  }
  return nullptr;
}

ToioController::InitStatus ToioController::connectCore(ToioCore* core) {
  if (!core) {
    return InitStatus::kInvalidArgument;
  }
  if (!core->connect()) {
    return InitStatus::kConnectionFailed;
  }
  return InitStatus::kReady;
}

void ToioController::configureCore(ToioCore* core) {
  core->setIDnotificationSettings(/*minimum_interval=*/5, /*condition=*/0x01);
  core->setIDmissedNotificationSettings(/*sensitivity=*/10);
  core->onIDReaderData([this](ToioCoreIDData data) { handleIdData(data); });
  core->onBattery([this](uint8_t level) { handleBatteryLevel(level); });

  active_core_ = core;
  handleBatteryLevel(core->getBatteryLevel());
  handleIdData(core->getIDReaderData());
}

void ToioController::handleIdData(const ToioCoreIDData& data) {
  if (data.type == ToioCoreIDTypePosition) {
    pose_.x = data.position.cubePosX;
    pose_.y = data.position.cubePosY;
    pose_.angle = data.position.cubeAngleDegree;
    pose_.on_mat = true;
    has_pose_ = true;
  } else {
    pose_.x = 0;
    pose_.y = 0;
    pose_.angle = 0;
    pose_.on_mat = false;
    has_pose_ = false;
  }
  pose_dirty_ = true;
  pose_updated_ms_ = millis();
}

void ToioController::handleBatteryLevel(uint8_t level) {
  battery_level_ = level;
  has_battery_ = true;
  battery_dirty_ = true;
  battery_updated_ms_ = millis();
}
