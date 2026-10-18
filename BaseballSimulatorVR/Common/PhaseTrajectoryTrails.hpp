#pragma once

#include "../Physics/BallState.hpp"
#include "BallFlightPhase.hpp"
#include "OverlayVisibilitySettings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <vector>

namespace BaseballSimulatorVR::Common {
inline constexpr std::size_t MaxTrajectoryPoints = 96;
inline constexpr double TrajectoryFadeDelaySeconds = 0.20;
inline constexpr double TrajectoryFadeDurationSeconds = 1.10;

class TrajectoryTrail final {
public:
  using Clock = std::chrono::steady_clock;
  TrajectoryTrail() { m_points.reserve(MaxTrajectoryPoints + 1); }
  [[nodiscard]] const std::vector<Physics::Vector3> &Points() const noexcept {
    return m_points;
  }
  [[nodiscard]] float FadeScale() const noexcept { return m_fadeScale; }
  void Clear() noexcept {
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
    if (m_points.empty() || separation >= 0.0016 ||
        (forceEndpoint && separation > 1e-16)) {
      m_points.push_back(point);
      if (m_points.size() > MaxTrajectoryPoints)
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
};

class PhaseTrajectoryTrails final {
public:
  using Clock = TrajectoryTrail::Clock;
  void Clear() noexcept {
    m_pitch.Clear();
    m_batted.Clear();
    m_previousPhase = BallFlightPhase::Stationary;
    m_lastFlightTimeSeconds = 0.0;
    m_hasSample = false;
  }
  // Contact and ball positions use the same overlay/simulation coordinates,
  // including any applied mound height. Visibility never controls collection.
  void Update(const Physics::BallState &ball, BallFlightPhase phase,
              bool ballVisible,
              const std::optional<Physics::Vector3> &contactPosition,
              Clock::time_point now) {
    const auto finite = [](const Physics::Vector3 &point) {
      return std::isfinite(point.x) && std::isfinite(point.y) &&
             std::isfinite(point.z);
    };
    if (!ballVisible || phase == BallFlightPhase::Stationary ||
        !std::isfinite(ball.flightTimeSeconds) || !finite(ball.position) ||
        ball.flightTimeSeconds < 0.0) {
      Clear();
      return;
    }
    if (m_hasSample &&
        (ball.flightTimeSeconds < m_lastFlightTimeSeconds ||
         (phase == BallFlightPhase::Pitch && ball.flightTimeSeconds <= 1e-6 &&
          m_lastFlightTimeSeconds <= 1e-6 &&
          Physics::LengthSquared(ball.velocity) > 1e-8) ||
         (phase == BallFlightPhase::Pitch &&
          m_previousPhase != BallFlightPhase::Pitch)))
      Clear();

    if (phase == BallFlightPhase::Batted &&
        m_previousPhase != BallFlightPhase::Batted) {
      m_batted.Clear();
      if (contactPosition && finite(*contactPosition)) {
        if (m_previousPhase == BallFlightPhase::Pitch)
          m_pitch.Append(*contactPosition, now, true);
        m_batted.Append(*contactPosition, now, true);
      }
    }
    auto &active = phase == BallFlightPhase::Pitch ? m_pitch : m_batted;
    active.Append(ball.position, now);
    if (!m_hasSample || ball.flightTimeSeconds > m_lastFlightTimeSeconds + 1e-6)
      active.Touch(now);
    m_pitch.UpdateFade(now);
    m_batted.UpdateFade(now);
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
    if (settings.showPitchTrajectory)
      callback(m_pitch);
    if (settings.showBattedTrajectory)
      callback(m_batted);
  }

private:
  TrajectoryTrail m_pitch, m_batted;
  BallFlightPhase m_previousPhase{BallFlightPhase::Stationary};
  double m_lastFlightTimeSeconds{0.0};
  bool m_hasSample{false};
};
} // namespace BaseballSimulatorVR::Common
