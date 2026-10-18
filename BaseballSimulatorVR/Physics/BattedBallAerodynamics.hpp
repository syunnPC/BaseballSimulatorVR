#pragma once

#include "Vector3.hpp"

#include <cmath>
#include <numbers>

namespace BaseballSimulatorVR::Physics
{
	inline constexpr double AerodynamicBaseballRadiusMeters = .037;
	inline constexpr double AerodynamicBaseballMassKilograms = .145;
	inline constexpr double StandardAirDensityKilogramsPerCubicMeter = 1.225;

	struct BattedBallAerodynamicCoefficients final
	{
		double drag{};
		double lift{};
		double transverseSpinFactor{};
		Vector3 liftDirection{};
	};

	// Nathan (2017), Analysis of Baseball Trajectories, Eqs. 10-11.
	// Dimensionless C_D and C_L for batted-ball flight.
	[[nodiscard]] inline BattedBallAerodynamicCoefficients ResolveBattedBallAerodynamicCoefficients(
	    const Vector3& relativeAirVelocity, const Vector3& spinAxis, double spinRpm)
	{
		BattedBallAerodynamicCoefficients result;
		result.drag = .297 + .0292 * (std::abs(spinRpm) / 1000.);
		const double speed = Length(relativeAirVelocity);
		if (speed <= 1e-6)
		{
			return result;
		}

		const auto airDirection = relativeAirVelocity / speed;
		// Only transverse spin produces Magnus lift. Nathan's fitted data
		// assume initial transverse spin; this extends it continuously to gyrospin.
		const auto angularVelocity =
		    NormalizeOrZero(spinAxis) * (spinRpm * (2. * std::numbers::pi / 60.));
		const auto transverseAngularVelocity = Cross(angularVelocity, airDirection);
		const double transverseSpin = Length(transverseAngularVelocity);
		if (transverseSpin <= 1e-12)
		{
			return result;
		}

		result.transverseSpinFactor = AerodynamicBaseballRadiusMeters * transverseSpin / speed;
		result.lift =
		    1.120 * result.transverseSpinFactor / (.583 + 2.333 * result.transverseSpinFactor);
		result.liftDirection = transverseAngularVelocity / transverseSpin;
		return result;
	}

	[[nodiscard]] inline Vector3 ComputeBattedBallAerodynamicAcceleration(
	    const Vector3& relativeAirVelocity, const Vector3& spinAxis, double spinRpm)
	{
		const double speed = Length(relativeAirVelocity);
		if (speed <= 1e-6)
		{
			return {};
		}
		const auto coefficients =
		    ResolveBattedBallAerodynamicCoefficients(relativeAirVelocity, spinAxis, spinRpm);
		constexpr double accelerationScale = StandardAirDensityKilogramsPerCubicMeter *
		                                     std::numbers::pi * AerodynamicBaseballRadiusMeters *
		                                     AerodynamicBaseballRadiusMeters /
		                                     (2. * AerodynamicBaseballMassKilograms);
		const double dynamicPressurePerMass = accelerationScale * speed * speed;
		return -(relativeAirVelocity / speed) * (dynamicPressurePerMass * coefficients.drag) +
		       coefficients.liftDirection * (dynamicPressurePerMass * coefficients.lift);
	}
} // namespace BaseballSimulatorVR::Physics
