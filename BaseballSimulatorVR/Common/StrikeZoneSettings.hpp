#pragma once

#include <algorithm>
#include <cmath>

namespace BaseballSimulatorVR::Common
{
	inline constexpr double MinimumStrikeZoneBottomMeters = 0.20;
	inline constexpr double MaximumStrikeZoneBottomMeters = 1.50;
	inline constexpr double MaximumStrikeZoneTopMeters = 2.50;
	inline constexpr double MinimumStrikeZoneHeightMeters = 0.20;
	inline constexpr double MaximumStrikeZoneHeightMeters = 1.60;
	inline constexpr double MinimumStrikeZoneWidthMeters = 0.20;
	inline constexpr double MaximumStrikeZoneWidthMeters = 0.80;
	inline constexpr double StrikeZoneAdjustmentStepMeters = 0.01;

	struct StrikeZoneRuntimeSettings final
	{
		double bottomMeters{0.55};
		double heightMeters{1.0};
		double widthMeters{0.44};
	};

	[[nodiscard]] inline bool IsValidStrikeZoneSettings(const StrikeZoneRuntimeSettings& zone) noexcept
	{
		return std::isfinite(zone.bottomMeters) && std::isfinite(zone.heightMeters) &&
		    std::isfinite(zone.widthMeters) &&
		    zone.bottomMeters >= MinimumStrikeZoneBottomMeters &&
		    zone.bottomMeters <= MaximumStrikeZoneBottomMeters &&
		    zone.heightMeters >= MinimumStrikeZoneHeightMeters &&
		    zone.heightMeters <= MaximumStrikeZoneHeightMeters &&
		    zone.bottomMeters + zone.heightMeters <= MaximumStrikeZoneTopMeters &&
		    zone.widthMeters >= MinimumStrikeZoneWidthMeters &&
		    zone.widthMeters <= MaximumStrikeZoneWidthMeters;
	}

	// Each adjustment keeps the opposite boundary fixed.
	[[nodiscard]] inline bool AdjustStrikeZoneBoundary(StrikeZoneRuntimeSettings& zone,
	    bool upper, double deltaMeters) noexcept
	{
		if (!IsValidStrikeZoneSettings(zone) || !std::isfinite(deltaMeters)) return false;
		const double top = zone.bottomMeters + zone.heightMeters;
		if (upper)
		{
			const double next = std::clamp(top + deltaMeters,
			    zone.bottomMeters + MinimumStrikeZoneHeightMeters,
			    (std::min)(MaximumStrikeZoneTopMeters, zone.bottomMeters + MaximumStrikeZoneHeightMeters));
			if (std::abs(next - top) <= 1e-9) return false;
			zone.heightMeters = std::clamp(next - zone.bottomMeters,
			    MinimumStrikeZoneHeightMeters, MaximumStrikeZoneHeightMeters);
		}
		else
		{
			const double next = std::clamp(zone.bottomMeters + deltaMeters,
			    (std::max)(MinimumStrikeZoneBottomMeters, top - MaximumStrikeZoneHeightMeters),
			    (std::min)(MaximumStrikeZoneBottomMeters, top - MinimumStrikeZoneHeightMeters));
			if (std::abs(next - zone.bottomMeters) <= 1e-9) return false;
			zone.bottomMeters = next;
			zone.heightMeters = std::clamp(top - next,
			    MinimumStrikeZoneHeightMeters, MaximumStrikeZoneHeightMeters);
		}
		return true;
	}
}
