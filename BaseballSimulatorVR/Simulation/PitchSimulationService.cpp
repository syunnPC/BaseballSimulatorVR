#include "PitchSimulationService.hpp"

namespace BaseballSimulatorVR::Simulation
{
	PitchSimulationService::PitchSimulationService()
	    : m_pitchPhysicsEngine()
	    , m_pitchParameters()
	    , m_ballState()
	{
		Reset(Baseball::PitchParameters{});
	}

	void PitchSimulationService::Reset(const Baseball::PitchParameters& pitchParameters)
	{
		m_pitchParameters = pitchParameters;
		m_ballState = Physics::BallState{};
		m_ballState.position.y = m_pitchParameters.releaseHeightMeters;
		m_ballState.velocity = m_pitchParameters.initialVelocity;
		m_ballState.spinAxis = Physics::NormalizeOrZero(m_pitchParameters.spinAxis);
		m_ballState.spinRpm = m_pitchParameters.spinRpm;
	}

	void PitchSimulationService::Tick(std::chrono::duration<double> deltaTime)
	{
		if (deltaTime <= std::chrono::duration<double>::zero())
		{
			return;
		}

		m_ballState = m_pitchPhysicsEngine.Step(m_ballState, m_pitchParameters, deltaTime);
	}

	void PitchSimulationService::SetBallState(const Physics::BallState& ballState)
	{
		m_ballState = ballState;
		m_pitchParameters.initialVelocity = ballState.velocity;
		m_pitchParameters.releaseHeightMeters = ballState.position.y;
		m_pitchParameters.spinAxis =
		    Physics::NormalizeOrZero(ballState.spinAxis);
		m_pitchParameters.spinRpm = ballState.spinRpm;
	}

	const Physics::BallState& PitchSimulationService::GetBallState() const noexcept
	{
		return m_ballState;
	}

	const Baseball::PitchParameters& PitchSimulationService::GetPitchParameters() const noexcept
	{
		return m_pitchParameters;
	}
} // namespace BaseballSimulatorVR::Simulation
