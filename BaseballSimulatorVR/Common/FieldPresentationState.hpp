#pragma once

#include "../Physics/Vector3.hpp"
#include "BallFlightPhase.hpp"

#include <optional>
#include <string>

namespace BaseballSimulatorVR::Common
{
    struct PitcherPresentationState final
    {
        bool visible{ false };
        double secondsFromRelease{ -1.2 };
        // Ball center at release, in world coordinates (including mound height).
        Physics::Vector3 releasePositionWorld{};
        double moundHeightMeters{ 0.0 };
        bool throwsLeftHanded{ false };
        double armSpeedScale{ 1.0 };
    };

    struct FieldStatisticsState final
    {
        bool visible{ false };
        bool pitchMetricsVisible{ true };
        std::wstring pitchName{};
        double pitchSpeedMetersPerSecond{ 0.0 };
        double pitchSpinRpm{ 0.0 };
        bool hasBattedMetrics{ false };
        bool battedMetricsLive{ false };
        bool battedHasGroundContact{ false };
        double battedLaunchSpeedMetersPerSecond{ 0.0 };
        double battedLaunchAngleDegrees{ 0.0 };
        double battedDistanceMeters{ 0.0 };
        double battedMaxHeightMeters{ 0.0 };
    };

    struct FieldPresentationState final
    {
        PitcherPresentationState pitcher{};
        FieldStatisticsState statistics{};
        // Suppress both the simulated ball and its trail during the windup.
        bool ballVisible{ true };
        BallFlightPhase ballFlightPhase{ BallFlightPhase::Stationary };
        std::optional<Physics::Vector3> battedContactWorld{};
    };
}
