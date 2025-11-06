#include "cube_registry.hpp"

#include <algorithm>
#include <utility>

namespace toio::control {

CubeRegistry::CubeRegistry(std::size_t max_history_entries)
    : max_history_(max_history_entries) {}

CubeRegistry::CubeState& CubeRegistry::ensure_locked(std::string_view cube_id) {
    auto key = std::string(cube_id);
    auto [it, inserted] = cubes_.try_emplace(key);
    if (inserted) {
        it->second.cube_id = key;
    }
    return it->second;
}

std::optional<CubeRegistry::CubeState> CubeRegistry::apply_update(const Update& update) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = ensure_locked(update.cube_id);
    bool changed = false;

    if (!update.relay_id.empty() && state.relay_id != update.relay_id) {
        state.relay_id = update.relay_id;
        changed = true;
    }
    if (update.position) {
        state.position = *update.position;
        state.has_position = true;
        changed = true;
    }
    if (update.battery && state.battery != *update.battery) {
        state.battery = *update.battery;
        changed = true;
    }
    if (update.state && state.state != *update.state) {
        state.state = *update.state;
        changed = true;
    }
    if (update.goal_id && state.goal_id != *update.goal_id) {
        state.goal_id = *update.goal_id;
        changed = true;
    }
    if (update.led && state.led != *update.led) {
        state.led = *update.led;
        changed = true;
    }

    if (!changed) {
        return std::nullopt;
    }

    state.updated_at = update.timestamp;
    history_.push_back(HistoryEntry{state, update.timestamp});
    if (history_.size() > max_history_) {
        history_.pop_front();
    }

    return state;
}

std::vector<CubeRegistry::CubeState> CubeRegistry::apply_updates(const std::vector<Update>& updates) {
    std::vector<CubeState> changed;
    std::lock_guard<std::mutex> lock(mutex_);
    changed.reserve(updates.size());
    for (const auto& update : updates) {
        auto& state = ensure_locked(update.cube_id);
        bool local_changed = false;

        if (!update.relay_id.empty() && state.relay_id != update.relay_id) {
            state.relay_id = update.relay_id;
            local_changed = true;
        }
        if (update.position) {
            state.position = *update.position;
            state.has_position = true;
            local_changed = true;
        }
        if (update.battery && state.battery != *update.battery) {
            state.battery = *update.battery;
            local_changed = true;
        }
        if (update.state && state.state != *update.state) {
            state.state = *update.state;
            local_changed = true;
        }
        if (update.goal_id && state.goal_id != *update.goal_id) {
            state.goal_id = *update.goal_id;
            local_changed = true;
        }
        if (update.led && state.led != *update.led) {
            state.led = *update.led;
            local_changed = true;
        }

        if (local_changed) {
            state.updated_at = update.timestamp;
            history_.push_back(HistoryEntry{state, update.timestamp});
            if (history_.size() > max_history_) {
                history_.pop_front();
            }
            changed.push_back(state);
        }
    }
    return changed;
}

std::optional<CubeRegistry::CubeState> CubeRegistry::get(std::string_view cube_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cubes_.find(std::string(cube_id));
    if (it == cubes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<CubeRegistry::CubeState> CubeRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CubeState> result;
    result.reserve(cubes_.size());
    for (const auto& [_, state] : cubes_) {
        result.push_back(state);
    }
    return result;
}

std::vector<CubeRegistry::HistoryEntry> CubeRegistry::history(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t available = history_.size();
    std::size_t count = limit == 0 ? available : std::min(limit, available);
    std::vector<HistoryEntry> result;
    result.reserve(count);
    if (count == 0) {
        return result;
    }

    auto start_iter = history_.end() - static_cast<std::ptrdiff_t>(count);
    for (auto it = start_iter; it != history_.end(); ++it) {
        result.push_back(*it);
    }
    return result;
}

}  // namespace toio::control
