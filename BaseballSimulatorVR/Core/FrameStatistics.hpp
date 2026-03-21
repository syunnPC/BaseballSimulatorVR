#pragma once

#include <chrono>
#include <optional>

namespace BaseballSimulatorVR::Core
{
	struct FrameStatisticsSnapshot final
	{
		double framesPerSecond{ 0.0 };
		double averageFrameTimeMilliseconds{ 0.0 };
		double maxFrameTimeMilliseconds{ 0.0 };
	};

	class FrameStatistics final
	{
	  public:
		void AddFrameSample(std::chrono::duration<double> frameDuration);

		[[nodiscard]] std::optional<FrameStatisticsSnapshot> ConsumePendingSnapshot();
		[[nodiscard]] FrameStatisticsSnapshot GetLatestSnapshot() const noexcept;

	  private:
		std::chrono::duration<double> m_accumulatedDuration{ 0.0 };
		std::chrono::duration<double> m_maxFrameDuration{ 0.0 };
		std::size_t m_frameCount{ 0 };
		FrameStatisticsSnapshot m_latestSnapshot{};
		std::optional<FrameStatisticsSnapshot> m_pendingSnapshot{};
	};
} // namespace BaseballSimulatorVR::Core
