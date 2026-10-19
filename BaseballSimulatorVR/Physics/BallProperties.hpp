#pragma once

#include <cmath>

namespace BaseballSimulatorVR::Physics
{
	struct BallProperties final
	{
		double radiusMeters{ 0.037 };
		double massKilograms{ 0.145 };
		// Base normal COR at 71.5264 m/s closing speed, before bat corrections.
		double restitutionAtReferenceSpeed{ 0.45 };

		[[nodiscard]] bool IsValid() const noexcept
		{
			return std::isfinite(radiusMeters) && std::isfinite(massKilograms) &&
			    radiusMeters >= 0.01 && radiusMeters <= 0.10 &&
			    massKilograms >= 0.01 && massKilograms <= 1.0 &&
			    std::isfinite(restitutionAtReferenceSpeed) &&
			    restitutionAtReferenceSpeed >= 0.0 && restitutionAtReferenceSpeed <= 1.0;
		}
	};
}
