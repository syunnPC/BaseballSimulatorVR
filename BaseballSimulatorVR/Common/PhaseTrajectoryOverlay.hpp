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
                contact, now);
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
                                 drawZone, drawBall);
  if (drawTrajectories)
    trails.ForEachVisible(visibility, [&](const TrajectoryTrail &trail) {
      EmitProjectedDebugWorldOverlay(projection, width, height, trail.Points(),
                                     ball, drawPoint, drawLine, true, false,
                                     false, false);
    });
}

template <class TDrawTriangle>
void EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
    const ViewProjectionState &projection, LONG width, LONG height,
    const PhaseTrajectoryTrails &trails,
    const OverlayVisibilitySettings &visibility, const Physics::BallState &ball,
    TDrawTriangle &&drawTriangle, float opacity) {
  static const std::vector<Physics::Vector3> empty;
  trails.ForEachVisible(visibility, [&](const TrajectoryTrail &trail) {
    EmitProjectedBallAndTrajectoryMeshOverlay(
        projection, width, height, trail.Points(), ball, drawTriangle, opacity,
        1.0F - trail.FadeScale(), false);
  });
  EmitProjectedBallAndTrajectoryMeshOverlay(projection, width, height, empty,
                                            ball, drawTriangle, opacity, 0.0F,
                                            true);
}
} // namespace BaseballSimulatorVR::Common
