#include "locomotion/calibration/CalibrationSession.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace locomotion::calibration {

CalibrationSession::CalibrationSession(CalibrationPipeline pipeline,
                                       SessionConfig session_config)
    : pipeline_(std::move(pipeline)), session_config_(std::move(session_config)) {}

std::optional<CalibrationResult> CalibrationSession::Run() {
  if (!pipeline_.initialize()) {
    spdlog::error("Failed to initialize CalibrationPipeline.");
    return std::nullopt;
  }

  std::optional<CalibrationResult> best;
  int successes = 0;

  for (int attempt = 0; attempt < session_config_.attempts; ++attempt) {
    auto snapshot = pipeline_.runOnce();
    if (!snapshot) {
      spdlog::info("Attempt {}: ChArUco detection failed.", attempt + 1);
      continue;
    }

    if (snapshot->reprojection_error > pipeline_.config().max_reprojection_error_id) {
      spdlog::warn("Attempt {}: reprojection error {:.3f} exceeds threshold {:.3f}.",
                   attempt + 1, snapshot->reprojection_error,
                   pipeline_.config().max_reprojection_error_id);
      continue;
    }

    if (pipeline_.config().enable_floor_plane_fit &&
        snapshot->floor_plane_std_mm > session_config_.max_plane_std_mm) {
      spdlog::warn("Attempt {}: plane std {:.3f} exceeds threshold {:.3f}.", attempt + 1,
                   snapshot->floor_plane_std_mm, session_config_.max_plane_std_mm);
      continue;
    }

    if (pipeline_.config().enable_floor_plane_fit &&
        snapshot->inlier_ratio < session_config_.min_inlier_ratio) {
      spdlog::warn("Attempt {}: inlier ratio {:.3f} below minimum {:.3f}.", attempt + 1,
                   snapshot->inlier_ratio, session_config_.min_inlier_ratio);
      continue;
    }

    CalibrationResult result = ToResult(*snapshot);
    if (!best || result.timestamp > best->timestamp) {
      best = result;
    }
    ++successes;
  }

  if (!best) {
    spdlog::error("CalibrationSession failed. No valid snapshots collected out of {} attempts.",
                  session_config_.attempts);
    return std::nullopt;
  }

  spdlog::info("CalibrationSession succeeded with {} valid snapshots.", successes);
  return best;
}

CalibrationResult CalibrationSession::ToResult(const CalibrationSnapshot& snapshot) {
  CalibrationResult result;
  result.homography = snapshot.homography_color_to_position.clone();
  result.floor_plane = snapshot.floor_plane;
  result.reprojection_error = snapshot.reprojection_error;
  result.floor_plane_std_mm = snapshot.floor_plane_std_mm;
  result.inlier_ratio = snapshot.inlier_ratio;
  result.detected_charuco_corners = snapshot.detected_charuco_corners;
  result.timestamp = snapshot.timestamp;
  return result;
}

bool CalibrationSession::SaveResultJson(const CalibrationResult& result,
                                        const std::string& path) const {
  nlohmann::json j;
  j["schema_version"] = 1;
  j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                       result.timestamp.time_since_epoch())
                       .count();
  j["reprojection_error_id"] = result.reprojection_error;
  j["floor_plane"] = {result.floor_plane[0], result.floor_plane[1],
                      result.floor_plane[2], result.floor_plane[3]};
  j["floor_plane_std_mm"] = result.floor_plane_std_mm;
  j["inlier_ratio"] = result.inlier_ratio;
  j["detected_charuco_corners"] = result.detected_charuco_corners;

  auto& h = j["homography_color_to_position"];
  h = nlohmann::json::array();
  for (int r = 0; r < result.homography.rows; ++r) {
    nlohmann::json row = nlohmann::json::array();
    for (int c = 0; c < result.homography.cols; ++c) {
      row.push_back(result.homography.at<double>(r, c));
    }
    h.push_back(row);
  }

  try {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    ofs << j.dump(2);
    spdlog::info("Calibration result saved to {}", path);
    return true;
  } catch (const std::exception& ex) {
    spdlog::error("Failed to save calibration result to {}: {}", path, ex.what());
    return false;
  }
}

}  // namespace locomotion::calibration
