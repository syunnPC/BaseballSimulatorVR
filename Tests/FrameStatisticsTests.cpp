#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Core/FrameStatistics.hpp"

#include <cmath>
#include <limits>

void RunFrameStatisticsTests()
{
	using namespace BaseballSimulatorVR::Core;
	FrameStatistics statistics;
	const auto sample = [&](double seconds) { statistics.AddFrameSample(std::chrono::duration<double>(seconds)); };
	Tests::Check(!statistics.ConsumePendingSnapshot(), "No initial statistics snapshot");
	sample(std::numeric_limits<double>::quiet_NaN());
	sample(std::numeric_limits<double>::infinity());
	sample(0.0);
	sample(-1.0);
	Tests::Check(!statistics.ConsumePendingSnapshot(), "Invalid frame durations must be ignored");
	for (int frame = 0; frame < 4; ++frame) sample(0.25);
	const auto first = statistics.ConsumePendingSnapshot();
	Tests::Check(first.has_value(), "Valid samples recover after invalid durations");
	Tests::Check(first->framesPerSecond == 4.0, "Four frames per second");
	Tests::Check(first->averageFrameTimeMilliseconds == 250.0, "Average excludes rejected samples");
	Tests::Check(first->maxFrameTimeMilliseconds == 250.0, "Maximum excludes rejected samples");
	Tests::Check(!statistics.ConsumePendingSnapshot(), "Pending snapshot consumed only once");
	sample(0.125);
	sample(0.875);
	const auto second = statistics.ConsumePendingSnapshot();
	Tests::Check(second && second->framesPerSecond == 2.0, "Next sample window resets counters");
	Tests::Check(second->maxFrameTimeMilliseconds == 875.0, "Largest frame in next window");
	Tests::Check(statistics.GetLatestSnapshot().averageFrameTimeMilliseconds == 500.0,
	             "Latest snapshot persists after consumption");
}
