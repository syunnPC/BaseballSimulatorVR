#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchDeliveryController.hpp"
#include "../BaseballSimulatorVR/Simulation/PitchSimulationService.hpp"

#include <cmath>
#include <limits>

void RunPitchDeliveryTests()
{
	using namespace BaseballSimulatorVR::Simulation;
	PitchDeliveryController delivery;
	PitchSimulationService simulation;
	const auto startPosition = simulation.GetBallState().position;
	const auto step = std::chrono::duration<double>(0.001);
	for (int tick = 0; tick < 1200; ++tick)
	{
		simulation.Tick(delivery.Advance(step));
	}
	Tests::Check(delivery.HasReleased() && delivery.GetSecondsFromRelease() == 0.0,
	             "Throwing hand releases at exactly 1.2 simulated seconds");
	Tests::Check(simulation.GetBallState().position.z == startPosition.z &&
	             simulation.GetBallState().flightTimeSeconds == 0.0,
	             "Ball stays at release point during windup");
	simulation.Tick(delivery.Advance(step));
	Tests::Check(simulation.GetBallState().flightTimeSeconds == 0.001, "Ball begins flight after release");
	Tests::Check(simulation.GetBallState().position.z > startPosition.z, "Released ball advances");
	delivery.Reset();
	Tests::Check(!delivery.HasReleased(), "Reset schedules another delivery");
	Tests::Check(delivery.Advance(std::chrono::duration<double>(1.1)).count() == 0.0, "Partial windup has no ball flight");
	Tests::Check(std::abs(delivery.Advance(std::chrono::duration<double>(0.3)).count() - 0.2) < 1e-12,
	             "A step spanning release integrates only its remaining flight time");
	const auto beforePause = delivery.GetSecondsFromRelease();
	Tests::Check(delivery.Advance(std::chrono::duration<double>(0.0)).count() == 0.0 &&
	             delivery.GetSecondsFromRelease() == beforePause, "Pause freezes pitcher and release together");
	Tests::Check(delivery.Advance(std::chrono::duration<double>(std::numeric_limits<double>::quiet_NaN())).count() == 0.0,
	             "Reject invalid elapsed time");
	PitchDeliveryController slow;
	for (int frame = 0; frame < 120; ++frame)
	{
		(void)slow.Advance(std::chrono::duration<double>(0.01 * 0.5));
	}
	Tests::Check(!slow.HasReleased(), "Playback slowdown delays hand and release equally");
}
