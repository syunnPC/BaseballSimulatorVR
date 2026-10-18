#include "FrameStatistics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace BaseballSimulatorVR::Core
{
	void FrameStatistics::AddFrameSample(std::chrono::duration<double> frameDuration)
	{
		if (!std::isfinite(frameDuration.count()) ||
		    frameDuration <= std::chrono::duration<double>::zero())
		{
			return;
		}

		m_accumulatedDuration += frameDuration;
		m_maxFrameDuration = std::max(m_maxFrameDuration, frameDuration);
		++m_frameCount;

		constexpr auto sampleInterval = std::chrono::duration<double>(1.0);
		if (m_accumulatedDuration < sampleInterval)
		{
			return;
		}

		const double sampledSeconds = m_accumulatedDuration.count();
		FrameStatisticsSnapshot snapshot{};
		snapshot.framesPerSecond = static_cast<double>(m_frameCount) / sampledSeconds;
		snapshot.averageFrameTimeMilliseconds = (sampledSeconds * 1000.0) / static_cast<double>(m_frameCount);
		snapshot.maxFrameTimeMilliseconds = m_maxFrameDuration.count() * 1000.0;

		m_latestSnapshot = snapshot;
		m_pendingSnapshot = snapshot;
		m_accumulatedDuration = std::chrono::duration<double>::zero();
		m_maxFrameDuration = std::chrono::duration<double>::zero();
		m_frameCount = 0;
	}

	std::optional<FrameStatisticsSnapshot> FrameStatistics::ConsumePendingSnapshot()
	{
		return std::exchange(m_pendingSnapshot, std::nullopt);
	}

	FrameStatisticsSnapshot FrameStatistics::GetLatestSnapshot() const noexcept
	{
		return m_latestSnapshot;
	}
} // namespace BaseballSimulatorVR::Core
