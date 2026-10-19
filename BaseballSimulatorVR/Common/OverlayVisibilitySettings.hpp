#pragma once

#include <string_view>

namespace BaseballSimulatorVR::Common
{
	enum class StrikeZoneDisplayMode { None, Plane, Box };
	enum class PitchTrajectoryDisplayMode { None, Full, ZoneOnly };

	[[nodiscard]] constexpr std::string_view GetPitchTrajectoryDisplayModeToken(
	    const PitchTrajectoryDisplayMode mode) noexcept
	{
		switch (mode)
		{
		case PitchTrajectoryDisplayMode::None: return "None";
		case PitchTrajectoryDisplayMode::Full: return "Full";
		case PitchTrajectoryDisplayMode::ZoneOnly: return "ZoneOnly";
		}
		return {};
	}

	[[nodiscard]] constexpr std::string_view GetStrikeZoneDisplayModeToken(
	    const StrikeZoneDisplayMode mode) noexcept
	{
		switch (mode)
		{
		case StrikeZoneDisplayMode::None: return "None";
		case StrikeZoneDisplayMode::Plane: return "Plane";
		case StrikeZoneDisplayMode::Box: return "Box";
		}
		return {};
	}

	[[nodiscard]] constexpr PitchTrajectoryDisplayMode NextPitchTrajectoryDisplayMode(
	    const PitchTrajectoryDisplayMode mode) noexcept
	{
		switch (mode)
		{
		case PitchTrajectoryDisplayMode::None: return PitchTrajectoryDisplayMode::Full;
		case PitchTrajectoryDisplayMode::Full: return PitchTrajectoryDisplayMode::ZoneOnly;
		case PitchTrajectoryDisplayMode::ZoneOnly: return PitchTrajectoryDisplayMode::None;
		}
		return mode;
	}

	[[nodiscard]] constexpr StrikeZoneDisplayMode NextStrikeZoneDisplayMode(
	    const StrikeZoneDisplayMode mode) noexcept
	{
		switch (mode)
		{
		case StrikeZoneDisplayMode::None: return StrikeZoneDisplayMode::Plane;
		case StrikeZoneDisplayMode::Plane: return StrikeZoneDisplayMode::Box;
		case StrikeZoneDisplayMode::Box: return StrikeZoneDisplayMode::None;
		}
		return mode;
	}

	struct OverlayVisibilitySettings final
	{
		PitchTrajectoryDisplayMode pitchTrajectoryDisplayMode{ PitchTrajectoryDisplayMode::Full };
		bool showBattedTrajectory{ true };
		StrikeZoneDisplayMode strikeZoneDisplayMode{ StrikeZoneDisplayMode::Box };
		bool showFloorMesh{ true };
	};
} // namespace BaseballSimulatorVR::Common
