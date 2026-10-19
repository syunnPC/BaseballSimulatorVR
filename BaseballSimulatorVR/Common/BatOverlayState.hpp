#pragma once

#include "../Physics/Vector3.hpp"
#include "../Physics/BatMaterial.hpp"

namespace BaseballSimulatorVR::Common
{
	struct BatOverlayState final
	{
		Physics::Vector3 startSimulationPosition{};
		Physics::Vector3 endSimulationPosition{};
		double radiusMeters{ 0.035 };
		Physics::BatMaterial material{ Physics::BatMaterial::Wood };
		// Endpoints span the complete knob-to-tip length.
		Physics::Vector3 upSimulationDirection{ 0.0, 1.0, 0.0 };
	};
} // namespace BaseballSimulatorVR::Common
