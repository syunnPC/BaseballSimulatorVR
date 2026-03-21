#pragma once

#include "../Physics/Vector3.hpp"

namespace BaseballSimulatorVR::Common
{
	struct BatOverlayState final
	{
		Physics::Vector3 startSimulationPosition{};
		Physics::Vector3 endSimulationPosition{};
		double radiusMeters{ 0.035 };
	};
} // namespace BaseballSimulatorVR::Common
