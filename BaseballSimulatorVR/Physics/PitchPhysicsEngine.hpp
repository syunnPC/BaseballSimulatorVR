#pragma once

#include "../Baseball/PitchParameters.hpp"
#include "BallState.hpp"

#include <chrono>
#include <utility>

namespace BaseballSimulatorVR::Physics
{
	class PitchPhysicsEngine final
	{
	  public:
		explicit PitchPhysicsEngine(Vector3 gravity = Vector3{ 0.0, -9.80665, 0.0 });

		[[nodiscard]] BallState Step(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, std::chrono::duration<double> deltaTime) const;

	  private:
		[[nodiscard]] BallState StepSemiImplicitEuler(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, double deltaSeconds) const;
		[[nodiscard]] BallState StepRk4(const BallState& currentState, const Baseball::PitchParameters& pitchParameters, double deltaSeconds) const;
		[[nodiscard]] Vector3 ComputeAerodynamicAcceleration(const BallState& currentState, const Baseball::PitchParameters& pitchParameters) const;
		[[nodiscard]] std::pair<double, double> ResolveAerodynamicCoefficients(double speed, double spinFactor, const Baseball::PitchParameters& pitchParameters) const;

		Vector3 m_gravity;
	};
} // namespace BaseballSimulatorVR::Physics
