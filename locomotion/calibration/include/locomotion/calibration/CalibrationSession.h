#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "locomotion/calibration/CalibrationPipeline.h"
#include "locomotion/calibration/CalibrationResult.h"

namespace locomotion::calibration {

struct SessionConfig {
  int attempts{5};
  double max_plane_std_mm{5.0};
  double min_inlier_ratio{0.5};
  bool save_intermediate_snapshots{false};
  std::string snapshot_output_dir;
};

class CalibrationSession {
 public:
  CalibrationSession(CalibrationPipeline pipeline, SessionConfig session_config);

  std::optional<CalibrationResult> Run();
  bool SaveResultJson(const CalibrationResult& result, const std::string& path) const;

 private:
  CalibrationPipeline pipeline_;
  SessionConfig session_config_;

  static CalibrationResult ToResult(const CalibrationSnapshot& snapshot);
};

}  // namespace locomotion::calibration
