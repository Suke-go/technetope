#include "locomotion/calibration/CharucoDetector.h"

#include <spdlog/spdlog.h>

#include <opencv2/imgproc.hpp>

namespace locomotion::calibration {

namespace {

void refineSubpixelIfNeeded(const cv::Mat& image,
                            std::vector<std::vector<cv::Point2f>>& marker_corners,
                            const CharucoDetectorConfig& config) {
  if (!config.enable_subpixel_refine) {
    return;
  }

  cv::TermCriteria criteria(cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS,
                            config.subpixel_max_iterations,
                            config.subpixel_epsilon);

  for (auto& corners : marker_corners) {
    cv::cornerSubPix(image, corners, config.subpixel_window, {-1, -1}, criteria);
  }
}

}  // namespace

CharucoDetector::CharucoDetector(cv::Ptr<cv::aruco::Dictionary> dictionary,
                                 cv::Ptr<cv::aruco::CharucoBoard> board,
                                 CharucoDetectorConfig config)
    : dictionary_(std::move(dictionary)),
      board_(std::move(board)),
      detector_params_(cv::aruco::DetectorParameters::create()),
      config_(std::move(config)) {
  detector_params_->cornerRefinementMethod =
      config_.enable_subpixel_refine ? cv::aruco::CORNER_REFINE_SUBPIX
                                     : cv::aruco::CORNER_REFINE_NONE;
}

std::optional<CharucoDetectionResult> CharucoDetector::Detect(
    const cv::Mat& bgr_image) const {
  if (bgr_image.empty()) {
    spdlog::warn("CharucoDetector received empty image.");
    return std::nullopt;
  }

  cv::Mat gray;
  if (bgr_image.channels() == 3) {
    cv::cvtColor(bgr_image, gray, cv::COLOR_BGR2GRAY);
  } else {
    gray = bgr_image;
  }

  std::vector<std::vector<cv::Point2f>> marker_corners;
  std::vector<int> marker_ids;
  cv::aruco::detectMarkers(gray, dictionary_, marker_corners, marker_ids, detector_params_);
  if (marker_ids.empty()) {
    return std::nullopt;
  }

  refineSubpixelIfNeeded(gray, marker_corners, config_);

  cv::Mat charuco_corners;
  cv::Mat charuco_ids;
  cv::aruco::interpolateCornersCharuco(marker_corners, marker_ids, gray, board_, charuco_corners,
                                       charuco_ids);
  if (charuco_ids.empty() || charuco_ids.total() < config_.min_corners) {
    return std::nullopt;
  }

  CharucoDetectionResult result;
  result.detected_markers = static_cast<int>(marker_ids.size());
  result.detected_charuco_corners = static_cast<int>(charuco_ids.total());
  result.image_points.reserve(charuco_ids.total());
  result.board_points.reserve(charuco_ids.total());
  result.ids.reserve(charuco_ids.total());

  for (int i = 0; i < charuco_ids.total(); ++i) {
    const auto id = charuco_ids.at<int>(i);
    const cv::Point2f pixel = charuco_corners.at<cv::Point2f>(i);
    const cv::Point3f board_pt = board_->chessboardCorners[id];
    result.ids.push_back(id);
    result.image_points.push_back(pixel);
    result.board_points.push_back(board_pt);
  }

  return result;
}

}  // namespace locomotion::calibration
