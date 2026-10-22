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
bool IsHomePlateColor(const float *color) {
  return std::equal(DebugWorldStyle::PlateColor.begin(),
                    DebugWorldStyle::PlateColor.end(), color);
}
void Populate(PhaseTrajectoryTrails &trails) {
  trails.Update(Ball(0.0, 18.0, 0.1), Phase::Pitch, true, {}, {}, Time(0.0));
  trails.Update(Ball(0.0, 18.2, 0.2), Phase::Pitch, true, {}, {}, Time(0.1));
  trails.Update(Ball(0.2, 18.3, 0.3), Phase::Batted, true,
                Physics::Vector3{0.0, 1.0, 18.205}, {}, Time(0.2));
}

void CheckStrikeZoneClippingAndRetention() {
  const StrikeZoneVolume zone{18.44, 0.55, 1.55};
  const auto clipped = [&](Physics::Vector3 from, Physics::Vector3 to) {
    const auto interval = ClipSegmentToStrikeZone(from, to, zone);
    Tests::Check(interval.has_value(), "Expected centerline intersects the physical strike zone");
    return std::array{from + (to - from) * interval->enter,
                      from + (to - from) * interval->exit};
  };
  const auto depthCrossing = clipped({0, 1, zone.frontDepth - 2}, {0, 1, zone.frontDepth + 2});
  Tests::Check(Same(depthCrossing[0], {0, 1, zone.frontDepth}) &&
               Same(depthCrossing[1], {0, 1, zone.frontDepth + HomePlateDepthMeters}),
               "High-speed segments retain exact entry and exit even when both samples are outside");
  const auto vertical = clipped({0, 0, zone.frontDepth + .1}, {0, 2, zone.frontDepth + .1});
  Tests::Check(Same(vertical[0], {0, zone.bottom, zone.frontDepth + .1}) &&
               Same(vertical[1], {0, zone.top, zone.frontDepth + .1}),
               "Centerline clipping uses the configured bottom and top planes");
  const auto lateral = clipped({-1, 1, zone.frontDepth + .1}, {1, 1, zone.frontDepth + .1});
  Tests::Check(std::abs(lateral[0].x + HomePlateWidthMeters * .5) < 1e-12 &&
               std::abs(lateral[1].x - HomePlateWidthMeters * .5) < 1e-12,
               "Side clipping uses official plate width rather than the adjustable aiming grid");
  for (double side : {-1.0, 1.0}) {
    const auto corner = clipped({side * .18, 1, zone.frontDepth + .2},
                                {side * .18, 1, zone.frontDepth + .5});
    Tests::Check(std::abs(corner[1].z - (zone.frontDepth + HomePlateDepthMeters - .18)) < 1e-12,
                 "Both cut rear corners clip against the pentagonal plate prism");
    Tests::Check(!ClipSegmentToStrikeZone({side * .18, 1, zone.frontDepth + .4},
                                         {side * .19, 1, zone.frontDepth + .4}, zone),
                 "A segment in a cut-off rear corner never appears as an inside trajectory");
  }
  Tests::Check(!ClipSegmentToStrikeZone({0, 2, zone.frontDepth - 1},
                                       {0, 2, zone.frontDepth + 1}, zone),
               "Crossing plate depth above the zone produces no clipped path");

  PhaseTrajectoryTrails trail;
  const auto update = [&](Physics::Vector3 position, double flight, double wall) {
    auto ball = Ball(position.x, position.z, flight); ball.position = position;
    trail.Update(ball, Phase::Pitch, true, {}, {}, Time(wall), zone);
  };
  update({0, 1, zone.frontDepth - 2}, .1, .1);
  update({0, 1, zone.frontDepth + 2}, .2, .2);
  const auto paths = [&](PitchTrajectoryDisplayMode mode) {
    OverlayVisibilitySettings selected{mode, false, StrikeZoneDisplayMode::None, false};
    TrajectoryPaths result;
    trail.ForEachVisible(selected, [&](const TrajectoryRenderSpan &span) {
      if (span.persistentZone) {
        Tests::Check(span.uniformColor && span.fadeScale == 1.0F,
                     "Inside pitch paths have uniform color without temporal fading");
        result.push_back(span.points);
      }
    });
    return result;
  };
  const auto equalPaths = [](const TrajectoryPaths &a, const TrajectoryPaths &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size()) return false;
      for (std::size_t j = 0; j < a[i].size(); ++j)
        if (!Same(a[i][j], b[i][j])) return false;
    }
    return true;
  };
  const auto inside = paths(PitchTrajectoryDisplayMode::ZoneOnly);
  Tests::Check(inside.size() == 1 && inside.front().size() == 2 &&
                 Same(inside.front().front(), depthCrossing[0]) &&
                 Same(inside.front().back(), depthCrossing[1]),
               "Collection retains the exact crossing independently of visibility switches");
  Tests::Check(equalPaths(inside, paths(PitchTrajectoryDisplayMode::Full)) &&
                 paths(PitchTrajectoryDisplayMode::None).empty(),
               "Full includes exactly the same inside path as ZoneOnly, while None hides it");
  std::size_t fullOutside = 0;
  OverlayVisibilitySettings full{PitchTrajectoryDisplayMode::Full, false, StrikeZoneDisplayMode::None, false};
  trail.ForEachVisible(full, [&](const TrajectoryRenderSpan &span) {
    if (!span.persistentZone) {
      ++fullOutside;
      for (std::size_t i = 1; i < span.points.size(); ++i) {
        const auto interval = ClipSegmentToStrikeZone(span.points[i - 1], span.points[i], zone);
        Tests::Check(!interval || interval->exit - interval->enter < 1e-12,
                     "Full renders no second copy of any interior pitch segment");
      }
    }
  });
  Tests::Check(fullOutside == 2, "A crossing splits the outside history at both zone boundaries");
  ViewProjectionState view; view.isValid = true; view.position = {0, 1, 3};
  const auto insideTriangles = [&](PitchTrajectoryDisplayMode mode) {
    auto selected = full; selected.pitchTrajectoryDisplayMode = mode;
    std::vector<std::array<double, 6>> triangles;
    EmitProjectedPhaseBallAndTrajectoryMeshOverlay(view, 1280, 720, trail, selected, {},
      [&](const ScreenPoint &a, const ScreenPoint &b, const ScreenPoint &c, const float *color) {
        if (!IsHomePlateColor(color) && color[3] == 1.0F)
          triangles.push_back({a.x, a.y, b.x, b.y, c.x, c.y});
      }, 1.0F, false);
    return triangles;
  };
  const auto zoneTriangles = insideTriangles(PitchTrajectoryDisplayMode::ZoneOnly);
  Tests::Check(!zoneTriangles.empty() && zoneTriangles == insideTriangles(PitchTrajectoryDisplayMode::Full),
               "Actual shared mesh renderer draws identical opaque inside geometry in Full and ZoneOnly");
  for (auto mode : {StrikeZoneDisplayMode::None, StrikeZoneDisplayMode::Plane, StrikeZoneDisplayMode::Box})
    for (bool floor : {false, true}) {
      auto selected = full; selected.strikeZoneDisplayMode = mode; selected.showFloorMesh = floor;
      std::size_t spans = 0;
      trail.ForEachVisible(selected, [&](const auto &) { ++spans; });
      Tests::Check(spans == 3, "Zone wire and floor visibility cannot change pitch trajectory selection");
    }

  for (int sample = 1; sample <= 200; ++sample)
    update({0, 1, zone.frontDepth + 2 + sample * .05}, .2 + sample * .01, .2 + sample * .01);
  Tests::Check(trail.Pitch().Points().size() == MaxPitchTrajectoryPoints &&
                 equalPaths(inside, paths(PitchTrajectoryDisplayMode::ZoneOnly)),
               "The inside crossing survives the 96-point outside history limit");
  update({0, 1, zone.frontDepth + 12}, 2.2, 500);
  Tests::Check(trail.Pitch().Points().empty() &&
                 equalPaths(inside, paths(PitchTrajectoryDisplayMode::Full)),
               "Outside fade completion preserves the same inside path in Full");
  trail.Update(Ball(0, 0, 0), Phase::Stationary, false, {}, {}, Time(501), zone);
  Tests::Check(equalPaths(inside, paths(PitchTrajectoryDisplayMode::ZoneOnly)),
               "The completed inside pitch remains while the interval ball is hidden");
  update({0, 1, 0}, 0, 502);
  Tests::Check(paths(PitchTrajectoryDisplayMode::ZoneOnly).empty(),
               "The next pitch clears the previous inside history before collecting its release");

  trail.Clear();
  update({0, 1, zone.frontDepth + .1}, .1, 1);
  update({.4, 1, zone.frontDepth + .1}, .2, 2);
  update({0, 1.2, zone.frontDepth + .1}, .3, 3);
  const auto separated = paths(PitchTrajectoryDisplayMode::ZoneOnly);
  Tests::Check(separated.size() == 2 && separated[0].size() == 2 && separated[1].size() == 2,
               "Leaving and re-entering the zone creates separate paths without joining the outside gap");
  auto lastBall = Ball(0, zone.frontDepth + .1, .3); lastBall.position.y = 1.2;
  trail.Update(lastBall, Phase::Pitch, true, {}, {}, Time(4), {zone.frontDepth, 1.4, 1.55});
  Tests::Check(paths(PitchTrajectoryDisplayMode::ZoneOnly).empty(),
               "Editing zone bounds re-clips the retained pitch against the new physical volume");

  trail.Clear();
  update({0, 1, zone.frontDepth + .1}, .1, 1);
  update({.01, 1, zone.frontDepth + .11}, .2, 2);
  Tests::Check(trail.Pitch().Points().size() == 1 &&
                 paths(PitchTrajectoryDisplayMode::ZoneOnly).front().size() == 2,
               "Inside collection preserves short movements independently of outside sample spacing");
  trail.Update(Ball(0, 0, 0), Phase::Stationary, false, {}, {false, true, 0}, Time(3), zone);
  Tests::Check(paths(PitchTrajectoryDisplayMode::ZoneOnly).empty(),
               "Starting the next duel windup explicitly clears the previous inside pitch");

  trail.Clear();
  update({0, 1, zone.frontDepth - 1}, .1, 1);
  trail.Update(Ball(0, zone.frontDepth + 1, .2), Phase::Pitch, false, {}, {}, Time(2), zone);
  Tests::Check(paths(PitchTrajectoryDisplayMode::ZoneOnly).size() == 1,
               "A final hidden pitch sample still contributes its high-speed zone crossing");
  trail.Update(Ball(0, 0, -1), Phase::Stationary, false, {}, {}, Time(3), zone);
  Tests::Check(paths(PitchTrajectoryDisplayMode::ZoneOnly).empty(),
               "An explicit trajectory reset removes the retained inside pitch");

  trail.Clear();
  update({0, 1, zone.frontDepth - .1}, .1, 1);
  const Physics::Vector3 contact{0, 1, zone.frontDepth + .1};
  trail.Update(Ball(.5, zone.frontDepth - 1, .2), Phase::Batted, true,
                contact, {}, Time(2), zone);
  const auto contacted = paths(PitchTrajectoryDisplayMode::ZoneOnly);
  Tests::Check(contacted.size() == 1 && Same(contacted[0].back(), contact),
               "A batted contact inside the zone terminates the pitch at exact contact without an outgoing segment");
}
} // namespace

void RunTrajectoryTrailTests() {
  using namespace BaseballSimulatorVR;
  using namespace Common;
  CheckStrikeZoneClippingAndRetention();
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
  visibility.pitchTrajectoryDisplayMode = PitchTrajectoryDisplayMode::Full;
  visibility.showBattedTrajectory = false;
  std::size_t visited = 0;
  trails.ForEachVisible(visibility, [&](const TrajectoryRenderSpan &trail) {
    ++visited;
    Tests::Check(!trail.uniformColor && !trail.persistentZone,
                 "Pitch-only visibility selects only pitched-ball segments");
  });
  Tests::Check(visited == 1,
               "Pitch visibility is independent of the current batted phase");
  visibility.pitchTrajectoryDisplayMode = PitchTrajectoryDisplayMode::None;
  visibility.showBattedTrajectory = true;
  visited = 0;
  trails.ForEachVisible(visibility, [&](const TrajectoryRenderSpan &trail) {
    ++visited;
    Tests::Check(&trail.Points() == &trails.Batted().Points(),
                 "Batted-only visibility selects only outgoing segments");
  });
  Tests::Check(
      visited == 1 && trails.Pitch().Points().size() == 3,
      "Hiding a phase does not delete its independently retained samples");
  visibility.showBattedTrajectory = false;
  visited = 0;
  trails.ForEachVisible(visibility,
                        [&](const TrajectoryRenderSpan &) { ++visited; });
  Tests::Check(visited == 0, "Both independent trajectory switches can be off");

  // Actual renderer helpers must produce one ball, and only selected ribbons.
  ViewProjectionState view;
  view.isValid = true;
  view.position = {0.0, 1.0, 3.0};
  std::size_t plateTriangles = 0;
  EmitProjectedHomePlateOverlay(view, 1280, 720,
      [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
          const float *color) {
        Tests::Check(IsHomePlateColor(color), "Shared plate helper uses its dedicated white color");
        ++plateTriangles;
      });
  Tests::Check(plateTriangles > 0, "Trajectory test camera includes the rendered home plate");
  const auto countTriangles = [&](bool pitch, bool batted) {
    OverlayVisibilitySettings selected;
    selected.pitchTrajectoryDisplayMode = pitch ? PitchTrajectoryDisplayMode::Full : PitchTrajectoryDisplayMode::None;
    selected.showBattedTrajectory = batted;
    std::size_t count = 0;
    std::size_t plateCount = 0;
    EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
        view, 1280, 720, trails, selected, Ball(0.2, 18.3, 0.3),
        [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
            const float *color) {
          if (IsHomePlateColor(color)) ++plateCount;
          else ++count;
        },
        1.0F, true);
    Tests::Check(plateCount == plateTriangles,
                 "Each phase visibility combination retains exactly one home plate");
    return count;
  };
  std::size_t ballTriangles = 0;
  EmitProjectedBallAndTrajectoryMeshOverlay(
      view, 1280, 720, {}, Ball(0.2, 18.3, 0.3),
      [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
          const float *) { ++ballTriangles; });
  Tests::Check(ballTriangles > 800, "The ball renderer draws a "
                                    "full-density visible hemisphere");
  Tests::Check(
      countTriangles(false, false) == ballTriangles,
      "Turning both trails off preserves exactly one full-density ball mesh");
  Tests::Check(
      countTriangles(true, false) == ballTriangles + 4,
      "Pitch-only mesh contains exactly the two pitched segments and one ball");
  const auto battedTriangles = countTriangles(false, true) - ballTriangles;
  Tests::Check(battedTriangles > 0,
      "Batted-only mesh contains the outgoing path and one ball");
  Tests::Check(countTriangles(true, true) == ballTriangles + 4 + battedTriangles,
               "Enabling both meshes does not create a connecting segment or "
               "duplicate the ball");

  {
    PhaseTrajectoryTrails battedTrail, pitchedTrail;
    const double plateDepth = GetPitchDistanceMeters() +
                              DebugWorldLayout::BatterToPlateDistanceMeters;
    const Physics::Vector3 start{0.0, 1.0, plateDepth};
    for (int sample = 1; sample <= 10; ++sample) {
      const auto ball = Ball(0.05 * sample, plateDepth - 10.0 * sample,
                             0.01 * sample);
      battedTrail.Update(ball, Phase::Batted, true, start, {}, Time(0.01 * sample));
      pitchedTrail.Update(ball, Phase::Pitch, true, {}, {}, Time(0.01 * sample));
    }
    const auto lastBall = Ball(0.5, plateDepth - 100.0, 0.1);
    const auto colors = [&](const PhaseTrajectoryTrails &history, bool batted,
                            float opacity) {
      OverlayVisibilitySettings selected;
      selected.pitchTrajectoryDisplayMode = batted ? PitchTrajectoryDisplayMode::None : PitchTrajectoryDisplayMode::Full;
      selected.showBattedTrajectory = batted;
      std::vector<std::array<float, 4>> result;
      std::size_t plateCount = 0;
      EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
          view, 1280, 720, history, selected, lastBall,
          [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &,
              const float *color) {
            if (IsHomePlateColor(color)) {
              ++plateCount;
              return;
            }
            result.push_back({color[0], color[1], color[2], color[3]});
          }, opacity, false);
      Tests::Check(plateCount == plateTriangles,
                   "Ribbon opacity and ball visibility preserve the independent white plate");
      return result;
    };
    for (float opacity : {1.0F, 0.4F}) {
      const auto rendered = colors(battedTrail, true, opacity);
      Tests::Check(!rendered.empty(),
                   "The batted path remains visible across a hundred-meter flight");
      for (const auto &color : rendered) {
        Tests::Check(color[0] == DebugWorldStyle::TrailMeshColor[0] &&
                         color[1] == DebugWorldStyle::TrailMeshColor[1] &&
                         color[2] == DebugWorldStyle::TrailMeshColor[2] &&
                         std::abs(color[3] - DebugWorldStyle::TrailMeshColor[3] * opacity) < 1e-6F,
                     "Batted ribbons retain full color and the configured opacity at every age and distance");
      }
    }
    const auto before = colors(battedTrail, true, 1.0F);
    battedTrail.Update(lastBall, Phase::Batted, false, start, {}, Time(500.0));
    Tests::Check(colors(battedTrail, true, 1.0F) == before,
                 "Retained batted ribbons keep identical colors after the ball is hidden");
    const auto pitched = colors(pitchedTrail, false, 1.0F);
    Tests::Check(pitched.size() == 18 && pitched.front()[3] < pitched.back()[3],
                 "Pitched ribbons grow more transparent toward their oldest samples");
  }

  trails.Update(Ball(0.5, 18.4, 0.4), Phase::Batted, true, contact, {}, Time(0.75));
  Tests::Check(
      trails.Pitch().FadeScale() < 1.0F && trails.Batted().FadeScale() == 1.0F,
      "The completed pitch fades independently while the batted ball advances");
  trails.Update(Ball(0.8, 18.5, 0.5), Phase::Batted, true, contact, {}, Time(1.51));
  Tests::Check(
      trails.Pitch().Points().empty() && !trails.Batted().Points().empty(),
      "An expired pitched trail cannot erase the still-moving batted trail");

  trails.Clear();
  for (int sample = 0; sample < 200; ++sample)
    trails.Update(Ball(0.0, 0.05 * sample, 0.01 * sample), Phase::Pitch, true,
                  {}, {}, Time(0.01 * sample));
  Tests::Check(
      trails.Pitch().Points().size() == 96 && trails.Batted().Points().empty(),
      "The pitched trail keeps at most 96 points");
  const auto recentPitch = trails.Pitch().Points().back();
  for (int sample = 0; sample < 200; ++sample)
    trails.Update(Ball(0.05 * sample, 10.0, 2.0 + 0.01 * sample), Phase::Batted,
                  true, recentPitch, {}, Time(2.0 + 0.001 * sample));
  Tests::Check(trails.Batted().Points().size() == 201 &&
                   trails.Pitch().Points().size() == MaxPitchTrajectoryPoints &&
                   Same(trails.Batted().Points().front(), recentPitch) &&
                   Same(trails.Batted().Points().back(), Ball(9.95, 10.0, 3.99).position),
               "A complete batted flight retains every spaced sample beyond the pitch history limit");
  {
    PhaseTrajectoryTrails retained;
    std::vector<Physics::Vector3> expected{contact};
    for (int sample = 1; sample <= 2048; ++sample) {
      auto ball = Ball(0.1 * sample, 18.205 - 0.05 * sample, 0.01 * sample);
      ball.position.y = 1.0 + 4.0 * std::sin(sample * 0.003);
      expected.push_back(ball.position);
      retained.Update(ball, Phase::Batted, true, contact, {}, Time(0.01 * sample));
      if (sample == 96 || sample == 97 || sample == 192 || sample == 193 ||
          sample == 512 || sample == 1024 || sample == 2048) {
        Tests::Check(retained.Batted().Points().size() == expected.size(),
                     "Long batted flights never discard earlier samples when storage grows");
        for (std::size_t index = 0; index < expected.size(); ++index)
          Tests::Check(Same(retained.Batted().Points()[index], expected[index]),
                       "Every previously drawn batted point remains unchanged as the path advances");
      }
    }
    const auto last = expected.back();
    auto stopped = Ball(last.x, last.z, 20.48);
    stopped.position = last;
    retained.Update(stopped, Phase::Batted, false, contact, {}, Time(1000.0));
    Tests::Check(retained.Batted().Points().size() == expected.size(),
                 "The complete long path remains after the ball is hidden");
  }
  trails.Update(Ball(0.0, 0.0, 0.0), Phase::Pitch, true, {}, {}, Time(3.0));
  Tests::Check(trails.Pitch().Points().size() == 1 &&
                   trails.Batted().Points().empty(),
               "A flight-time reset clears both old histories before starting "
               "a new pitch");
  trails.Update(Ball(0.0, 0.5, 0.0), Phase::Pitch, true, {}, {}, Time(3.01));
  Tests::Check(trails.Pitch().Points().size() == 1,
               "Resetting a pitch repeatedly at time zero cannot connect "
               "different release positions");
  trails.Update(Ball(0.0, 0.6, -1.0), Phase::Pitch, true, {}, {}, Time(3.02));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().empty(),
               "The explicit negative-time trajectory reset sentinel clears "
               "both phases");
  Populate(trails);
  trails.Update(Ball(0.0, 0.0, 0.0), Phase::Stationary, false, {},
                {false, true, 0}, Time(4.0));
  Tests::Check(
      trails.Pitch().Points().empty() && trails.Batted().Points().empty(),
      "A hidden windup ball clears previous pitch and batted geometry");
  trails.Clear();
  const TrajectoryLifecycleState teeContact{true, false, 1};
  trails.Update(Ball(0.2, 18.3, 0.01), Phase::Batted, true, contact, teeContact, Time(5.01));
  Tests::Check(
      trails.Pitch().Points().empty() &&
          Same(trails.Batted().Points().front(), contact),
      "A tee hit starts at contact without manufacturing a pitched trajectory");
  const auto savedTeePoints = trails.Batted().Points();
  trails.Update(Ball(0.2, 18.3, 0.01), Phase::Batted, true, contact, teeContact, Time(100.0));
  Tests::Check(trails.Batted().Points().size() == savedTeePoints.size() &&
                   trails.Batted().FadeScale() == 1.0F,
               "A stopped batted trajectory keeps full opacity indefinitely");
  trails.Update(Ball(0.0, 18.44, 0.0), Phase::Stationary, true, {}, teeContact, Time(101.0));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().size() == savedTeePoints.size() &&
                   Same(trails.Batted().Points().front(), savedTeePoints.front()) &&
                   Same(trails.Batted().Points().back(), savedTeePoints.back()),
               "Respawning a stationary tee ball keeps the completed trajectory without adding a return segment");
  trails.Update(Ball(0.5, 16.0, -1.0), Phase::Stationary, true, {}, teeContact, Time(102.0));
  Tests::Check(trails.Batted().Points().size() == savedTeePoints.size(),
               "The tee reposition and explicit-reset sentinel preserve the previous hit trajectory");
  const Physics::Vector3 nextContact{1.0, 1.2, 16.0};
  trails.Update(Ball(1.5, 15.5, 0.02), Phase::Batted, true, nextContact,
                {true, false, 2}, Time(103.0));
  Tests::Check(trails.Batted().Points().size() == 2 &&
                   Same(trails.Batted().Points().front(), nextContact),
               "The next actual tee contact replaces the old path rather than connecting two hits");
  const Physics::Vector3 repeatedContact{1.4, 1.0, 15.7};
  trails.Update(Ball(1.8, 15.1, 0.03), Phase::Batted, true, repeatedContact,
                {true, false, 3}, Time(104.0));
  Tests::Check(trails.Batted().Points().size() == 2 &&
                   Same(trails.Batted().Points().front(), repeatedContact),
               "A new contact sequence also replaces a trajectory without an intervening stationary frame");
  trails.Clear();
  Populate(trails);
  const auto duelPoints = trails.Batted().Points();
  trails.Update(Ball(0.2, 18.3, 0.3), Phase::Batted, false, contact,
                {false, false, 0}, Time(200.0));
  Tests::Check(trails.Batted().Points().size() == duelPoints.size() &&
                   trails.Batted().FadeScale() == 1.0F,
               "The hidden duel ball retains the completed hit throughout the inter-pitch interval");
  OverlayVisibilitySettings retainedVisibility;
  retainedVisibility.pitchTrajectoryDisplayMode = PitchTrajectoryDisplayMode::None;
  retainedVisibility.showBattedTrajectory = true;
  std::size_t retainedTriangles = 0;
  std::size_t retainedPlateTriangles = 0;
  EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
      view, 1280, 720, trails, retainedVisibility, Ball(0.2, 18.3, 0.3),
      [&](const ScreenPoint &, const ScreenPoint &, const ScreenPoint &, const float *color) {
        if (IsHomePlateColor(color)) ++retainedPlateTriangles;
        else ++retainedTriangles;
      }, 1.0F, false);
  Tests::Check(retainedPlateTriangles == plateTriangles,
               "Hiding an interval ball preserves exactly one independent home plate");
  Tests::Check(retainedTriangles == battedTriangles,
               "A hidden interval ball still renders its retained ribbon without emitting a ball mesh");
  trails.Update(Ball(0.0, 0.0, 0.0), Phase::Stationary, false, {},
                {false, true, 0}, Time(201.0));
  Tests::Check(trails.Batted().Points().empty(),
               "Starting the next duel windup explicitly clears the previous hit trajectory");

  trails.Clear();
  trails.Update(Ball(0.0, 1.0, 0.1), Phase::Pitch, true, {}, {}, Time(0.0));
  trails.Update(Ball(0.0, 1.03, 0.2), Phase::Pitch, true, {}, {}, Time(0.01));
  Tests::Check(trails.Pitch().Points().size() == 1,
               "Movement under the four-centimeter sampling threshold "
               "adds no sample");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, {}, Time(0.02));
  Tests::Check(trails.Pitch().Points().size() == 2,
               "Movement accumulates relative to the last retained sample");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, {}, Time(0.22));
  Tests::Check(trails.Pitch().FadeScale() == 1.0F,
               "A paused trail stays fully opaque during its fade delay");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, {}, Time(0.77));
  Tests::Check(
      std::abs(trails.Pitch().FadeScale() - 0.5F) < 1e-5F,
      "A paused trail is half faded midway through its 1.1-second fade");
  trails.Update(Ball(0.0, 1.05, 0.3), Phase::Pitch, true, {}, {}, Time(1.33));
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
  trails.Update(invalid, Phase::Batted, true, {}, {}, Time(2.02));
  Tests::Check(trails.Pitch().Points().empty() &&
                   trails.Batted().Points().empty(),
               "Nonfinite ball samples cannot contaminate either history");
}
