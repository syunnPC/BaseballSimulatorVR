#pragma once

#include "../Physics/PitchIntegratorType.hpp"
#include "../Physics/Vector3.hpp"

namespace BaseballSimulatorVR::Baseball
{
	struct PitchParameters final
	{
		Physics::Vector3 initialVelocity{ 0.0, 0.0, 40.0 };
		double releaseHeightMeters{ 1.8 };
		Physics::Vector3 spinAxis{ 0.0, 1.0, 0.0 };
		Physics::Vector3 windVelocity{};
		double spinRpm{ 2200.0 };
		double dragCoefficient{ 0.00045 };
		double liftCoefficient{ 0.00075 };
		Physics::PitchIntegratorType integratorType{ Physics::PitchIntegratorType::Rk4 };
		bool useCoefficientTable{ true };
		bool enableHeightDependentWind{ false };
		double windShearFactor{ 0.08 };
		bool enableSeamPerturbation{ false };
		double seamPerturbationAmplitude{ 0.08 };
		double seamPerturbationFrequencyHz{ 22.0 };
		double seamPerturbationPhase{ 0.0 };
	};
} // namespace BaseballSimulatorVR::Baseball
