#pragma once

#include "../Physics/BallState.hpp"
#include "BallFlightPhase.hpp"
#include "FieldPresentationState.hpp"
#include "OverlayVisibilitySettings.hpp"
#include "StrikeZoneTrajectoryGeometry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace BaseballSimulatorVR::Common {
inline constexpr std::size_t MaxPitchTrajectoryPoints = 96;
inline constexpr double TrajectorySampleSpacingMeters = 0.04;
inline constexpr double TrajectoryFadeDelaySeconds = 0.20;
inline constexpr double TrajectoryFadeDurationSeconds = 1.10;

enum class TrajectoryHistory { Recent, CompleteFlight };

class TrajectoryTrail final {
public:
  using Clock = std::chrono::steady_clock;
  explicit TrajectoryTrail(TrajectoryHistory history) : m_history(history) {
    m_points.reserve(history == TrajectoryHistory::Recent ? MaxPitchTrajectoryPoints + 1 : 512);
  }
  [[nodiscard]] const std::vector<Physics::Vector3> &Points() const noexcept {
    return m_points;
  }
  [[nodiscard]] float FadeScale() const noexcept { return m_fadeScale; }
  [[nodiscard]] std::uint64_t Revision() const noexcept { return m_revision; }
  void Clear() noexcept {
    if (!m_points.empty()) ++m_revision;
    m_points.clear();
    m_fadeScale = 1.0F;
    m_lastAdvanceTime = {};
  }
  void Touch(Clock::time_point now) noexcept {
    m_fadeScale = 1.0F;
    m_lastAdvanceTime = now;
  }
  void Append(const Physics::Vector3 &point, Clock::time_point now,
              bool forceEndpoint = false) {
    const double separation =
        m_points.empty() ? 1.0
                         : Physics::LengthSquared(point - m_points.back());
    if (m_points.empty() || separation >= TrajectorySampleSpacingMeters * TrajectorySampleSpacingMeters ||
        (forceEndpoint && separation > 1e-16)) {
      m_points.push_back(point);
      ++m_revision;
      if (m_history == TrajectoryHistory::Recent && m_points.size() > MaxPitchTrajectoryPoints)
        m_points.erase(m_points.begin());
      Touch(now);
    }
  }
  void UpdateFade(Clock::time_point now) noexcept {
    if (m_points.size() < 2)
      return;
    const double stale =
        std::chrono::duration<double>(now - m_lastAdvanceTime).count();
    m_fadeScale = static_cast<float>(
        1.0 - std::clamp((stale - TrajectoryFadeDelaySeconds) /
                             TrajectoryFadeDurationSeconds,
                         0.0, 1.0));
    if (m_fadeScale <= 0.001F)
      Clear();
  }

private:
  std::vector<Physics::Vector3> m_points;
  Clock::time_point m_lastAdvanceTime{};
  float m_fadeScale{1.0F};
  TrajectoryHistory m_history;
  std::uint64_t m_revision{};
};

struct TrajectoryRenderSpan final {
  const std::vector<Physics::Vector3> &points;
  float fadeScale{1.0F};
  bool uniformColor{};
  bool persistentZone{};
  [[nodiscard]] const std::vector<Physics::Vector3> &Points() const noexcept { return points; }
  [[nodiscard]] float FadeScale() const noexcept { return fadeScale; }
};

class PhaseTrajectoryTrails final {
public:
  using Clock = TrajectoryTrail::Clock;
  void Clear() noexcept {
    m_pitch.Clear();
    m_batted.Clear();
    ClearPitchZone();
    m_pitchOutside.clear();
    m_previousPhase = BallFlightPhase::Stationary;
    m_lastFlightTimeSeconds = 0.0;
    m_hasSample = false;
    m_hasLifecycle = false;
  }
  // Contact and ball positions use the same overlay/simulation coordinates,
  // including any applied mound height. Overlay switches never control collection.
  void Update(const Physics::BallState &ball, BallFlightPhase phase,
              bool ballVisible,
              const std::optional<Physics::Vector3> &contactPosition,
              const TrajectoryLifecycleState &lifecycle,
              Clock::time_point now, const StrikeZoneVolume &zone = {}) {
    SetZone(zone);
    const auto finite = [](const Physics::Vector3 &point) {
      return std::isfinite(point.x) && std::isfinite(point.y) &&
             std::isfinite(point.z);
    };
    if (m_hasLifecycle && m_retainBattedOnReset != lifecycle.retainBattedOnBallReset)
      Clear();
    const bool newContact = m_hasLifecycle &&
        lifecycle.battedContactSequence != m_lastContactSequence;
    m_hasLifecycle = true;
    m_retainBattedOnReset = lifecycle.retainBattedOnBallReset;
    m_lastContactSequence = lifecycle.battedContactSequence;
    if (lifecycle.clearBatted) {
      m_batted.Clear();
      ClearPitchZone();
    }
    if (!std::isfinite(ball.flightTimeSeconds) || !finite(ball.position)) {
      Clear();
      return;
    }
    const auto resetSamples = [&](bool clearBatted) {
      m_pitch.Clear();
      if (clearBatted) m_batted.Clear();
      m_previousPhase = BallFlightPhase::Stationary;
      m_lastFlightTimeSeconds = 0.0;
      m_hasSample = false;
    };
    if (ball.flightTimeSeconds < 0.0) {
      resetSamples(!lifecycle.retainBattedOnBallReset);
      ClearPitchZone();
      return;
    }
    if (!ballVisible || phase == BallFlightPhase::Stationary) {
      if (phase == BallFlightPhase::Pitch && m_previousPhase == BallFlightPhase::Pitch &&
          m_hasSample && ball.flightTimeSeconds >= m_lastFlightTimeSeconds)
        AppendPitchZone(ball.position);
      resetSamples(phase == BallFlightPhase::Stationary &&
          !lifecycle.retainBattedOnBallReset);
      return;
    }
    const bool newPitch = phase == BallFlightPhase::Pitch &&
        (!m_hasSample || m_previousPhase != BallFlightPhase::Pitch ||
         ball.flightTimeSeconds < m_lastFlightTimeSeconds ||
         (ball.flightTimeSeconds <= 1e-6 && m_lastFlightTimeSeconds <= 1e-6 &&
          Physics::LengthSquared(ball.velocity) > 1e-8));
    if (newPitch) ClearPitchZone();
    if (m_hasSample &&
        (ball.flightTimeSeconds < m_lastFlightTimeSeconds ||
         (phase == BallFlightPhase::Pitch && ball.flightTimeSeconds <= 1e-6 &&
          m_lastFlightTimeSeconds <= 1e-6 &&
          Physics::LengthSquared(ball.velocity) > 1e-8) ||
         (phase == BallFlightPhase::Pitch &&
          m_previousPhase != BallFlightPhase::Pitch)))
      resetSamples(phase == BallFlightPhase::Pitch || !lifecycle.retainBattedOnBallReset);

    if (phase == BallFlightPhase::Batted &&
        (newContact || m_batted.Points().empty())) {
      m_batted.Clear();
      if (contactPosition && finite(*contactPosition)) {
        if (m_previousPhase == BallFlightPhase::Pitch)
        {
          m_pitch.Append(*contactPosition, now, true);
          AppendPitchZone(*contactPosition);
        }
        m_batted.Append(*contactPosition, now, true);
      }
    }
    auto &active = phase == BallFlightPhase::Pitch ? m_pitch : m_batted;
    active.Append(ball.position, now);
    if (phase == BallFlightPhase::Pitch) AppendPitchZone(ball.position);
    if (!m_hasSample || ball.flightTimeSeconds > m_lastFlightTimeSeconds + 1e-6)
      active.Touch(now);
    m_pitch.UpdateFade(now);
    m_previousPhase = phase;
    m_lastFlightTimeSeconds = ball.flightTimeSeconds;
    m_hasSample = true;
  }
  [[nodiscard]] const TrajectoryTrail &Pitch() const noexcept {
    return m_pitch;
  }
  [[nodiscard]] const TrajectoryTrail &Batted() const noexcept {
    return m_batted;
  }
  template <class TCallback>
  void ForEachVisible(const OverlayVisibilitySettings &settings,
                      TCallback &&callback) const {
    if (settings.pitchTrajectoryDisplayMode == PitchTrajectoryDisplayMode::Full) {
      if (m_outsideRevision != m_pitch.Revision()) {
        BuildOutsideStrikeZoneTrajectory(m_pitch.Points(), m_zone, m_pitchOutside);
        m_outsideRevision = m_pitch.Revision();
      }
      for (const auto &path : m_pitchOutside)
        callback(TrajectoryRenderSpan{path, m_pitch.FadeScale(), false, false});
    }
    if (settings.pitchTrajectoryDisplayMode != PitchTrajectoryDisplayMode::None)
      for (const auto &path : m_pitchZone)
        callback(TrajectoryRenderSpan{path, 1.0F, true, true});
    if (settings.showBattedTrajectory)
      callback(TrajectoryRenderSpan{m_batted.Points(), 1.0F, true, false});
  }

private:
  void ClearPitchZone() noexcept {
    m_rawPitch.clear();
    m_pitchZone.clear();
    m_zoneContinuous = false;
  }
  void SetZone(const StrikeZoneVolume &zone) {
    if (zone == m_zone) return;
    m_zone = zone;
    m_pitchZone.clear();
    m_zoneContinuous = false;
    for (std::size_t i = 1; i < m_rawPitch.size(); ++i)
      AppendStrikeZoneTrajectorySegment(m_pitchZone, m_zoneContinuous,
                                        m_rawPitch[i - 1], m_rawPitch[i], m_zone);
    BuildOutsideStrikeZoneTrajectory(m_pitch.Points(), m_zone, m_pitchOutside);
    m_outsideRevision = m_pitch.Revision();
  }
  void AppendPitchZone(const Physics::Vector3 &point) {
    if (!m_rawPitch.empty()) {
      if (Physics::LengthSquared(point - m_rawPitch.back()) <= 1e-20) return;
      AppendStrikeZoneTrajectorySegment(m_pitchZone, m_zoneContinuous,
                                        m_rawPitch.back(), point, m_zone);
    }
    m_rawPitch.push_back(point);
  }
  TrajectoryTrail m_pitch{TrajectoryHistory::Recent}, m_batted{TrajectoryHistory::CompleteFlight};
  // Keep exact pitch samples for re-clipping when the zone is edited. This history
  // resets with the next pitch; only the small clipped paths reach the renderer.
  std::vector<Physics::Vector3> m_rawPitch;
  TrajectoryPaths m_pitchZone;
  mutable TrajectoryPaths m_pitchOutside;
  mutable std::uint64_t m_outsideRevision{(std::numeric_limits<std::uint64_t>::max)()};
  StrikeZoneVolume m_zone{};
  bool m_zoneContinuous{};
  BallFlightPhase m_previousPhase{BallFlightPhase::Stationary};
  double m_lastFlightTimeSeconds{0.0};
  bool m_hasSample{false};
  bool m_hasLifecycle{false};
  bool m_retainBattedOnReset{false};
  std::uint64_t m_lastContactSequence{0};
};
} // namespace BaseballSimulatorVR::Common
