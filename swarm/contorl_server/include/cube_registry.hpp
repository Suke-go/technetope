#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace toio::control {

class CubeRegistry {
public:
    struct Pose {
        double x{0.0};
        double y{0.0};
        double deg{0.0};
        bool on_mat{false};
    };

    struct LedState {
        int r{0};
        int g{0};
        int b{0};

        bool operator==(const LedState& other) const {
            return r == other.r && g == other.g && b == other.b;
        }

        bool operator!=(const LedState& other) const {
            return !(*this == other);
        }
    };

    struct CubeState {
        std::string cube_id;
        std::string relay_id;
        Pose position{};
        bool has_position{false};
        int battery{-1};
        std::string state{"unknown"};
        std::string goal_id;
        LedState led{};
        std::chrono::system_clock::time_point updated_at{};
    };

    struct Update {
        std::string cube_id;
        std::string relay_id;
        std::optional<Pose> position;
        std::optional<int> battery;
        std::optional<std::string> state;
        std::optional<std::string> goal_id;
        std::optional<LedState> led;
        std::chrono::system_clock::time_point timestamp;
    };

    struct HistoryEntry {
        CubeState state;
        std::chrono::system_clock::time_point timestamp;
    };

    explicit CubeRegistry(std::size_t max_history_entries = 256);

    std::optional<CubeState> apply_update(const Update& update);
    std::vector<CubeState> apply_updates(const std::vector<Update>& updates);

    std::optional<CubeState> get(std::string_view cube_id) const;
    std::vector<CubeState> snapshot() const;
    std::vector<HistoryEntry> history(std::size_t limit = 0) const;

private:
    CubeState& ensure_locked(std::string_view cube_id);

    const std::size_t max_history_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CubeState> cubes_;
    std::deque<HistoryEntry> history_;
};

}  // namespace toio::control
