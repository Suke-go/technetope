#include "fleet_orchestrator.hpp"

#include <stdexcept>
#include <string>

#include "util/logging.hpp"

namespace toio::control {

FleetOrchestrator::FleetOrchestrator(CubeRegistry& registry) : registry_(registry) {}

std::string FleetOrchestrator::assign_goal(const GoalRequest& request) {
    if (request.targets.empty()) {
        throw std::invalid_argument("GoalRequest.targets must not be empty");
    }

    const auto counter = ++goal_counter_;
    std::string goal_id = "goal-" + std::to_string(counter);
    const auto now = std::chrono::system_clock::now();
    GoalAssignment assignment{
        .goal_id = goal_id,
        .cube_id = request.targets.front(),
        .pose = request.pose,
        .priority = request.priority,
        .created_at = now,
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_goals_[assignment.cube_id] = assignment;
        if (request.keep_history) {
            history_.push_back(assignment);
            if (history_.size() > max_history_) {
                history_.pop_front();
            }
        }
    }

    util::log::info("Assigned goal " + goal_id + " to cube " + assignment.cube_id);
    return goal_id;
}

void FleetOrchestrator::clear_goal(const std::string& cube_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_goals_.erase(cube_id);
}

FleetOrchestrator::FleetState FleetOrchestrator::snapshot() const {
    FleetState state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state.tasks_in_queue = active_goals_.size();
        state.active_goals.reserve(active_goals_.size());
        for (const auto& [cube_id, goal] : active_goals_) {
            (void)cube_id;
            state.active_goals.push_back(goal);
        }
    }

    const auto cubes = registry_.snapshot();
    for (const auto& cube : cubes) {
        if (!cube.has_position) {
            state.warnings.push_back("Cube " + cube.cube_id + " position unknown");
        }
    }

    return state;
}

}  // namespace toio::control
