#include "PitchPhysicsEngine.hpp"

#include "../Common/MathUtilities.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace BaseballSimulatorVR::Physics
{
	namespace
	{
		constexpr std::array<double, 4> SpeedBreakpoints{ 20.0, 30.0, 40.0, 50.0 };
		constexpr std::array<double, 3> SpinBreakpoints{ 0.0, 1.0, 2.0 };

		constexpr std::array<std::array<double, 3>, 4> DragCoefficientTable{
			std::array<double, 3>{ 0.00054, 0.00055, 0.00056 },
			std::array<double, 3>{ 0.00049, 0.00050, 0.00051 },
			std::array<double, 3>{ 0.00045, 0.00046, 0.00047 },
			std::array<double, 3>{ 0.00043, 0.00044, 0.00045 },
		};

		constexpr std::array<std::array<double, 3>, 4> LiftCoefficientTable{
			std::array<double, 3>{ 0.00036, 0.00052, 0.00064 },
			std::array<double, 3>{ 0.00044, 0.00062, 0.00078 },
			std::array<double, 3>{ 0.00050, 0.00070, 0.00090 },
			std::array<double, 3>{ 0.00056, 0.00078, 0.00100 },
		};

		constexpr double ReferenceWindHeightMeters = 1.0;
		constexpr double SeamDragInfluence = 0.20;
		constexpr double SeamLiftInfluence = 0.40;
		constexpr double SeamSideForceScale = 0.28;
		constexpr double TwoPi = 2.0 * std::numbers::pi;

		struct BracketingIndices final
		{
			std::size_t lower{ 0 };
			std::size_t upper{ 0 };
			double blend{ 0.0 };
		};

		template <std::size_t N>
		    requires(N > 1)
		[[nodiscard]] BracketingIndices LocateBracketingIndices(const std::array<double, N>& breakpoints, double value)
		{
			if (value <= breakpoints.front())
			{
				return BracketingIndices{ 0, 0, 0.0 };
			}

			if (value >= breakpoints.back())
			{
				return BracketingIndices{ N - 1, N - 1, 0.0 };
			}

			for (std::size_t index = 0; (index + 1) < N; ++index)
			{
				const double lowerValue = breakpoints[index];
				const double upperValue = breakpoints[index + 1];
				if ((value >= lowerValue) && (value <= upperValue))
				{
					const double range = upperValue - lowerValue;
					if (range <= 0.0)
					{
						return BracketingIndices{ index, index + 1, 0.0 };
					}

					const double blend = (value - lowerValue) / range;
					return BracketingIndices{ index, index + 1, std::clamp(blend, 0.0, 1.0) };
				}
			}

			return BracketingIndices{ N - 1, N - 1, 0.0 };
		}

		template <std::size_t RowCount, std::size_t ColumnCount>
		    requires((RowCount > 1) && (ColumnCount > 1))
		[[nodiscard]] double Interpolate2D(
		    const std::array<std::array<double, ColumnCount>, RowCount>& table,
		    const std::array<double, RowCount>& rowBreakpoints,
		    const std::array<double, ColumnCount>& columnBreakpoints,
		    double rowValue,
		    double columnValue)
		{
			const BracketingIndices row = LocateBracketingIndices(rowBreakpoints, rowValue);
			const BracketingIndices column = LocateBracketingIndices(columnBreakpoints, columnValue);

			const double rowLowerColumnLower = table[row.lower][column.lower];
			const double rowLowerColumnUpper = table[row.lower][column.upper];
			const double rowUpperColumnLower = table[row.upper][column.lower];
			const double rowUpperColumnUpper = table[row.upper][column.upper];

			const double lowerRowInterpolated = Common::Math::Lerp(
			    rowLowerColumnLower,
			    rowLowerColumnUpper,
			    column.blend);
			const double upperRowInterpolated = Common::Math::Lerp(
			    rowUpperColumnLower,
			    rowUpperColumnUpper,
			    column.blend);
			return Common::Math::Lerp(
			    lowerRowInterpolated,
			    upperRowInterpolated,
			    row.blend);
		}
	} // namespace

	PitchPhysicsEngine::PitchPhysicsEngine(Vector3 gravity)
	    : m_gravity(gravity)
	{
	}

	BallState PitchPhysicsEngine::Step(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, std::chrono::duration<double> deltaTime) const
	{
		if (deltaTime <= std::chrono::duration<double>::zero())
		{
			return currentState;
		}

		const double deltaSeconds = deltaTime.count();
		BallState nextState = currentState;
		switch (pitchParameters.integratorType)
		{
			case PitchIntegratorType::SemiImplicitEuler:
				nextState = StepSemiImplicitEuler(currentState, pitchParameters, deltaSeconds);
				break;
			case PitchIntegratorType::Rk4:
				nextState = StepRk4(currentState, pitchParameters, deltaSeconds);
				break;
			default:
				nextState = StepSemiImplicitEuler(currentState, pitchParameters, deltaSeconds);
				break;
		}

		return nextState;
	}

	BallState PitchPhysicsEngine::StepSemiImplicitEuler(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, double deltaSeconds) const
	{
		const Vector3 aerodynamicAcceleration = ComputeAerodynamicAcceleration(currentState, pitchParameters);
		const Vector3 totalAcceleration = m_gravity + aerodynamicAcceleration;

		BallState nextState = currentState;
		nextState.velocity += totalAcceleration * deltaSeconds;
		nextState.position += nextState.velocity * deltaSeconds;
		nextState.flightTimeSeconds += deltaSeconds;
		return nextState;
	}

	BallState PitchPhysicsEngine::StepRk4(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, double deltaSeconds) const
	{
		const auto accelerationAtState = [&](const Vector3& position, const Vector3& velocity, double flightTimeSeconds) -> Vector3
		{
			BallState sampleState = currentState;
			sampleState.position = position;
			sampleState.velocity = velocity;
			sampleState.flightTimeSeconds = flightTimeSeconds;
			return m_gravity + ComputeAerodynamicAcceleration(sampleState, pitchParameters);
		};

		const Vector3& position = currentState.position;
		const Vector3& velocity = currentState.velocity;
		const double flightTimeSeconds = currentState.flightTimeSeconds;

		const Vector3 k1Velocity = accelerationAtState(position, velocity, flightTimeSeconds);
		const Vector3 k1Position = velocity;

		const Vector3 positionAtHalfStepK1 = position + (k1Position * (0.5 * deltaSeconds));
		const Vector3 velocityAtHalfStepK1 = velocity + (k1Velocity * (0.5 * deltaSeconds));
		const Vector3 k2Velocity = accelerationAtState(positionAtHalfStepK1, velocityAtHalfStepK1, flightTimeSeconds + (0.5 * deltaSeconds));
		const Vector3 k2Position = velocityAtHalfStepK1;

		const Vector3 positionAtHalfStepK2 = position + (k2Position * (0.5 * deltaSeconds));
		const Vector3 velocityAtHalfStepK2 = velocity + (k2Velocity * (0.5 * deltaSeconds));
		const Vector3 k3Velocity = accelerationAtState(positionAtHalfStepK2, velocityAtHalfStepK2, flightTimeSeconds + (0.5 * deltaSeconds));
		const Vector3 k3Position = velocityAtHalfStepK2;

		const Vector3 positionAtFullStepK3 = position + (k3Position * deltaSeconds);
		const Vector3 velocityAtFullStepK3 = velocity + (k3Velocity * deltaSeconds);
		const Vector3 k4Velocity = accelerationAtState(positionAtFullStepK3, velocityAtFullStepK3, flightTimeSeconds + deltaSeconds);
		const Vector3 k4Position = velocityAtFullStepK3;

		const Vector3 velocityDelta = (k1Velocity + (2.0 * k2Velocity) + (2.0 * k3Velocity) + k4Velocity) * (deltaSeconds / 6.0);
		const Vector3 positionDelta = (k1Position + (2.0 * k2Position) + (2.0 * k3Position) + k4Position) * (deltaSeconds / 6.0);

		BallState nextState = currentState;
		nextState.velocity = velocity + velocityDelta;
		nextState.position = position + positionDelta;
		nextState.flightTimeSeconds += deltaSeconds;
		return nextState;
	}

	Vector3 PitchPhysicsEngine::ComputeAerodynamicAcceleration(const BallState& currentState, const Baseball::PitchParameters& pitchParameters) const
	{
		Vector3 windVelocity = pitchParameters.windVelocity;
		if (pitchParameters.enableHeightDependentWind)
		{
			const double heightDelta = std::max(currentState.position.y - ReferenceWindHeightMeters, 0.0);
			windVelocity.x += pitchParameters.windShearFactor * heightDelta;
		}

		const Vector3 relativeAirVelocity = currentState.velocity - windVelocity;
		const double speed = Length(relativeAirVelocity);
		if (speed <= 1e-6)
		{
			return Vector3{};
		}

		const Vector3 velocityDirection = NormalizeOrZero(relativeAirVelocity);
		const Vector3 normalizedSpinAxis = NormalizeOrZero(pitchParameters.spinAxis);
		const double spinFactor = std::clamp(pitchParameters.spinRpm / 3000.0, 0.0, 2.0);
		const bool hasMeaningfulSpin =
		    (spinFactor > 1e-6) &&
		    (LengthSquared(normalizedSpinAxis) > 1e-12);
		auto [dragCoefficient, liftCoefficient] = ResolveAerodynamicCoefficients(speed, spinFactor, pitchParameters);

		double seamWave = 0.0;
		const double seamAmplitude = std::clamp(pitchParameters.seamPerturbationAmplitude, 0.0, 0.5);
		if (pitchParameters.enableSeamPerturbation && hasMeaningfulSpin)
		{
			const double seamAngle =
			    (TwoPi * pitchParameters.seamPerturbationFrequencyHz *
			     currentState.flightTimeSeconds) +
			    pitchParameters.seamPerturbationPhase;
			const double wrappedSeamAngle =
			    std::fmod(seamAngle, TwoPi);
			seamWave = std::sin(wrappedSeamAngle);
			dragCoefficient *= (1.0 + (SeamDragInfluence * seamAmplitude * seamWave));
			liftCoefficient *= (1.0 + (SeamLiftInfluence * seamAmplitude * seamWave));
		}

		const double speedSquared = speed * speed;
		const Vector3 dragAcceleration =
		    -velocityDirection * (dragCoefficient * speedSquared);
		const Vector3 magnusDirection = NormalizeOrZero(Cross(normalizedSpinAxis, velocityDirection));
		const Vector3 magnusAcceleration = magnusDirection * (liftCoefficient * speed * spinFactor);

		Vector3 seamSideAcceleration{};
		if (pitchParameters.enableSeamPerturbation && hasMeaningfulSpin)
		{
			const Vector3 seamSideDirection = NormalizeOrZero(
			    normalizedSpinAxis -
			    (velocityDirection * Dot(normalizedSpinAxis, velocityDirection)));
			seamSideAcceleration = seamSideDirection * (liftCoefficient * speed * SeamSideForceScale * seamAmplitude * seamWave);
		}

		return dragAcceleration + magnusAcceleration + seamSideAcceleration;
	}

	std::pair<double, double> PitchPhysicsEngine::ResolveAerodynamicCoefficients(double speed, double spinFactor, const Baseball::PitchParameters& pitchParameters) const
	{
		if (!pitchParameters.useCoefficientTable)
		{
			return std::pair<double, double>{ pitchParameters.dragCoefficient, pitchParameters.liftCoefficient };
		}

		const double tableDragCoefficient = Interpolate2D(DragCoefficientTable, SpeedBreakpoints, SpinBreakpoints, speed, spinFactor);
		const double tableLiftCoefficient = Interpolate2D(LiftCoefficientTable, SpeedBreakpoints, SpinBreakpoints, speed, spinFactor);
		return std::pair<double, double>{ tableDragCoefficient, tableLiftCoefficient };
	}
} // namespace BaseballSimulatorVR::Physics
