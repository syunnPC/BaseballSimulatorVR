#pragma once

#include "Vector3.hpp"
#include "BallProperties.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace BaseballSimulatorVR::Physics
{
	inline constexpr double AerodynamicBaseballRadiusMeters = .037;
	inline constexpr double AerodynamicBaseballMassKilograms = .145;
	inline constexpr double StandardAirDensityKilogramsPerCubicMeter = 1.225;
	// Spin decay time constant for a flying baseball (Nathan's trajectory calculator uses
	// roughly 25-30 s); only the physical model applies it.
	inline constexpr double PhysicalSpinDecayTimeConstantSeconds = 25.0;

	// Moist-air density from altitude (standard-atmosphere pressure), air
	// temperature and relative humidity: rho = p_d / (R_d T) + e / (R_v T),
	// scaled so sea level, 15 C, dry air is exactly the standard 1.225 kg/m^3
	// that the fitted presets assume.
	[[nodiscard]] inline double ComputeAirDensity(
	    const double altitudeMeters, const double temperatureCelsius, const double relativeHumidityPercent) noexcept
	{
		const auto moistAirDensity = [](const double altitude, const double celsius, const double humidity)
		{
			const double pressure = 101325.0 * std::pow(1.0 - 2.25577e-5 * altitude, 5.25588);
			const double kelvin = celsius + 273.15;
			const double saturationPressure = 610.94 * std::exp(17.625 * celsius / (celsius + 243.04));
			const double vaporPressure = std::clamp(humidity, 0.0, 100.0) / 100.0 * saturationPressure;
			return (pressure - vaporPressure) / (287.058 * kelvin) + vaporPressure / (461.495 * kelvin);
		};
		return StandardAirDensityKilogramsPerCubicMeter *
		    (moistAirDensity(altitudeMeters, temperatureCelsius, relativeHumidityPercent) /
		        moistAirDensity(0.0, 15.0, 0.0));
	}

	[[nodiscard]] inline bool IsPlausibleAirDensity(const double density) noexcept
	{
		return std::isfinite(density) && density >= 0.5 && density <= 1.6;
	}

	// rho * A / (2 m): converts dimensionless C_D / C_L into acceleration per speed squared.
	[[nodiscard]] inline double AerodynamicAccelerationScale(
	    const BallProperties& properties, const double airDensity) noexcept
	{
		return airDensity * std::numbers::pi * properties.radiusMeters * properties.radiusMeters /
		    (2. * properties.massKilograms);
	}

	// Nathan (2017) drag fit; the spin term is the RPM-dependent part.
	[[nodiscard]] inline double NathanDragCoefficient(const double spinRpm) noexcept
	{
		return .297 + .0292 * (std::abs(spinRpm) / 1000.);
	}

	// Nathan (2017) lift fit as a function of the transverse spin factor S = r w / v.
	[[nodiscard]] inline double NathanLiftCoefficient(const double spinFactor) noexcept
	{
		return spinFactor > 0. ? 1.120 * spinFactor / (.583 + 2.333 * spinFactor) : 0.;
	}

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
	    const Vector3& relativeAirVelocity, const Vector3& spinAxis, double spinRpm,
	    const BallProperties& properties = {})
	{
		BattedBallAerodynamicCoefficients result;
		result.drag = NathanDragCoefficient(spinRpm);
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

		result.transverseSpinFactor = properties.radiusMeters * transverseSpin / speed;
		result.lift = NathanLiftCoefficient(result.transverseSpinFactor);
		result.liftDirection = transverseAngularVelocity / transverseSpin;
		return result;
	}

	[[nodiscard]] inline Vector3 ComputeBattedBallAerodynamicAcceleration(
	    const Vector3& relativeAirVelocity, const Vector3& spinAxis, double spinRpm,
	    const BallProperties& properties = {},
	    const double airDensity = StandardAirDensityKilogramsPerCubicMeter)
	{
		const double speed = Length(relativeAirVelocity);
		if (speed <= 1e-6)
		{
			return {};
		}
		const auto coefficients =
		    ResolveBattedBallAerodynamicCoefficients(relativeAirVelocity, spinAxis, spinRpm, properties);
		const double accelerationScale = AerodynamicAccelerationScale(properties, airDensity);
		const double dynamicPressurePerMass = accelerationScale * speed * speed;
		return -(relativeAirVelocity / speed) * (dynamicPressurePerMass * coefficients.drag) +
		       coefficients.liftDirection * (dynamicPressurePerMass * coefficients.lift);
	}
} // namespace BaseballSimulatorVR::Physics
