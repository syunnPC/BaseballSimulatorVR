#include "PitchPhysicsEngine.hpp"
#include "BattedBallAerodynamics.hpp"


#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace BaseballSimulatorVR::Physics
{
	namespace
	{
		constexpr double ReferenceWindHeightMeters = 1.0;
		constexpr double SeamDragInfluence = 0.20;
		constexpr double SeamLiftInfluence = 0.40;
		constexpr double SeamSideForceScale = 0.28;
		constexpr double TwoPi = 2.0 * std::numbers::pi;
		// Peak lateral force coefficient on a knuckleball's seams; see KnuckleballAcceleration.
		constexpr double KnuckleballLateralCoefficient = 0.20;

		[[nodiscard]] bool IsFinite(const double value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool IsFinite(const Vector3& value) noexcept
		{
			return IsFinite(value.x) &&
			       IsFinite(value.y) &&
			       IsFinite(value.z);
		}

		[[nodiscard]] double FiniteOr(
		    const double value,
		    const double fallback) noexcept
		{
			return IsFinite(value) ? value : fallback;
		}

		[[nodiscard]] Vector3 FiniteOrZero(const Vector3& value) noexcept
		{
			return IsFinite(value) ? value : Vector3{};
		}

		// A knuckleball turns less than once on its way to the plate, so its seams sit nearly
		// still against the air and the flow separates unevenly around them. Wind-tunnel
		// measurements on a non-spinning baseball (Watts & Sawyer 1975) show the resulting
		// sideways force reversing four times per turn of the ball and peaking at about a
		// fifth of the dynamic pressure times its cross section. The push acts across the
		// plane the seams turn in, so as they slowly turn the ball darts one way and then the
		// other.
		[[nodiscard]] Vector3 KnuckleballAcceleration(const BallState& state,
		    const Baseball::PitchParameters& parameters, const Vector3& velocityDirection, const double speed,
		    const double airDensity) noexcept
		{
			if (!parameters.knuckleball)
			{
				return {};
			}
			Vector3 across = Cross(NormalizeOrZero(state.spinAxis), velocityDirection);
			if (LengthSquared(across) < 1e-12)
			{
				across = Cross(Vector3{ 1.0, 0.0, 0.0 }, velocityDirection);
			}
			across = NormalizeOrZero(across);
			const double turn = std::abs(FiniteOr(state.spinRpm, 0.0)) * (TwoPi / 60.0) *
			        FiniteOr(state.flightTimeSeconds, 0.0) +
			    FiniteOr(parameters.seamPerturbationPhase, 0.0);
			const double coefficient = KnuckleballLateralCoefficient * std::sin(4.0 * std::fmod(turn, TwoPi));
			return across * (AerodynamicAccelerationScale(state.properties, airDensity) * coefficient * speed * speed);
		}

		[[nodiscard]] BallState SanitizeBallState(
		    const BallState& ballState) noexcept
		{
			const BallState defaults{};
			BallState sanitized = ballState;
			if (!IsFinite(sanitized.position))
			{
				sanitized.position = defaults.position;
			}
			if (!IsFinite(sanitized.velocity))
			{
				sanitized.velocity = defaults.velocity;
			}
			if (!IsFinite(sanitized.spinAxis))
			{
				sanitized.spinAxis = defaults.spinAxis;
			}

			sanitized.spinAxis = NormalizeOrZero(sanitized.spinAxis);
			sanitized.spinRpm = FiniteOr(sanitized.spinRpm, defaults.spinRpm);
			sanitized.flightTimeSeconds = std::max(
			    0.0,
			    FiniteOr(
			        sanitized.flightTimeSeconds,
			        defaults.flightTimeSeconds));
			return sanitized;
		}
	} // namespace

	PitchPhysicsEngine::PitchPhysicsEngine(Vector3 gravity)
	    : m_gravity(gravity)
	{
	}

	BallState PitchPhysicsEngine::Step(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, std::chrono::duration<double> deltaTime) const
	{
		const double deltaSeconds = deltaTime.count();
		if (!IsFinite(deltaSeconds) ||
		    (deltaSeconds <= 0.0))
		{
			return currentState;
		}

		const BallState sanitizedCurrentState = SanitizeBallState(currentState);
		if (!sanitizedCurrentState.properties.IsValid())
			throw std::invalid_argument("Invalid ball radius or mass");
		BallState nextState = sanitizedCurrentState;
		switch (pitchParameters.integratorType)
		{
			case PitchIntegratorType::SemiImplicitEuler:
				nextState = StepSemiImplicitEuler(
				    sanitizedCurrentState,
				    pitchParameters,
				    deltaSeconds);
				break;
			case PitchIntegratorType::Rk4:
				nextState = StepRk4(
				    sanitizedCurrentState,
				    pitchParameters,
				    deltaSeconds);
				break;
			default:
				nextState = StepSemiImplicitEuler(
				    sanitizedCurrentState,
				    pitchParameters,
				    deltaSeconds);
				break;
		}

		if (nextState.aerodynamicsModel == BallAerodynamicsModel::Physical)
		{
			nextState.spinRpm *= std::exp(-deltaSeconds / PhysicalSpinDecayTimeConstantSeconds);
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
		const bool decaysSpin = currentState.aerodynamicsModel == BallAerodynamicsModel::Physical;
		const auto accelerationAtState = [&](const Vector3& position, const Vector3& velocity, double flightTimeSeconds) -> Vector3
		{
			BallState sampleState = currentState;
			sampleState.position = position;
			sampleState.velocity = velocity;
			sampleState.flightTimeSeconds = flightTimeSeconds;
			if (decaysSpin)
			{
				// Evaluate each stage with the spin it has at that time, so the exponential
				// decay keeps RK4's step-size convergence.
				sampleState.spinRpm *= std::exp(
				    -(flightTimeSeconds - currentState.flightTimeSeconds) / PhysicalSpinDecayTimeConstantSeconds);
			}
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
		const Vector3 velocity = FiniteOrZero(currentState.velocity);
		Vector3 windVelocity = FiniteOrZero(pitchParameters.windVelocity);
		if (pitchParameters.enableHeightDependentWind)
		{
			const double heightDelta = std::max(
			    FiniteOr(
			        currentState.position.y,
			        ReferenceWindHeightMeters) -
			        ReferenceWindHeightMeters,
			    0.0);
			windVelocity.x += FiniteOr(pitchParameters.windShearFactor, 0.0) *
			                  heightDelta;
		}

		const Vector3 relativeAirVelocity = velocity - windVelocity;
		const double speed = Length(relativeAirVelocity);
		if (!IsFinite(speed) ||
		    (speed <= 1e-6))
		{
			return Vector3{};
		}

		const double airDensity = IsPlausibleAirDensity(pitchParameters.airDensityKilogramsPerCubicMeter)
		    ? pitchParameters.airDensityKilogramsPerCubicMeter
		    : StandardAirDensityKilogramsPerCubicMeter;
		switch (currentState.aerodynamicsModel)
		{
			case BallAerodynamicsModel::Physical:
				return ComputeBattedBallAerodynamicAcceleration(relativeAirVelocity,
				    currentState.spinAxis, currentState.spinRpm, currentState.properties, airDensity);
			case BallAerodynamicsModel::PitchProfile:
				break;
			default:
				throw std::invalid_argument("Unknown ball aerodynamics model");
		}

		const Vector3 velocityDirection = relativeAirVelocity / speed;
		const Vector3 normalizedSpinAxis = NormalizeOrZero(
		    FiniteOrZero(currentState.spinAxis));
		const double spinFactor = std::clamp(
		    FiniteOr(currentState.spinRpm, 0.0) / 3000.0,
		    0.0,
		    2.0);
		const bool hasMeaningfulSpin =
		    (spinFactor > 1e-6) &&
		    (LengthSquared(normalizedSpinAxis) > 1e-12);
		auto [dragCoefficient, liftCoefficient] = ResolveAerodynamicCoefficients(
		    speed, FiniteOr(currentState.spinRpm, 0.0), spinFactor, currentState.properties, pitchParameters);
		dragCoefficient = std::max(0.0, FiniteOr(dragCoefficient, 0.0));
		liftCoefficient = FiniteOr(liftCoefficient, 0.0);
		if (!pitchParameters.useCoefficientTable)
		{
			// Fixed profile coefficients are specified at standard air density.
			const double densityScale = airDensity / StandardAirDensityKilogramsPerCubicMeter;
			dragCoefficient *= densityScale;
			liftCoefficient *= densityScale;
		}

		double seamWave = 0.0;
		const double seamAmplitude = std::clamp(
		    FiniteOr(pitchParameters.seamPerturbationAmplitude, 0.0),
		    0.0,
		    0.5);
		if (pitchParameters.enableSeamPerturbation && hasMeaningfulSpin)
		{
			const double seamAngle =
			    (TwoPi *
			         FiniteOr(pitchParameters.seamPerturbationFrequencyHz, 0.0) *
			         FiniteOr(currentState.flightTimeSeconds, 0.0)) +
			    FiniteOr(pitchParameters.seamPerturbationPhase, 0.0);
			if (IsFinite(seamAngle))
			{
				const double wrappedSeamAngle = std::fmod(seamAngle, TwoPi);
				seamWave = std::sin(wrappedSeamAngle);
			}
			dragCoefficient *= (1.0 + (SeamDragInfluence * seamAmplitude * seamWave));
			liftCoefficient *= (1.0 + (SeamLiftInfluence * seamAmplitude * seamWave));
		}

		const double speedSquared = speed * speed;
		if (!IsFinite(speedSquared))
		{
			return Vector3{};
		}
		const Vector3 dragAcceleration =
		    -velocityDirection * (dragCoefficient * speedSquared);
		// Keep the transverse spin fraction so Magnus lift fades continuously toward gyrospin.
		const Vector3 magnusDirection = Cross(normalizedSpinAxis, velocityDirection);
		const Vector3 magnusAcceleration = magnusDirection * (liftCoefficient * speed * spinFactor);

		Vector3 seamSideAcceleration{};
		if (pitchParameters.enableSeamPerturbation && hasMeaningfulSpin)
		{
			// This remains a heuristic seam perturbation. Keep its projection magnitude
			// to avoid a discontinuous side force when the spin approaches the air direction.
			const Vector3 seamSideDirection = normalizedSpinAxis -
			    (velocityDirection * Dot(normalizedSpinAxis, velocityDirection));
			seamSideAcceleration = seamSideDirection * (liftCoefficient * speed * SeamSideForceScale * seamAmplitude * seamWave);
		}

		return dragAcceleration + magnusAcceleration + seamSideAcceleration +
		    KnuckleballAcceleration(currentState, pitchParameters, velocityDirection, speed, airDensity);
	}

	std::pair<double, double> PitchPhysicsEngine::ResolveAerodynamicCoefficients(
	    const double speed, const double spinRpm, const double spinFactor,
	    const BallProperties& ballProperties, const Baseball::PitchParameters& pitchParameters) const
	{
		if (!pitchParameters.useCoefficientTable)
		{
			return std::pair<double, double>{
				FiniteOr(pitchParameters.dragCoefficient, 0.0),
				FiniteOr(pitchParameters.liftCoefficient, 0.0)
			};
		}

		// Speed/spin-dependent coefficients from the same Nathan (2017) fits as the
		// physical model, expressed in the profile model's units: drag [1/m] multiplies
		// v^2 and lift [1/s] multiplies v * spinFactor.
		const double airDensity = IsPlausibleAirDensity(pitchParameters.airDensityKilogramsPerCubicMeter)
		    ? pitchParameters.airDensityKilogramsPerCubicMeter
		    : StandardAirDensityKilogramsPerCubicMeter;
		const BallProperties properties = ballProperties.IsValid() ? ballProperties : BallProperties{};
		const double scale = AerodynamicAccelerationScale(properties, airDensity);
		const double drag = scale * NathanDragCoefficient(spinRpm);
		const double angularSpeed = std::abs(spinRpm) * (2.0 * std::numbers::pi / 60.0);
		if (!(speed > 1e-6) || !(spinFactor > 1e-9))
		{
			return std::pair<double, double>{ drag, 0.0 };
		}
		// Nathan's fit comes from batted balls and over-predicts pitched-ball lift by
		// about 1.3x: scaled so a 95 mph, 2300 rpm four-seamer with 90% active spin
		// gets the MLB-typical ~16 in of induced vertical break.
		constexpr double PitchedBallLiftScale = 0.78;
		const double liftCoefficient = PitchedBallLiftScale *
		    NathanLiftCoefficient(properties.radiusMeters * angularSpeed / speed);
		return std::pair<double, double>{ drag, scale * liftCoefficient * speed / spinFactor };
	}
} // namespace BaseballSimulatorVR::Physics
