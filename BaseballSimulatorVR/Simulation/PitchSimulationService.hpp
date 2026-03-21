#pragma once

#include "../Baseball/PitchParameters.hpp"
#include "../Physics/BallState.hpp"
#include "../Physics/PitchPhysicsEngine.hpp"

#include <chrono>

namespace BaseballSimulatorVR::Simulation
{
	class PitchSimulationService final
	{
	  public:
		PitchSimulationService();

		void Reset(const Baseball::PitchParameters& pitchParameters);
		void Tick(std::chrono::duration<double> deltaTime);
		void SetBallState(const Physics::BallState& ballState);

		[[nodiscard]] const Physics::BallState& GetBallState() const noexcept;
		[[nodiscard]] const Baseball::PitchParameters& GetPitchParameters() const noexcept;

	  private:
		Physics::PitchPhysicsEngine m_pitchPhysicsEngine;
		Baseball::PitchParameters m_pitchParameters;
		Physics::BallState m_ballState;
	};
} // namespace BaseballSimulatorVR::Simulation
