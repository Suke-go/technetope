#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace locomotion::robot {

struct Vector2f {
    float x{0.0F};
    float y{0.0F};

    Vector2f() = default;
    Vector2f(float x_, float y_) : x(x_), y(y_) {}

    Vector2f& operator+=(const Vector2f& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    Vector2f& operator-=(const Vector2f& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    Vector2f& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

inline Vector2f operator+(Vector2f lhs, const Vector2f& rhs) {
    lhs += rhs;
    return lhs;
}

inline Vector2f operator-(Vector2f lhs, const Vector2f& rhs) {
    lhs -= rhs;
    return lhs;
}

inline Vector2f operator*(Vector2f lhs, float scalar) {
    lhs *= scalar;
    return lhs;
}

inline Vector2f operator*(float scalar, Vector2f rhs) {
    rhs *= scalar;
    return rhs;
}

struct RobotState {
    int id{0};
    Vector2f position{};
    Vector2f velocity{};
    float heading{0.0F};  // Radians, counter-clockwise from +X axis.
};

struct RobotGoal {
    Vector2f targetPosition{};
    std::optional<Vector2f> targetVelocity{};
    float preferredSpeed{0.0F};
};

struct RewardSignal {
    float safety{1.0F};     // 0.0 = 危険、1.0 = 十分安全
    float curiosity{0.0F};  // >0.0 で探索意欲を増やす
    float taskUrgency{0.0F};
};

struct RobotIntent {
    RobotState state{};
    RobotGoal goal{};
    RewardSignal reward{};
};

struct ObstacleSample {
    float timeAhead{0.0F};   // seconds into the future
    Vector2f position{};     // predicted position in the shared 2D plane
    float certainty{1.0F};   // weight to modulate influence (0..1 recommended)
};

struct DynamicObstacle {
    std::string label;
    std::vector<ObstacleSample> samples;
};

struct MotionPlannerConfig {
    float maxSpeed{0.18F};                // [m/s] or map units per second
    float maxAcceleration{0.40F};         // per second
    float maxAngularSpeed{2.5F};          // rad/s
    float orientationGain{4.0F};
    float neighborInfluenceRadius{0.12F};
    float neighborRepulsionGain{0.14F};
    float obstacleInfluenceRadius{0.18F};
    float obstacleRepulsionGain{0.30F};
    float obstacleTimeHorizon{0.8F};
    float baseExplorationStd{0.015F};
    float curiosityStdGain{0.040F};
    float safetyStdPenalty{0.030F};
    float urgencyStdGain{0.025F};
    float minExplorationStd{0.0F};
    float maxExplorationStd{0.09F};
    float smoothingHalfLife{0.2F};
    float neighborSoftening{0.02F};
    float obstacleSoftening{0.04F};
    uint64_t randomSeed{42};
};

struct RobotCommand {
    int id{0};
    Vector2f linearVelocity{};
    float angularVelocity{0.0F};
    float appliedExplorationStd{0.0F};
};

class MotionPlanner {
public:
    explicit MotionPlanner(MotionPlannerConfig config = {});

    void SetConfig(MotionPlannerConfig config);
    [[nodiscard]] const MotionPlannerConfig& GetConfig() const noexcept;

    void SetRandomSeed(uint64_t seed);

    [[nodiscard]] std::vector<RobotCommand> Plan(
        const std::vector<RobotIntent>& intents,
        const std::vector<DynamicObstacle>& obstacles,
        float deltaTimeSeconds);

    void Reset();

private:
    MotionPlannerConfig config_{};
    std::mt19937 rng_;
    std::normal_distribution<float> normal_{0.0F, 1.0F};
    std::unordered_map<int, Vector2f> previousVelocities_;

    [[nodiscard]] Vector2f ComputeTargetVector(const RobotIntent& intent) const;
    [[nodiscard]] Vector2f ComputeNeighborRepulsion(
        const RobotIntent& intent,
        const std::vector<RobotIntent>& intents) const;
    [[nodiscard]] Vector2f ComputeObstacleRepulsion(
        const RobotIntent& intent,
        const std::vector<DynamicObstacle>& obstacles) const;
    [[nodiscard]] float ComputeExplorationStd(const RewardSignal& reward) const;
};

}  // namespace locomotion::robot
