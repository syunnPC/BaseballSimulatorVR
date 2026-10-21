#pragma once

#include "../Physics/PitchIntegratorType.hpp"
#include "../Physics/Vector3.hpp"
#include "../Physics/BallProperties.hpp"

namespace BaseballSimulatorVR::Baseball
{
	struct PitchParameters final
	{
		Physics::Vector3 initialVelocity{ 0.0, 0.0, 40.0 };
		double launchElevationDegrees{ 0.0 };
		bool usePhysicalAerodynamics{ false };
		bool underhandToss{ false };
		Physics::BallProperties ballProperties{};
		double releaseHeightMeters{ 1.8 };
		// (-1, 0, 0) is pure backspin for a pitch travelling towards +z.
		Physics::Vector3 spinAxis{ -1.0, 0.0, 0.0 };
		Physics::Vector3 windVelocity{};
		double spinRpm{ 2200.0 };
		// Profile-model coefficients at standard air density (1.225 kg/m^3):
		// drag acceleration = dragCoefficient [1/m] * v^2, and Magnus acceleration =
		// liftCoefficient [1/s] * v * (rpm / 3000) * sin(angle between spin axis and air velocity).
		// A physical baseball drag coefficient is about rho*A*C_D/(2m) = 0.0064 1/m.
		double dragCoefficient{ 0.0064 };
		double liftCoefficient{ 0.12 };
		Physics::PitchIntegratorType integratorType{ Physics::PitchIntegratorType::Rk4 };
		// Use speed- and spin-dependent physical coefficients (Nathan 2017 C_D / C_L fits)
		// instead of the fixed dragCoefficient / liftCoefficient.
		bool useCoefficientTable{ true };
		// Environment air density; the physical model uses it directly and the fixed profile
		// coefficients scale with it relative to standard density.
		double airDensityKilogramsPerCubicMeter{ 1.225 };
		bool enableHeightDependentWind{ false };
		double windShearFactor{ 0.08 };
		bool enableSeamPerturbation{ false };
		double seamPerturbationAmplitude{ 0.08 };
		double seamPerturbationFrequencyHz{ 22.0 };
		double seamPerturbationPhase{ 0.0 };
	};
} // namespace BaseballSimulatorVR::Baseball
