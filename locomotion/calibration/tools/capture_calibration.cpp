#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "locomotion/calibration/CalibrationPipeline.h"
#include "locomotion/calibration/CalibrationSession.h"

using namespace locomotion::calibration;

namespace {

CalibrationConfig loadConfigFromJson(const std::filesystem::path& path) {
  CalibrationConfig config;

  if (!std::filesystem::exists(path)) {
    std::cerr << "[WARN] Config file " << path << " not found. Using defaults.\n";
    return config;
  }

  nlohmann::json j;
  std::ifstream ifs(path);
  ifs >> j;

  auto load_int = [&j](const char* key, int& dst) {
    if (j.contains(key)) {
      dst = j[key].get<int>();
    }
  };
  auto load_double = [&j](const char* key, double& dst) {
    if (j.contains(key)) {
      dst = j[key].get<double>();
    }
  };
  auto load_string = [&j](const char* key, std::string& dst) {
    if (j.contains(key)) {
      dst = j[key].get<std::string>();
    }
  };

  load_int("color_width", config.color_width);
  load_int("color_height", config.color_height);
  load_int("depth_width", config.depth_width);
  load_int("depth_height", config.depth_height);
  load_int("fps", config.fps);
  load_int("charuco_squares_x", config.charuco_squares_x);
  load_int("charuco_squares_y", config.charuco_squares_y);
  load_double("charuco_square_length_mm", config.charuco_square_length_mm);
  load_double("charuco_marker_length_mm", config.charuco_marker_length_mm);
  load_int("min_charuco_corners", config.min_charuco_corners);
  load_double("homography_ransac_thresh_px", config.homography_ransac_thresh_px);
  load_double("max_reprojection_error_id", config.max_reprojection_error_id);
  load_double("floor_inlier_threshold_mm", config.floor_inlier_threshold_mm);
  load_int("floor_ransac_iterations", config.floor_ransac_iterations);
  load_string("aruco_dictionary", config.aruco_dictionary);
  load_string("playmat_layout_path", config.playmat_layout_path);
  load_string("board_mount_label", config.board_mount_label);
  load_string("log_level", config.log_level);

  return config;
}

SessionConfig makeSessionConfig(const nlohmann::json& j) {
  SessionConfig config;
  if (j.contains("session_attempts")) {
    config.attempts = j["session_attempts"].get<int>();
  }
  if (j.contains("max_plane_std_mm")) {
    config.max_plane_std_mm = j["max_plane_std_mm"].get<double>();
  }
  if (j.contains("min_inlier_ratio")) {
    config.min_inlier_ratio = j["min_inlier_ratio"].get<double>();
  }
  if (j.contains("save_intermediate_snapshots")) {
    config.save_intermediate_snapshots = j["save_intermediate_snapshots"].get<bool>();
  }
  if (j.contains("snapshot_output_dir")) {
    config.snapshot_output_dir = j["snapshot_output_dir"].get<std::string>();
  }
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path config_path = "calibration_config.json";
  std::filesystem::path output_path = "calib_result.json";

  if (argc > 1) {
    config_path = argv[1];
  }
  if (argc > 2) {
    output_path = argv[2];
  }

  CalibrationConfig calib_config = loadConfigFromJson(config_path);

  nlohmann::json config_json;
  if (std::filesystem::exists(config_path)) {
    std::ifstream ifs(config_path);
    ifs >> config_json;
  }
  SessionConfig session_config = makeSessionConfig(config_json);

  CalibrationPipeline pipeline(calib_config);
  CalibrationSession session(std::move(pipeline), session_config);

  auto result = session.Run();
  if (!result) {
    std::cerr << "[ERROR] Calibration failed.\n";
    return 1;
  }

  if (!session.SaveResultJson(*result, output_path.string())) {
    std::cerr << "[ERROR] Failed to write calibration result.\n";
    return 1;
  }

  std::cout << "[INFO] Calibration completed. Result saved to " << output_path << "\n";
  return 0;
}
