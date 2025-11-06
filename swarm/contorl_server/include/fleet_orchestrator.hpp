#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cube_registry.hpp"

namespace toio::control {

class FleetOrchestrator {
public:
    struct GoalPose {
        double x{0.0};
        double y{0.0};
        std::optional<double> angle;
    };

    struct GoalRequest {
        std::vector<std::string> targets;
        GoalPose pose;
        int priority{0};
        bool keep_history{false};
    };

    struct GoalAssignment {
        std::string goal_id;
        std::string cube_id;
        GoalPose pose;
        int priority{0};
        std::chrono::system_clock::time_point created_at{};
    };

    struct FleetState {
        double tick_hz{30.0};
        std::size_t tasks_in_queue{0};
        std::vector<std::string> warnings;
        std::vector<GoalAssignment> active_goals;
    };

    explicit FleetOrchestrator(CubeRegistry& registry);

    std::string assign_goal(const GoalRequest& request);
    void clear_goal(const std::string& cube_id);
    FleetState snapshot() const;

private:
    CubeRegistry& registry_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, GoalAssignment> active_goals_;
    std::deque<GoalAssignment> history_;
    std::size_t max_history_{64};
    std::atomic_uint64_t goal_counter_{0};
};

}  // namespace toio::control
