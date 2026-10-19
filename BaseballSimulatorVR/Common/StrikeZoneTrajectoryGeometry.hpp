#pragma once

#include "HomePlateGeometry.hpp"
#include "OverlayVisibilitySettings.hpp"

#include <optional>
#include <vector>

namespace BaseballSimulatorVR::Common {
struct StrikeZoneVolume final {
  double frontDepth{18.44};
  double bottom{0.55};
  double top{1.55};
  bool operator==(const StrikeZoneVolume &) const = default;
};

struct StrikeZoneSegmentInterval final { double enter{}, exit{1.0}; };

// Clip the centerline against all seven half-spaces of the physical plate prism.
// Both endpoints may lie outside: a fast pitch still retains its entire crossing.
[[nodiscard]] inline std::optional<StrikeZoneSegmentInterval>
ClipSegmentToStrikeZone(const Physics::Vector3 &from, const Physics::Vector3 &to,
                        const StrikeZoneVolume &zone) noexcept {
  const auto finite = [](const Physics::Vector3 &p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
  };
  if (!finite(from) || !finite(to) || !std::isfinite(zone.frontDepth) ||
      !std::isfinite(zone.bottom) || !std::isfinite(zone.top) || zone.top < zone.bottom)
    return {};
  StrikeZoneSegmentInterval interval;
  const auto clip = [&](double start, double end) {
    if (start < 0.0 && end < 0.0) return false;
    if (start < 0.0) interval.enter = (std::max)(interval.enter, start / (start - end));
    else if (end < 0.0) interval.exit = (std::min)(interval.exit, start / (start - end));
    return interval.enter <= interval.exit;
  };
  if (!clip(from.y - zone.bottom, to.y - zone.bottom) ||
      !clip(zone.top - from.y, zone.top - to.y)) return {};
  const auto vertices = GetHomePlateVertices(zone.frontDepth);
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto &a = vertices[i];
    const auto edge = vertices[(i + 1) % vertices.size()] - a;
    if (!clip(edge.x * (from.z - a.z) - edge.z * (from.x - a.x),
              edge.x * (to.z - a.z) - edge.z * (to.x - a.x))) return {};
  }
  return interval;
}

template<class TLine>
void ForEachStrikeZoneWireSegment(StrikeZoneDisplayMode mode,
                                 const StrikeZoneVolume &zone, TLine &&line) {
  if (mode == StrikeZoneDisplayMode::None) return;
  const auto bottom = GetHomePlateVertices(zone.frontDepth, zone.bottom);
  const auto top = GetHomePlateVertices(zone.frontDepth, zone.top);
  if (mode == StrikeZoneDisplayMode::Plane) {
    line(bottom[0], bottom[1]); line(bottom[1], top[1]);
    line(top[1], top[0]); line(top[0], bottom[0]);
    return;
  }
  for (std::size_t i = 0; i < bottom.size(); ++i) {
    const auto next = (i + 1) % bottom.size();
    line(bottom[i], bottom[next]); line(top[i], top[next]); line(bottom[i], top[i]);
  }
}

using TrajectoryPaths = std::vector<std::vector<Physics::Vector3>>;

inline void AppendTrajectorySegment(TrajectoryPaths &paths,
                                    const Physics::Vector3 &from,
                                    const Physics::Vector3 &to,
                                    bool joinPrevious) {
  if (Physics::LengthSquared(to - from) <= 1e-20) return;
  if (joinPrevious && !paths.empty() &&
      Physics::LengthSquared(paths.back().back() - from) <= 1e-20)
    paths.back().push_back(to);
  else paths.push_back({from, to});
}

// The explicit continuity flag prevents a tiny outside excursion from joining
// two otherwise coincident boundary intersections into a fabricated path.
inline void AppendStrikeZoneTrajectorySegment(TrajectoryPaths &paths,
    bool &continuous, const Physics::Vector3 &from, const Physics::Vector3 &to,
    const StrikeZoneVolume &zone) {
  const auto interval = ClipSegmentToStrikeZone(from, to, zone);
  if (!interval) { continuous = false; return; }
  const auto delta = to - from;
  AppendTrajectorySegment(paths, from + delta * interval->enter,
                          from + delta * interval->exit,
                          continuous && interval->enter == 0.0);
  continuous = interval->exit == 1.0;
}

inline void BuildOutsideStrikeZoneTrajectory(const std::vector<Physics::Vector3> &points,
    const StrikeZoneVolume &zone, TrajectoryPaths &paths) {
  paths.clear();
  bool continuous = false;
  for (std::size_t i = 1; i < points.size(); ++i) {
    const auto &from = points[i - 1]; const auto &to = points[i];
    const auto interval = ClipSegmentToStrikeZone(from, to, zone);
    if (!interval) {
      AppendTrajectorySegment(paths, from, to, continuous); continuous = true;
    } else {
      const auto delta = to - from;
      if (interval->enter > 0.0)
        AppendTrajectorySegment(paths, from, from + delta * interval->enter, continuous);
      if (interval->exit < 1.0)
        AppendTrajectorySegment(paths, from + delta * interval->exit, to, false);
      continuous = interval->exit < 1.0;
    }
  }
}
} // namespace BaseballSimulatorVR::Common
