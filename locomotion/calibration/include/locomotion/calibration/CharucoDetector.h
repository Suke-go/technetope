#pragma once

#include <optional>
#include <vector>

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/core.hpp>

namespace locomotion::calibration {

struct CharucoDetectorConfig {
  int min_corners{12};
  bool enable_subpixel_refine{true};
  cv::Size subpixel_window{5, 5};
  int subpixel_max_iterations{30};
  double subpixel_epsilon{0.1};
};

struct CharucoDetectionResult {
  std::vector<cv::Point2f> image_points;
  std::vector<cv::Point3f> board_points;
  std::vector<int> ids;
  int detected_markers{0};
  int detected_charuco_corners{0};
};

class CharucoDetector {
 public:
  CharucoDetector(cv::Ptr<cv::aruco::Dictionary> dictionary,
                  cv::Ptr<cv::aruco::CharucoBoard> board,
                  CharucoDetectorConfig config = {});

  std::optional<CharucoDetectionResult> Detect(const cv::Mat& bgr_image) const;

 private:
  cv::Ptr<cv::aruco::Dictionary> dictionary_;
  cv::Ptr<cv::aruco::CharucoBoard> board_;
  cv::Ptr<cv::aruco::DetectorParameters> detector_params_;
  CharucoDetectorConfig config_;
};

}  // namespace locomotion::calibration
