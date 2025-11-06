#include "locomotion/robot/MotionPlanner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace locomotion::robot {

namespace {

constexpr float kEpsilon = 1e-5F;
constexpr float kLn2 = 0.6931471805599453F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 6.28318530717958647692F;

float Dot(const Vector2f& a, const Vector2f& b) {
    return (a.x * b.x) + (a.y * b.y);
}

float NormSquared(const Vector2f& v) {
    return Dot(v, v);
}

float Norm(const Vector2f& v) {
    return std::sqrt(std::max(NormSquared(v), 0.0F));
}

Vector2f Normalize(const Vector2f& v, float softening) {
    const float denom = std::max(Norm(v), softening);
    return Vector2f{v.x / denom, v.y / denom};
}

Vector2f ClampMagnitude(const Vector2f& v, float maxMagnitude) {
    const float magnitude = Norm(v);
    if (magnitude <= maxMagnitude || magnitude <= kEpsilon) {
        return v;
    }
    const float scale = maxMagnitude / magnitude;
    return Vector2f{v.x * scale, v.y * scale};
}

float WrapToPi(float angle) {
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

}  // namespace

MotionPlanner::MotionPlanner(MotionPlannerConfig config) : config_(config) {
    SetRandomSeed(config_.randomSeed);
}

void MotionPlanner::SetConfig(MotionPlannerConfig config) {
    const bool seedChanged = config.randomSeed != config_.randomSeed;
    config_ = config;
    if (seedChanged) {
        SetRandomSeed(config_.randomSeed);
    }
}

const MotionPlannerConfig& MotionPlanner::GetConfig() const noexcept {
    return config_;
}

void MotionPlanner::SetRandomSeed(uint64_t seed) {
    if (seed == 0) {
        std::seed_seq seedSeq{
            static_cast<uint64_t>(std::random_device{}()),
            static_cast<uint64_t>(std::random_device{}())};
        rng_.seed(seedSeq);
    } else {
        rng_.seed(seed);
    }
}

std::vector<RobotCommand> MotionPlanner::Plan(
    const std::vector<RobotIntent>& intents,
    const std::vector<DynamicObstacle>& obstacles,
    float deltaTimeSeconds) {
    std::vector<RobotCommand> commands;
    commands.reserve(intents.size());

    if (deltaTimeSeconds <= 0.0F) {
        deltaTimeSeconds = kEpsilon;
    }

    for (const auto& intent : intents) {
        const Vector2f targetVector = ComputeTargetVector(intent);
        const Vector2f neighborRepulsion =
            ComputeNeighborRepulsion(intent, intents);
        const Vector2f obstacleRepulsion =
            ComputeObstacleRepulsion(intent, obstacles);

        Vector2f desiredVelocity = targetVector + neighborRepulsion + obstacleRepulsion;

        const float explorationStd = ComputeExplorationStd(intent.reward);
        const Vector2f jitter{
            explorationStd * normal_(rng_),
            explorationStd * normal_(rng_)};
        desiredVelocity += jitter;

        const auto previousIt = previousVelocities_.find(intent.state.id);
        const Vector2f previousVelocity =
            previousIt != previousVelocities_.end() ? previousIt->second : Vector2f{};

        const float maxDelta = config_.maxAcceleration * deltaTimeSeconds;
        const Vector2f deltaVelocity = desiredVelocity - previousVelocity;
        const float deltaNorm = Norm(deltaVelocity);
        Vector2f limitedVelocity = desiredVelocity;
        if (deltaNorm > maxDelta && deltaNorm > kEpsilon) {
            const Vector2f direction = Normalize(deltaVelocity, kEpsilon);
            limitedVelocity = previousVelocity + (direction * maxDelta);
        }

        const float halfLife = std::max(config_.smoothingHalfLife, 0.01F);
        const float smoothing = std::exp(-kLn2 * deltaTimeSeconds / halfLife);
        Vector2f blendedVelocity =
            (previousVelocity * smoothing) + (limitedVelocity * (1.0F - smoothing));
        blendedVelocity = ClampMagnitude(blendedVelocity, config_.maxSpeed);

        previousVelocities_[intent.state.id] = blendedVelocity;

        float angularVelocity = 0.0F;
        const float speed = Norm(blendedVelocity);
        if (speed > 1e-3F) {
            const float targetHeading = std::atan2(blendedVelocity.y, blendedVelocity.x);
            const float headingError = WrapToPi(targetHeading - intent.state.heading);
            angularVelocity =
                std::clamp(config_.orientationGain * headingError,
                           -config_.maxAngularSpeed,
                           config_.maxAngularSpeed);
        }

        commands.push_back(RobotCommand{
            intent.state.id,
            blendedVelocity,
            angularVelocity,
            explorationStd});
    }

    return commands;
}

void MotionPlanner::Reset() {
    previousVelocities_.clear();
}

Vector2f MotionPlanner::ComputeTargetVector(const RobotIntent& intent) const {
    Vector2f targetDelta = intent.goal.targetPosition - intent.state.position;
    const float distance = Norm(targetDelta);
    Vector2f attraction{};

    if (distance > kEpsilon) {
        const float preferredSpeed =
            intent.goal.preferredSpeed > 0.0F ? intent.goal.preferredSpeed : config_.maxSpeed;
        const float speed = std::min(preferredSpeed, config_.maxSpeed);
        attraction = Normalize(targetDelta, kEpsilon) * speed;
    }

    if (intent.goal.targetVelocity) {
        attraction += *intent.goal.targetVelocity;
    }

    return ClampMagnitude(attraction, config_.maxSpeed);
}

Vector2f MotionPlanner::ComputeNeighborRepulsion(
    const RobotIntent& intent,
    const std::vector<RobotIntent>& intents) const {
    Vector2f repulsion{};
    const float radius = std::max(config_.neighborInfluenceRadius, kEpsilon);

    for (const auto& other : intents) {
        if (other.state.id == intent.state.id) {
            continue;
        }

        const Vector2f offset = intent.state.position - other.state.position;
        const float distance = Norm(offset);
        if (distance > radius || distance < kEpsilon) {
            continue;
        }

        const float falloff = 1.0F - (distance / radius);
        const float gain = config_.neighborRepulsionGain * falloff;
        const Vector2f direction = Normalize(offset, config_.neighborSoftening);
        repulsion += direction * gain;
    }

    return repulsion;
}

Vector2f MotionPlanner::ComputeObstacleRepulsion(
    const RobotIntent& intent,
    const std::vector<DynamicObstacle>& obstacles) const {
    Vector2f repulsion{};
    const float radius = std::max(config_.obstacleInfluenceRadius, kEpsilon);

    for (const auto& obstacle : obstacles) {
        for (const auto& sample : obstacle.samples) {
            const Vector2f offset = intent.state.position - sample.position;
            const float distance = Norm(offset);
            if (distance > radius || distance < kEpsilon) {
                continue;
            }

            const float falloff = 1.0F - (distance / radius);
            const float timeDecay =
                std::exp(-std::max(sample.timeAhead, 0.0F) /
                         std::max(config_.obstacleTimeHorizon, kEpsilon));
            const float certainty = std::clamp(sample.certainty, 0.0F, 1.0F);
            const float gain = config_.obstacleRepulsionGain * falloff * timeDecay * certainty;
            const Vector2f direction = Normalize(offset, config_.obstacleSoftening);
            repulsion += direction * gain;
        }
    }

    return repulsion;
}

float MotionPlanner::ComputeExplorationStd(const RewardSignal& reward) const {
    float exploration = config_.baseExplorationStd;
    const float safety = std::clamp(reward.safety, 0.0F, 1.0F);
    const float curiosity = std::clamp(reward.curiosity, 0.0F, 1.0F);
    const float urgency = std::clamp(reward.taskUrgency, 0.0F, 1.0F);

    exploration += config_.curiosityStdGain * curiosity;
    exploration += config_.urgencyStdGain * urgency;
    exploration -= config_.safetyStdPenalty * safety;

    exploration = std::clamp(exploration, config_.minExplorationStd, config_.maxExplorationStd);
    return exploration;
}

}  // namespace locomotion::robot
