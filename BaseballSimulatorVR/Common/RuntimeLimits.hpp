#pragma once

namespace BaseballSimulatorVR::Common::RuntimeLimits
{
	constexpr int DuelZoneColumns = 5;
	constexpr int DuelZoneRows = 5;
	constexpr int DuelZoneCount = DuelZoneColumns * DuelZoneRows;

	constexpr double MinimumPitchDistanceMeters = 16.0;
	constexpr double MaximumPitchDistanceMeters = 21.0;

	constexpr double MinimumDuelPitchIntervalSeconds = 0.0;
	constexpr double MaximumDuelPitchIntervalSeconds = 3.0;

	constexpr double MinimumPlaybackSpeedPercent = 0.0;
	constexpr double MaximumPlaybackSpeedPercent = 200.0;
} // namespace BaseballSimulatorVR::Common::RuntimeLimits
