#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace BaseballSimulatorVR::Common
{
	// Standing anatomical approximations: below the knee, and shoulder/waist midpoint.
	// HMD height cannot measure individual knee, shoulder or waist landmarks.
	inline constexpr double StrikeZoneBottomHeightFraction = 0.280;
	inline constexpr double StrikeZoneTopHeightFraction = 0.700;
	inline constexpr double EyeHeightToFullHeightFraction = 0.93;
	inline constexpr double MinimumCalibratedPlayerHeightMeters = 1.00;
	inline constexpr double MaximumCalibratedPlayerHeightMeters = 2.50;

	[[nodiscard]] inline double EstimatePlayerHeightFromEyeHeight(
	    const double eyeHeightMeters) noexcept
	{
		if (!std::isfinite(eyeHeightMeters) || (eyeHeightMeters <= 0.0))
		{
			return 0.0;
		}
		const double height = eyeHeightMeters / EyeHeightToFullHeightFraction;
		return std::isfinite(height) ? height : 0.0;
	}

	struct StrikeZoneFromHeight final
	{
		double bottomMeters{ 0.0 };
		double heightMeters{ 0.0 };
	};

	[[nodiscard]] inline StrikeZoneFromHeight ComputeStrikeZoneFromPlayerHeight(
	    const double playerHeightMeters) noexcept
	{
		if (!std::isfinite(playerHeightMeters) || (playerHeightMeters <= 0.0))
		{
			return {};
		}
		return StrikeZoneFromHeight{
			playerHeightMeters * StrikeZoneBottomHeightFraction,
			playerHeightMeters *
			    (StrikeZoneTopHeightFraction - StrikeZoneBottomHeightFraction)
		};
	}

	struct FloorEyeHeightSample final
	{
		// Average of both raw eye positions relative to a known physical floor.
		// Never populate from a camera pose after artificial locomotion/rotation.
		double heightMeters{ 0.0 };
		double sampleTimeSeconds{ 0.0 };
		// Changes when the physical floor/reference coordinate system changes.
		std::uint64_t referenceSpaceRevision{ 0 };
		bool isFloorReferenced{ false };
		// Requires OpenXR POSITION_VALID and POSITION_TRACKED, and current input.
		bool isPositionTracked{ false };
	};

	struct StrikeZoneHeightCalibration final
	{
		double eyeHeightMeters{ 0.0 };
		double playerHeightMeters{ 0.0 };
		StrikeZoneFromHeight strikeZone{};
	};
	enum class StrikeZoneCalibrationStatus
	{
		WaitingForFloor,
		WaitingForTracking,
		InvalidHeight,
		Moving,
		Steady,
		Complete,
		Manual,
		Saved
	};
	struct StrikeZoneCalibrationProgress final
	{
		StrikeZoneCalibrationStatus status{ StrikeZoneCalibrationStatus::WaitingForFloor };
		double stableDurationSeconds{};
		double fraction{};
	};

	class StrikeZoneHeightCalibrator final
	{
	  public:
		static constexpr double RequiredStableDurationSeconds = 3.0;
		static constexpr std::size_t RequiredSampleCount = 120;
		static constexpr double MaximumSampleGapSeconds = 0.10;
		static constexpr double MaximumHeightRangeMeters = 0.04;

		void Reset() noexcept
		{
			m_isComplete = false;
			ClearWindow();
			m_status = StrikeZoneCalibrationStatus::WaitingForFloor;
		}
		void UseManualZone() noexcept
		{
			m_isComplete = true;
			ClearWindow();
			m_status = StrikeZoneCalibrationStatus::Manual;
		}
		void UseSavedZone() noexcept
		{
			m_isComplete = true;
			ClearWindow();
			m_status = StrikeZoneCalibrationStatus::Saved;
		}

		[[nodiscard]] bool IsComplete() const noexcept { return m_isComplete; }
		[[nodiscard]] std::size_t GetSampleCount() const noexcept { return m_sampleCount; }
		[[nodiscard]] double GetStableDurationSeconds() const noexcept
		{
			return m_sampleCount == 0 ? 0.0 : m_lastTimeSeconds - m_startTimeSeconds;
		}
		[[nodiscard]] StrikeZoneCalibrationProgress GetProgress() const noexcept
		{
			const double duration = GetStableDurationSeconds();
			const double fraction = m_isComplete ? 1.0 : (std::min)(
			    std::clamp(duration / RequiredStableDurationSeconds, 0.0, 1.0),
			    std::clamp(static_cast<double>(m_sampleCount) / RequiredSampleCount, 0.0, 1.0));
			return { m_status, (std::min)(duration, RequiredStableDurationSeconds), fraction };
		}

		// Returns a result at most once per Reset(). Until then the caller keeps
		// applying its current zone; only a complete, valid result replaces it.
		[[nodiscard]] std::optional<StrikeZoneHeightCalibration> AddSample(
		    const FloorEyeHeightSample& sample) noexcept
		{
			if (m_isComplete)
			{
				return std::nullopt;
			}
			const double playerHeight = EstimatePlayerHeightFromEyeHeight(sample.heightMeters);
			if (!sample.isFloorReferenced)
			{
				ClearWindow();
				m_status = StrikeZoneCalibrationStatus::WaitingForFloor;
				return std::nullopt;
			}
			if (!sample.isPositionTracked || !std::isfinite(sample.sampleTimeSeconds))
			{
				ClearWindow();
				m_status = StrikeZoneCalibrationStatus::WaitingForTracking;
				return std::nullopt;
			}
			if ((playerHeight < MinimumCalibratedPlayerHeightMeters) ||
			    (playerHeight > MaximumCalibratedPlayerHeightMeters))
			{
				ClearWindow();
				m_status = StrikeZoneCalibrationStatus::InvalidHeight;
				return std::nullopt;
			}

			if (m_sampleCount > 0)
			{
				if (sample.referenceSpaceRevision == m_referenceSpaceRevision &&
				    sample.sampleTimeSeconds == m_lastTimeSeconds)
				{
					return std::nullopt; // Re-reading one XR frame is not a new sample.
				}
				const double gap = sample.sampleTimeSeconds - m_lastTimeSeconds;
				const double range = (std::max)(m_maxHeightMeters, sample.heightMeters) -
				                     (std::min)(m_minHeightMeters, sample.heightMeters);
				if (sample.referenceSpaceRevision != m_referenceSpaceRevision ||
				    (gap <= 0.0) || (gap > MaximumSampleGapSeconds + 1e-9) ||
				    (range > MaximumHeightRangeMeters + 1e-9))
				{
					const bool moved = range > MaximumHeightRangeMeters + 1e-9;
					ClearWindow();
					m_recoveringFromMotion = moved;
				}
			}

			if (m_sampleCount == 0)
			{
				m_startTimeSeconds = sample.sampleTimeSeconds;
				m_referenceSpaceRevision = sample.referenceSpaceRevision;
				m_minHeightMeters = sample.heightMeters;
				m_maxHeightMeters = sample.heightMeters;
			}
			m_lastTimeSeconds = sample.sampleTimeSeconds;
			m_minHeightMeters = (std::min)(m_minHeightMeters, sample.heightMeters);
			m_maxHeightMeters = (std::max)(m_maxHeightMeters, sample.heightMeters);
			++m_sampleCount;
			m_meanHeightMeters += (sample.heightMeters - m_meanHeightMeters) /
			                      static_cast<double>(m_sampleCount);
			if (GetStableDurationSeconds() >= 0.5) m_recoveringFromMotion = false;
			m_status = m_recoveringFromMotion ? StrikeZoneCalibrationStatus::Moving
			                                  : StrikeZoneCalibrationStatus::Steady;
			if ((m_sampleCount < RequiredSampleCount) ||
			    (GetStableDurationSeconds() + 1e-9 < RequiredStableDurationSeconds))
			{
				return std::nullopt;
			}

			m_isComplete = true;
			m_status = StrikeZoneCalibrationStatus::Complete;
			const double calibratedHeight = EstimatePlayerHeightFromEyeHeight(m_meanHeightMeters);
			return StrikeZoneHeightCalibration{
				m_meanHeightMeters, calibratedHeight,
				ComputeStrikeZoneFromPlayerHeight(calibratedHeight)
			};
		}

	  private:
		void ClearWindow() noexcept
		{
			m_sampleCount = 0;
			m_startTimeSeconds = 0.0;
			m_lastTimeSeconds = 0.0;
			m_meanHeightMeters = 0.0;
			m_recoveringFromMotion = false;
		}

		bool m_isComplete{ false };
		bool m_recoveringFromMotion{ false };
		StrikeZoneCalibrationStatus m_status{ StrikeZoneCalibrationStatus::WaitingForFloor };
		std::size_t m_sampleCount{ 0 };
		std::uint64_t m_referenceSpaceRevision{ 0 };
		double m_startTimeSeconds{ 0.0 };
		double m_lastTimeSeconds{ 0.0 };
		double m_minHeightMeters{ 0.0 };
		double m_maxHeightMeters{ 0.0 };
		double m_meanHeightMeters{ 0.0 };
	};
}
