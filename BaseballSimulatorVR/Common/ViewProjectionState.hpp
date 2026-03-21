#pragma once

#include "../Physics/Vector3.hpp"

namespace BaseballSimulatorVR::Common
{
	struct ViewProjectionState final
	{
		bool isValid{ false };
		Physics::Vector3 position{};
		Physics::Vector3 orientation{};
		double orientationW{ 1.0 };
		double fovLeftRadians{ -0.75 };
		double fovRightRadians{ 0.75 };
		double fovUpRadians{ 0.75 };
		double fovDownRadians{ -0.75 };
	};
} // namespace BaseballSimulatorVR::Common
