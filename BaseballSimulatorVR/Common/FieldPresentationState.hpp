#pragma once

#include "../Physics/Vector3.hpp"
#include "BallFlightPhase.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Common
{
    struct TrajectoryLifecycleState final
    {
        bool retainBattedOnBallReset{ false };
        bool clearBatted{ false };
        std::uint64_t battedContactSequence{ 0 };
    };

    struct PitcherPresentationState final
    {
        bool visible{ false };
        double secondsFromRelease{ -1.2 };
        // Ball center at release, in world coordinates (including mound height).
        Physics::Vector3 releasePositionWorld{};
        double moundHeightMeters{ 0.0 };
        bool throwsLeftHanded{ false };
        double armSpeedScale{ 1.0 };
        double heldBallRadiusMeters{ .037 };
        bool underhandToss{ false };
    };

    enum class PitchZoneCall { Pending, Ball, Strike, Contact };

    struct FieldStatisticsState final
    {
        bool visible{ false };
        bool pitchMetricsVisible{ true };
        std::wstring pitchName{};
        double pitchSpeedMetersPerSecond{ 0.0 };
        double pitchSpinRpm{ 0.0 };
        PitchZoneCall zoneCall{ PitchZoneCall::Pending };
        bool hasPitchBreak{ false };
        // Displacement at the plate front from the initial straight launch ray.
        double pitchHorizontalBreakMeters{ 0.0 };
        double pitchVerticalBreakMeters{ 0.0 };
        double zoneBottomMeters{ .55 };
        double zoneTopMeters{ 1.55 };
        double zoneWidthMeters{ .4318 };
        double pitchBallRadiusMeters{ .037 };
        // Straight-launch residuals at equal depth, anchored at the final actual x/y. Catcher's x is right.
        std::vector<Physics::Vector3> pitchTrace{};
        bool hasBattedMetrics{ false };
        bool battedMetricsLive{ false };
        bool battedHasGroundContact{ false };
        double battedLaunchSpeedMetersPerSecond{ 0.0 };
        double battedLaunchAngleDegrees{ 0.0 };
        double battedDistanceMeters{ 0.0 };
        double battedMaxHeightMeters{ 0.0 };
        bool hasBatContactPosition{ false };
        double batContactFraction{ 0.0 };
        double contactBatLengthMeters{ .84 };
    };

    struct FieldPresentationState final
    {
        PitcherPresentationState pitcher{};
        FieldStatisticsState statistics{};
        // Suppress both the simulated ball and its trail during the windup.
        bool ballVisible{ true };
        BallFlightPhase ballFlightPhase{ BallFlightPhase::Stationary };
        std::optional<Physics::Vector3> battedContactWorld{};
        TrajectoryLifecycleState trajectoryLifecycle{};
    };
}
