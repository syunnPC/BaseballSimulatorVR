#pragma once

#include "Vector3.hpp"
#include "BallAerodynamicsModel.hpp"
#include "BallProperties.hpp"

namespace BaseballSimulatorVR::Physics
{
	struct BallState final
	{
		Vector3 position{ 0.0, 1.8, 0.0 };
		Vector3 velocity{};
		Vector3 spinAxis{ 0.0, 1.0, 0.0 };
		double spinRpm{ 0.0 };
		double flightTimeSeconds{ 0.0 };
		BallAerodynamicsModel aerodynamicsModel{ BallAerodynamicsModel::PitchProfile };
		BallProperties properties{};
	};
} // namespace BaseballSimulatorVR::Physics
