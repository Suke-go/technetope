#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace locomotion::calibration {

struct CalibrationResult {
  cv::Mat homography;
  cv::Vec4f floor_plane;
  double reprojection_error{0.0};
  double floor_plane_std_mm{0.0};
  double inlier_ratio{0.0};
  int detected_charuco_corners{0};
  std::chrono::system_clock::time_point timestamp;
};

}  // namespace locomotion::calibration
