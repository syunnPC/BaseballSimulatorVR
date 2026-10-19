#pragma once

#include "Vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace BaseballSimulatorVR::Physics
{
	struct BatProfilePoint final
	{
		double fraction;
		double radiusRatio;
	};

	inline constexpr double BatGripStartRatio = .027;
	inline constexpr double BatGripEndRatio = .275;
	inline constexpr double BatGripCenterRatio = .14;

	// Axial fractions run from the knob face (0) to the barrel tip (1).
	inline constexpr std::array BatBodyProfile{
		BatProfilePoint{.000, .56}, BatProfilePoint{.004, .65},
		BatProfilePoint{.012, .65}, BatProfilePoint{.020, .52},
		BatProfilePoint{.027, .35}, BatProfilePoint{.045, .33},
		BatProfilePoint{.100, .32}, BatProfilePoint{.200, .34},
		BatProfilePoint{.275, .36}, BatProfilePoint{.320, .39},
		BatProfilePoint{.400, .50}, BatProfilePoint{.480, .67},
		BatProfilePoint{.550, .84}, BatProfilePoint{.610, .96},
		BatProfilePoint{.660, 1.0}, BatProfilePoint{.750, 1.0},
		BatProfilePoint{.850, 1.0}, BatProfilePoint{.930, 1.0},
		BatProfilePoint{.965, .99}, BatProfilePoint{.980, .94},
		BatProfilePoint{.990, .80}, BatProfilePoint{.996, .55},
		BatProfilePoint{1.000, 0.0}
	};

	[[nodiscard]] inline double BatRadiusAtFraction(double fraction, double maximumRadiusMeters) noexcept
	{
		fraction = std::clamp(fraction, 0.0, 1.0);
		for (std::size_t i = 1; i < BatBodyProfile.size(); ++i)
		{
			const auto& a = BatBodyProfile[i - 1];
			const auto& b = BatBodyProfile[i];
			if (fraction <= b.fraction)
				return maximumRadiusMeters * std::lerp(a.radiusRatio, b.radiusRatio,
				    (fraction - a.fraction) / (b.fraction - a.fraction));
		}
		return 0.0;
	}

	struct BatEndpoints final
	{
		Vector3 knob;
		Vector3 tip;
	};

	// axis is a unit vector pointing from the grip toward the barrel.
	[[nodiscard]] inline BatEndpoints BatEndpointsFromGrip(
	    const Vector3& grip, const Vector3& axis, double totalLengthMeters) noexcept
	{
		const auto knob = grip - axis * (BatGripCenterRatio * totalLengthMeters);
		return {knob, knob + axis * totalLengthMeters};
	}
}
