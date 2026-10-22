#pragma once

#include "../Physics/Vector3.hpp"
#include "../Physics/BatMaterial.hpp"

namespace BaseballSimulatorVR::Common
{
	struct BatOverlayState final
	{
		// The endpoints span the whole bat: start at the knob, end at the tip.
		Physics::Vector3 startSimulationPosition{};
		Physics::Vector3 endSimulationPosition{};
		double radiusMeters{ 0.035 };
		Physics::BatMaterial material{ Physics::BatMaterial::Wood };
		// Sets the bat's roll about its axis; only the part perpendicular to the axis is used.
		Physics::Vector3 upSimulationDirection{ 0.0, 1.0, 0.0 };
	};
} // namespace BaseballSimulatorVR::Common
