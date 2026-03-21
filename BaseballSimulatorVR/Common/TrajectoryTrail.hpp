#pragma once

#include "../Physics/BallState.hpp"
#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace BaseballSimulatorVR::Common
{
	struct TrajectoryTrailState final
	{
		double lastFlightTimeSeconds{ 0.0 };
		std::chrono::steady_clock::time_point lastAdvanceTime{};
		float fadeScale{ 1.0F };
	};

	constexpr std::size_t MaxTrajectoryPoints = 96;
	constexpr double TrajectoryFadeDelaySeconds = 0.20;
	constexpr double TrajectoryFadeDurationSeconds = 1.10;

	inline void UpdateTrajectoryTrail(
	    const Physics::BallState& ballState,
	    std::vector<Physics::Vector3>* const trajectoryPoints,
	    TrajectoryTrailState* const trailState,
	    const std::chrono::steady_clock::time_point now)
	{
		if ((trajectoryPoints == nullptr) || (trailState == nullptr))
		{
			return;
		}

		const double previousFlightTimeSeconds = trailState->lastFlightTimeSeconds;
		const double velocityMagnitudeSquared =
		    Physics::LengthSquared(ballState.velocity);
		const bool isStaticBallSample =
		    (ballState.flightTimeSeconds <= 1e-6) &&
		    (velocityMagnitudeSquared <= 1e-8);
		const bool isDynamicSampleAtCycleStart =
		    (ballState.flightTimeSeconds <= 1e-6) &&
		    (velocityMagnitudeSquared > 1e-8) &&
		    (previousFlightTimeSeconds <= 1e-6);
		if (isStaticBallSample || isDynamicSampleAtCycleStart ||
		    (ballState.flightTimeSeconds < previousFlightTimeSeconds))
		{
			trajectoryPoints->clear();
			trailState->fadeScale = 1.0F;
			trailState->lastAdvanceTime = now;
		}

		trailState->lastFlightTimeSeconds = ballState.flightTimeSeconds;
		bool trajectoryAdvanced = false;
		if (trajectoryPoints->empty())
		{
			trajectoryPoints->push_back(ballState.position);
			trajectoryAdvanced = true;
		}
		else
		{
			const Physics::Vector3& lastPoint = trajectoryPoints->back();
			const Physics::Vector3 delta = ballState.position - lastPoint;
			if (Physics::LengthSquared(delta) >= 0.0016)
			{
				trajectoryPoints->push_back(ballState.position);
				if (trajectoryPoints->size() > MaxTrajectoryPoints)
				{
					trajectoryPoints->erase(trajectoryPoints->begin());
				}
				trajectoryAdvanced = true;
			}
		}

		if (trajectoryAdvanced)
		{
			trailState->fadeScale = 1.0F;
			trailState->lastAdvanceTime = now;
			return;
		}
		if (ballState.flightTimeSeconds > (previousFlightTimeSeconds + 1e-6))
		{
			trailState->fadeScale = 1.0F;
			trailState->lastAdvanceTime = now;
			return;
		}

		if (trajectoryPoints->size() < 2)
		{
			trailState->fadeScale = 1.0F;
			return;
		}

		const double staleSeconds =
		    std::chrono::duration<double>(now - trailState->lastAdvanceTime)
		        .count();
		const double fadedSeconds = staleSeconds - TrajectoryFadeDelaySeconds;
		const double fadeProgress = std::clamp(
		    fadedSeconds / TrajectoryFadeDurationSeconds,
		    0.0,
		    1.0);
		trailState->fadeScale = static_cast<float>(1.0 - fadeProgress);
		if (trailState->fadeScale <= 0.001F)
		{
			trajectoryPoints->clear();
			trailState->fadeScale = 1.0F;
			trailState->lastAdvanceTime = now;
		}
	}
} // namespace BaseballSimulatorVR::Common
