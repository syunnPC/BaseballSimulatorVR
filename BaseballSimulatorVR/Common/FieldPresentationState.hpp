#pragma once

#include "../Physics/BatMaterial.hpp"
#include "../Physics/Vector3.hpp"
#include "BallFlightPhase.hpp"
#include "CatcherMittState.hpp"

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
        // 0 while delivering; rises to 1 as the pitcher walks back to the rubber and comes
        // set between duel pitches.
        double recoveryProgress{ 0.0 };
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
        // Simulation depth of the plate front, where the zone stands.
        double plateFrontDepthMeters{ 18.44 };
        // The pitch's actual flight: x toward the catcher's right, y height above the field,
        // z the simulation depth from the release.
        std::vector<Physics::Vector3> pitchTrace{};
        bool hasBattedMetrics{ false };
        bool battedMetricsLive{ false };
        bool battedHasGroundContact{ false };
        // Out or hit and where it went, once the batted ball has come down.
        std::wstring battedResult{};
        double battedLaunchSpeedMetersPerSecond{ 0.0 };
        double battedLaunchAngleDegrees{ 0.0 };
        double battedDistanceMeters{ 0.0 };
        double battedMaxHeightMeters{ 0.0 };
        bool hasBatContactPosition{ false };
        double batContactFraction{ 0.0 };
        // Bat in use, so the contact diagram marks that bat's sweet zone.
        Physics::BatMaterial batMaterial{ Physics::BatMaterial::Wood };
        double contactBatLengthMeters{ .84 };
        // Catcher mode: the running tally and how the last pitch was handled.
        bool catcherMode{ false };
        int catcherCaught{ 0 };
        int catcherPitches{ 0 };
        std::wstring catcherResult{};
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
        std::optional<CatcherMittState> catcherMitt{};
        // The sign being given and what to do next, on a card above the mitt.
        std::wstring catcherSignLine{};
        std::wstring catcherStatusLine{};
    };
}
