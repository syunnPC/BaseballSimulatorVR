#include "PitchSimulationService.hpp"

#include <cmath>

namespace BaseballSimulatorVR::Simulation
{
	namespace
	{
		[[nodiscard]] bool IsFinite(const double value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool IsFinite(const Physics::Vector3& value) noexcept
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

		[[nodiscard]] Physics::Vector3 FiniteOr(
		    const Physics::Vector3& value,
		    const Physics::Vector3& fallback) noexcept
		{
			return IsFinite(value) ? value : fallback;
		}
	} // namespace

	PitchSimulationService::PitchSimulationService()
	    : m_pitchPhysicsEngine()
	    , m_pitchParameters()
	    , m_ballState()
	{
		Reset(Baseball::PitchParameters{});
	}

	void PitchSimulationService::Reset(const Baseball::PitchParameters& pitchParameters)
	{
		const Baseball::PitchParameters defaults{};
		m_pitchParameters = pitchParameters;
		m_pitchParameters.initialVelocity = FiniteOr(
		    m_pitchParameters.initialVelocity,
		    defaults.initialVelocity);
		m_pitchParameters.releaseHeightMeters = FiniteOr(
		    m_pitchParameters.releaseHeightMeters,
		    defaults.releaseHeightMeters);
		m_pitchParameters.spinAxis = FiniteOr(
		    m_pitchParameters.spinAxis,
		    defaults.spinAxis);
		m_pitchParameters.windVelocity = FiniteOr(
		    m_pitchParameters.windVelocity,
		    defaults.windVelocity);
		m_pitchParameters.spinRpm = FiniteOr(
		    m_pitchParameters.spinRpm,
		    defaults.spinRpm);
		m_pitchParameters.dragCoefficient = FiniteOr(
		    m_pitchParameters.dragCoefficient,
		    defaults.dragCoefficient);
		m_pitchParameters.liftCoefficient = FiniteOr(
		    m_pitchParameters.liftCoefficient,
		    defaults.liftCoefficient);
		m_pitchParameters.windShearFactor = FiniteOr(
		    m_pitchParameters.windShearFactor,
		    defaults.windShearFactor);
		m_pitchParameters.seamPerturbationAmplitude = FiniteOr(
		    m_pitchParameters.seamPerturbationAmplitude,
		    defaults.seamPerturbationAmplitude);
		m_pitchParameters.seamPerturbationFrequencyHz = FiniteOr(
		    m_pitchParameters.seamPerturbationFrequencyHz,
		    defaults.seamPerturbationFrequencyHz);
		m_pitchParameters.seamPerturbationPhase = FiniteOr(
		    m_pitchParameters.seamPerturbationPhase,
		    defaults.seamPerturbationPhase);

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
		const Physics::BallState defaults{};
		m_ballState = ballState;
		m_ballState.position = FiniteOr(
		    m_ballState.position,
		    defaults.position);
		m_ballState.velocity = FiniteOr(
		    m_ballState.velocity,
		    defaults.velocity);
		m_ballState.spinAxis = Physics::NormalizeOrZero(
		    FiniteOr(
		        m_ballState.spinAxis,
		        defaults.spinAxis));
		m_ballState.spinRpm = FiniteOr(
		    m_ballState.spinRpm,
		    defaults.spinRpm);
		m_ballState.flightTimeSeconds = FiniteOr(
		    m_ballState.flightTimeSeconds,
		    defaults.flightTimeSeconds);
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
