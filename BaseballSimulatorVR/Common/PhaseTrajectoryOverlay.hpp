#pragma once

#include "DebugWorldProjection.hpp"
#include "FieldPresentationState.hpp"
#include "PhaseTrajectoryTrails.hpp"

namespace BaseballSimulatorVR::Common {
inline void
UpdatePhaseTrajectoryTrails(PhaseTrajectoryTrails &trails,
                            const Physics::BallState &ball,
                            const FieldPresentationState &presentation,
                            TrajectoryTrail::Clock::time_point now) {
  auto contact = presentation.battedContactWorld;
  if (contact)
    contact->z += GetPitchDistanceMeters() +
                  DebugWorldLayout::BatterToPlateDistanceMeters;
  trails.Update(ball, presentation.ballFlightPhase, presentation.ballVisible,
                contact, presentation.trajectoryLifecycle, now,
                {GetPitchDistanceMeters(), GetStrikeZoneBottomMeters(), GetStrikeZoneTopMeters()});
}

template <class TDrawPoint, class TDrawLine>
void EmitProjectedPhaseDebugWorldOverlay(
    const ViewProjectionState &projection, LONG width, LONG height,
    const PhaseTrajectoryTrails &trails,
    const OverlayVisibilitySettings &visibility, const Physics::BallState &ball,
    TDrawPoint &&drawPoint, TDrawLine &&drawLine, bool drawTrajectories,
    bool drawFloor, bool drawZone, bool drawBall) {
  static const std::vector<Physics::Vector3> empty;
  EmitProjectedDebugWorldOverlay(projection, width, height, empty, ball,
                                 drawPoint, drawLine, false, drawFloor,
                                 drawZone ? visibility.strikeZoneDisplayMode : StrikeZoneDisplayMode::None,
                                 drawBall);
  if (drawTrajectories)
    trails.ForEachVisible(visibility, [&](const TrajectoryRenderSpan &trail) {
      auto trailColor = DebugWorldStyle::TrailColor;
      trailColor[3] = trail.persistentZone ? 1.0F : trailColor[3] * trail.fadeScale;
      EmitProjectedDebugWorldOverlay(projection, width, height, trail.Points(),
                                     ball, drawPoint,
                                     [&](const auto &from, const auto &to, LONG halfSize, const float *) {
                                       drawLine(from, to, halfSize, trailColor.data());
                                     }, true, false,
                                     StrikeZoneDisplayMode::None, false);
    });
}

template <class TDrawTriangle>
void EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
    const ViewProjectionState &projection, LONG width, LONG height,
    const PhaseTrajectoryTrails &trails,
    const OverlayVisibilitySettings &visibility, const Physics::BallState &ball,
    TDrawTriangle &&drawTriangle, float opacity, bool drawBall) {
  static const std::vector<Physics::Vector3> empty;
  EmitProjectedHomePlateOverlay(projection, width, height, drawTriangle);
  trails.ForEachVisible(visibility, [&](const TrajectoryRenderSpan &trail) {
    EmitProjectedBallAndTrajectoryMeshOverlay(
        projection, width, height, trail.Points(), ball, drawTriangle, opacity,
        1.0F - trail.FadeScale(), false,
        trail.persistentZone ? TrajectoryColorMode::UniformOpaque :
        trail.uniformColor ? TrajectoryColorMode::Uniform : TrajectoryColorMode::Fading);
  });
  if (drawBall)
    EmitProjectedBallAndTrajectoryMeshOverlay(projection, width, height, empty,
                                              ball, drawTriangle, opacity, 0.0F,
                                              true);
}
} // namespace BaseballSimulatorVR::Common
