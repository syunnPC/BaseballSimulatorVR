#include "../BaseballSimulatorVR/Common/PhaseTrajectoryOverlay.hpp"
#include "TestSupport.hpp"

#include <cmath>
#include <limits>

namespace {
using namespace BaseballSimulatorVR;
using namespace Common;
using Phase = BallFlightPhase;
TrajectoryTrail::Clock::time_point Time(double seconds) {
  return TrajectoryTrail::Clock::time_point{} +
         std::chrono::duration_cast<TrajectoryTrail::Clock::duration>(
             std::chrono::duration<double>(seconds));
}
Physics::BallState Ball(double x, double z, double flight) {
  Physics::BallState ball;
  ball.position = {x, 1.0, z};
  ball.velocity = {0.0, 0.0, 30.0};
  ball.flightTimeSeconds = flight;
  return ball;
}
bool Same(const Physics::Vector3 &a, const Physics::Vector3 &b) {
  return Physics::LengthSquared(a - b) < 1e-16;
}
void Populate(PhaseTrajectoryTrails &trails) {
  trails.Update(Ball(0.0, 18.0, 0.1), Phase::Pitch, true, {}, Time(0.0));
  trails.Update(Ball(0.0, 18.2, 0.2), Phase::Pitch, true, {}, Time(0.1));
  trails.Update(Ball(0.2, 18.3, 0.3), Phase::Batted, true,
                Physics::Vector3{0.0, 1.0, 18.205}, Time(0.2));
}
} // namespace

void RunTrajectoryTrailTests() {
  using namespace BaseballSimulatorVR;
  using namespace Common;
  PhaseTrajectoryTrails trails;
  Populate(trails);
  const Physics::Vector3 contact{0.0, 1.0, 18.205};
  Tests::Check(
      trails.Pitch().Points().size() == 3 &&
          trails.Batted().Points().size() == 2,
      "Pitched and batted samples are retained in separate phase histories");
  Tests::Check(Same(trails.Pitch().Points().back(), contact) &&
                   Same(trails.Batted().Points().front(), contact),
               "Exact contact joins each phase endpoint even within the normal "
               "four-centimeter sample spacing");
  Tests::Check(
      !Same(trails.Pitch().Points().back(), trails.Batted().Points().back()),
      "An outgoing batted-ball sample never enters the pitched-ball history");
  OverlayVisibilitySettings visibility;
  visibility.showPitchTrajectory = true;
  visibility.showBattedTrajectory = false;
  std::size_t visited = 0;
  trails.ForEachVisible(visibility, [&](const TrajectoryTrail &trail) {
    ++visited;
    Tests::Check(&trail == &trails.Pitch(),
                 "Pitch-only visibility selects only pitched-ball segments");
  });
  Tests::Check(visited == 1,
               "Pitch visibility is independent of the current batted phase");
  visibility.showPitchTrajectory = false;
  visibility.showBattedTrajectory = true;
  visited = 0;
  trails.ForEachVisible(visibility, [&](const TrajectoryTrail &trail) {
    ++visited;
    Tests::Check(&trail == &trails.Batted(),
                 "Batted-only visibility selects only outgoing segments");
  });
  Tests::Check(
      visited == 1 && trails.Pitch().Points().size() == 3,
      "Hiding a phase does not delete its independently retained samples");
  visibility.showBattedTrajectory = false;
  visited = 0;
  trails.ForEachVisible(visibility,
                        [&](const TrajectoryTrail &) { ++visited; });
  Tests::Check(visited == 0, "Both independent trajectory switches can be off");

  // Actual renderer helpers must produce one ball, and only selected ribbons.
  ViewProjectionState view;
  view.isValid = true;
  view.position = {0.0, 1.0, 3.0};
  const auto countTriangles = [&](bool pitch, bool batted) {
    OverlayVisibilitySettings selected;
    selected.showPitchTrajectory = pitch;
    selected.showBattedTrajectory = batted;
    std::size_t count = 0;
    EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
        view, 1280, 720, trails, selected, Ball(0.2, 18.3, 0.3),
        [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
            const float *) { ++count; },
        1.0F);
    return count;
  };
  std::size_t ballTriangles = 0;
  EmitProjectedBallAndTrajectoryMeshOverlay(
      view, 1280, 720, {}, Ball(0.2, 18.3, 0.3),
      [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
          const float *) { ++ballTriangles; });
  Tests::Check(ballTriangles > 800, "The unchanged ball renderer retains its "
                                    "full-density visible hemisphere");
  Tests::Check(
      countTriangles(false, false) == ballTriangles,
      "Turning both trails off preserves exactly one full-density ball mesh");
  Tests::Check(
      countTriangles(true, false) == ballTriangles + 4,
      "Pitch-only mesh contains exactly the two pitched segments and one ball");
  Tests::Check(
      countTriangles(false, true) == ballTriangles + 2,
      "Batted-only mesh contains exactly the outgoing segment and one ball");
  Tests::Check(countTriangles(true, true) == ballTriangles + 6,
               "Enabling both meshes does not create a connecting segment or "
               "duplicate the ball");

  trails.Update(Ball(0.5, 18.4, 0.4), Phase::Batted, true, contact, Time(0.75));
  Tests::Check(
      trails.Pitch().FadeScale() < 1.0F && trails.Batted().FadeScale() == 1.0F,
      "The completed pitch fades independently while the batted ball advances");
  trails.Update(Ball(0.8, 18.5, 0.5), Phase::Batted, true, contact, Time(1.51));
  Tests::Check(
      trails.Pitch().Points().empty() && !trails.Batted().Points().empty(),
      "An expired pitched trail cannot erase the still-moving batted trail");

  trails.Clear();
  for (int sample = 0; sample < 200; ++sample)
    trails.Update(Ball(0.0, 0.05 * sample, 0.01 * sample), Phase::Pitch, true,
                  {}, Time(0.01 * sample));
  Tests::Check(
      trails.Pitch().Points().size() == 96 && trails.Batted().Points().empty(),
      "The pitched trail preserves the existing 96-point density limit");
  const auto recentPitch = trails.Pitch().Points().back();
  for (int sample = 0; sample < 200; ++sample)
    trails.Update(Ball(0.05 * sample, 10.0, 2.0 + 0.01 * sample), Phase::Batted,
                  true, recentPitch, Time(2.0 + 0.001 * sample));
  Tests::Check(trails.Batted().Points().size() == 96 &&
                   trails.Pitch().Points().size() == 96,
               "Each phase has its own point limit rather than evicting the "
               "other phase's history");
  trails.Update(Ball(0.0, 0.0, 0.0), Phase::Pitch, true, {}, Time(3.0));
  Tests::Check(trails.Pitch().Points().size() == 1 &&
                   trails.Batted().Points().empty(),
               "A flight-time reset clears both old histories before starting "
               "a new pitch");
  trails.Update(Ball(0.0, 0.5, 0.0), Phase::Pitch, true, {}, Time(3.01));
  Tests::Check(trails.Pitch().Points().size() == 1,
               "Resetting a pitch repeatedly at time zero cannot connect "
               "different release positions");
  trails.Update(Ball(0.0, 0.6, -1.0), Phase::Pitch, true, {}, Time(3.02));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().empty(),
               "The explicit negative-time trajectory reset sentinel clears "
               "both phases");
  Populate(trails);
  trails.Update(Ball(0.0, 0.0, 0.0), Phase::Pitch, false, {}, Time(4.0));
  Tests::Check(
      trails.Pitch().Points().empty() && trails.Batted().Points().empty(),
      "A hidden windup ball clears previous pitch and batted geometry");
  Populate(trails);
  trails.Update(Ball(0.0, 18.44, 0.0), Phase::Stationary, true, {}, Time(5.0));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().empty(),
               "Respawning a stationary tee ball cannot retain a previously "
               "batted trail");
  trails.Update(Ball(0.2, 18.3, 0.01), Phase::Batted, true, contact,
                Time(5.01));
  Tests::Check(
      trails.Pitch().Points().empty() &&
          Same(trails.Batted().Points().front(), contact),
      "A tee hit starts at contact without manufacturing a pitched trajectory");

  trails.Clear();
  trails.Update(Ball(0.0, 1.0, 0.1), Phase::Pitch, true, {}, Time(0.0));
  trails.Update(Ball(0.0, 1.03, 0.2), Phase::Pitch, true, {}, Time(0.01));
  Tests::Check(trails.Pitch().Points().size() == 1,
               "Sub-four-centimeter movement preserves the established "
               "sampling threshold");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, Time(0.02));
  Tests::Check(trails.Pitch().Points().size() == 2,
               "Movement accumulates relative to the last retained sample");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, Time(0.22));
  Tests::Check(trails.Pitch().FadeScale() == 1.0F,
               "A paused trail retains its original fade delay");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, Time(0.77));
  Tests::Check(
      std::abs(trails.Pitch().FadeScale() - 0.5F) < 1e-5F,
      "A paused trail preserves the original 1.1-second fade duration");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, Time(1.33));
  Tests::Check(trails.Pitch().Points().empty(),
               "A fully faded paused trail releases its segments");

  FieldPresentationState presentation;
  presentation.ballFlightPhase = Phase::Pitch;
  UpdatePhaseTrajectoryTrails(trails, Ball(0.0, GetPitchDistanceMeters(), 0.1),
                              presentation, Time(2.0));
  presentation.ballFlightPhase = Phase::Batted;
  presentation.battedContactWorld = Physics::Vector3{
      0.0, 1.18, -DebugWorldLayout::BatterToPlateDistanceMeters};
  auto outgoing = Ball(0.2, GetPitchDistanceMeters() - 0.1, 0.2);
  outgoing.position.y = 1.18;
  UpdatePhaseTrajectoryTrails(trails, outgoing, presentation, Time(2.01));
  Tests::Check(Same(trails.Batted().Points().front(),
                    {0.0, 1.18, GetPitchDistanceMeters()}),
               "World contact converts depth once and keeps the "
               "already-applied mound height");

  auto invalid = outgoing;
  invalid.position.x = std::numeric_limits<double>::quiet_NaN();
  trails.Update(invalid, Phase::Batted, true, {}, Time(2.02));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().empty(),
               "Nonfinite ball samples cannot contaminate either history");
}
