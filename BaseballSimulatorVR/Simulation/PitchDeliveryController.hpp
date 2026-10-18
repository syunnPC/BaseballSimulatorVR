#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

namespace BaseballSimulatorVR::Simulation
{
	// A single simulation clock drives both the throwing hand and ball release.
	class PitchDeliveryController final
	{
	  public:
		static constexpr double WindupSeconds = 1.2;

		void Reset() noexcept { m_secondsFromRelease = -WindupSeconds; }
		[[nodiscard]] double GetSecondsFromRelease() const noexcept { return m_secondsFromRelease; }
		[[nodiscard]] bool HasReleased() const noexcept { return m_secondsFromRelease >= 0.0; }

		[[nodiscard]] std::chrono::duration<double> Advance(std::chrono::duration<double> elapsed) noexcept
		{
			if (!std::isfinite(elapsed.count()) || elapsed.count() <= 0.0) return {};
			const double previousFlightSeconds = (std::max)(0.0, m_secondsFromRelease);
			m_secondsFromRelease += elapsed.count();
			// Remove floating-point residue at the exact release boundary.
			if (std::abs(m_secondsFromRelease) < 1e-12) m_secondsFromRelease = 0.0;
			return std::chrono::duration<double>((std::max)(0.0, m_secondsFromRelease) - previousFlightSeconds);
		}

	  private:
		double m_secondsFromRelease{ -WindupSeconds };
	};
}
